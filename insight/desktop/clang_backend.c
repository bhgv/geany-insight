/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "clang_backend.h"
#include <string.h>
gboolean insight_clang_source(const gchar *path) {
 const gchar *dot=path ? strrchr(path,'.'):NULL;if(!dot) return FALSE;const gchar *extensions[]={".c",".h",".cc",".cpp",".cxx",".hpp",".hh",".hxx"};for(guint i=0;i<G_N_ELEMENTS(extensions);i++) if(!g_ascii_strcasecmp(dot,extensions[i])) return TRUE;return FALSE;
}
static gchar *dependency_stamp(const gchar *path) {
 GFile *file=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(file,"standard::size,time::modified,time::modified-usec",G_FILE_QUERY_INFO_NONE,NULL,NULL);g_object_unref(file);if(!info) return g_strdup("missing");
 gchar *stamp=g_strdup_printf("%"G_GUINT64_FORMAT":%"G_GUINT64_FORMAT":%u",(guint64)g_file_info_get_size(info),g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_TIME_MODIFIED),g_file_info_get_attribute_uint32(info,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC));g_object_unref(info);return stamp;
}
gboolean insight_clang_dependencies_fresh(const InsightIndex *relations,const gchar *path) {
 gsize count=insight_index_query(relations,"",FALSE,NULL,"@clanginclude",path,0,NULL,0);if(count>4096) return FALSE;const InsightSymbol **rows=g_new(const InsightSymbol *,count);insight_index_query(relations,"",FALSE,NULL,"@clanginclude",path,0,rows,count);gboolean fresh=TRUE;
 for(gsize i=0;i<count && fresh;i++) {gchar *now=dependency_stamp(rows[i]->name);fresh=!g_strcmp0(now,rows[i]->signature);g_free(now);}g_free(rows);return fresh;
}
gchar *insight_clang_symbol_at(InsightIndex *relations,const gchar *path,guint offset,guint line,const gchar *name) {
 if(!relations || !path || !name) return NULL;gsize n=insight_index_query(relations,name,TRUE,NULL,"@clangref",path,0,NULL,0);const InsightSymbol **rows=g_new(const InsightSymbol *,n);insight_index_query(relations,name,TRUE,NULL,"@clangref",path,0,rows,n);gchar *usr=NULL;guint64 smallest=G_MAXUINT64;
 for(gsize i=0;i<n;i++) {gchar **parts=g_strsplit(rows[i]->signature,":",3);if(g_strv_length(parts)==3) {guint64 a=g_ascii_strtoull(parts[0],NULL,10),z=g_ascii_strtoull(parts[1],NULL,10);if(a<=offset && offset<=z && z-a<smallest) {g_free(usr);usr=g_strdup(rows[i]->scope);smallest=z-a;}}g_strfreev(parts);}g_free(rows);if(usr) return usr;
 n=insight_index_query(relations,"",FALSE,NULL,"@clangdef",path,0,NULL,0);rows=g_new(const InsightSymbol *,n);insight_index_query(relations,"",FALSE,NULL,"@clangdef",path,0,rows,n);gboolean ambiguous=FALSE;
 for(gsize i=0;i<n;i++) if(rows[i]->line==line && !g_strcmp0(rows[i]->signature,name)) {if(usr && strcmp(usr,rows[i]->name)) ambiguous=TRUE;g_free(usr);usr=g_strdup(rows[i]->name);}g_free(rows);if(ambiguous) g_clear_pointer(&usr,g_free);return usr;
}
void insight_clang_result_free(InsightClangResult *r) {if(!r) return;insight_index_free(r->symbols);insight_index_free(r->relations);g_array_unref(r->tokens);if(r->attributes) g_hash_table_unref(r->attributes);g_free(r->digest);g_free(r->diagnostics);g_free(r);}
static gboolean import_index(InsightIndex *from,InsightIndex *to,const gchar *path) {
 gsize n=insight_index_count(from);InsightSymbol *rows=g_new(InsightSymbol,n);for(gsize i=0;i<n;i++) rows[i]=*insight_index_at(from,i);gboolean ok=insight_index_update(to,path,rows,n);g_free(rows);return ok;
}
gboolean insight_clang_import(InsightClangResult *r,const gchar *path,InsightIndex *symbols,InsightIndex *relations) {return r && !r->limited && import_index(r->symbols,symbols,path) && import_index(r->relations,relations,path);}
#ifdef HAVE_LIBCLANG
#include <clang-c/Index.h>
#include <clang-c/CXCompilationDatabase.h>
typedef struct { InsightClangResult *result;const gchar *path,*text;gsize length;GArray *symbols,*relations;GPtrArray *owned;GCancellable *cancel;guint visited;const gchar *owner; } Parse;
static gchar *string(CXString value) {const gchar *p=clang_getCString(value);gchar *copy=g_strdup(p ? p:"");clang_disposeString(value);return copy;}
static gchar *own(Parse *p,gchar *value) {g_ptr_array_add(p->owned,value);return value;}
static gboolean location(CXSourceLocation value,const gchar *path,guint *line,guint *column,guint *offset) {
 CXFile file=NULL;clang_getSpellingLocation(value,&file,line,column,offset);if(!file) return FALSE;gchar *name=string(clang_getFileName(file));gboolean same=!g_strcmp0(name,path);g_free(name);return same;
}
static const gchar *kind(CXCursor cursor) {
 switch(clang_getCursorKind(cursor)) {
 case CXCursor_FunctionDecl:return clang_isCursorDefinition(cursor) ? "f":"p";
 case CXCursor_CXXMethod:case CXCursor_Constructor:case CXCursor_Destructor:case CXCursor_ConversionFunction:return clang_isCursorDefinition(cursor) ? "f":"p";
 case CXCursor_FunctionTemplate:return clang_isCursorDefinition(cursor) ? "f":"p";
 case CXCursor_StructDecl:return "s";case CXCursor_UnionDecl:return "u";case CXCursor_ClassDecl:case CXCursor_ClassTemplate:return "c";
 case CXCursor_EnumDecl:return "g";case CXCursor_EnumConstantDecl:return "e";case CXCursor_FieldDecl:return "m";
 case CXCursor_ParmDecl:return "z";case CXCursor_VarDecl:{enum CXCursorKind parent=clang_getCursorKind(clang_getCursorSemanticParent(cursor));return parent==CXCursor_FunctionDecl || parent==CXCursor_CXXMethod || parent==CXCursor_Constructor ? "l":"v";}
 case CXCursor_TypedefDecl:case CXCursor_TypeAliasDecl:return "t";case CXCursor_Namespace:return "n";case CXCursor_MacroDefinition:return "d";default:return NULL;
 }
}
static InsightClangRole role(CXCursor cursor) {
 const gchar *k=kind(cursor);if(!k) return INSIGHT_CLANG_VARIABLE;
 if(strchr("sucgtn",*k)) return *k=='n' ? INSIGHT_CLANG_NAMESPACE:INSIGHT_CLANG_TYPE;
 if(*k=='f'||*k=='p') return INSIGHT_CLANG_FUNCTION;if(*k=='m') return INSIGHT_CLANG_MEMBER;if(*k=='e') return INSIGHT_CLANG_CONSTANT;if(*k=='d') return INSIGHT_CLANG_MACRO;return INSIGHT_CLANG_VARIABLE;
}
static gchar *scope(CXCursor cursor) {
 GPtrArray *parts=g_ptr_array_new_with_free_func(g_free);CXCursor parent=clang_getCursorSemanticParent(cursor);for(guint i=0;i<32 && !clang_Cursor_isNull(parent) && clang_getCursorKind(parent)!=CXCursor_TranslationUnit;i++) {gchar *name=string(clang_getCursorSpelling(parent));if(*name) g_ptr_array_add(parts,name);else g_free(name);parent=clang_getCursorSemanticParent(parent);}
 GString *out=g_string_new(NULL);for(guint i=parts->len;i>0;i--) {if(out->len) g_string_append(out,"::");g_string_append(out,g_ptr_array_index(parts,i-1));}g_ptr_array_unref(parts);return g_string_free(out,FALSE);
}
static gchar *snippet(Parse *p,guint offset) {gsize a=MIN(offset,p->length),z=a;while(a && p->text[a-1]!='\n' && z-a<512) a--;while(z<p->length && p->text[z]!='\n' && z-a<1024) z++;return g_utf8_make_valid(p->text+a,z-a);}
static void relation(Parse *p,const gchar *name,const gchar *usr,const gchar *type,guint line,guint column,guint begin,guint end) {
 if(strcmp(type,"@clangcall") && strcmp(type,"@clangvirtual")) {gsize start=MIN(begin,p->length);while(start && p->text[start-1]!='\n') start--;column=(guint)g_utf8_strlen(p->text+start,MIN(begin,p->length)-start)+1;}
 gchar *positions=own(p,g_strdup_printf("%u:%u:%u",begin,end,column));InsightSymbol row={own(p,g_strdup(name)),p->path,own(p,g_strdup(usr)),type,line,positions,own(p,snippet(p,begin)),""};g_array_append_val(p->relations,row);
}
static gchar *parameters(Parse *p,CXCursor cursor,const gchar *fallback) {
 if(clang_getCursorKind(cursor)==CXCursor_MacroDefinition && clang_Cursor_isMacroFunctionLike(cursor)) {
  CXTranslationUnit unit=clang_Cursor_getTranslationUnit(cursor);CXToken *tokens=NULL;unsigned count=0;clang_tokenize(unit,clang_getCursorExtent(cursor),&tokens,&count);GString *args=g_string_new(NULL);gboolean opened=FALSE,closed=FALSE;
  for(guint i=1;i<count && i<4096;i++) {if(clang_getTokenKind(tokens[i])==CXToken_Comment) continue;gchar *token=string(clang_getTokenSpelling(unit,tokens[i]));if(!opened) {opened=!strcmp(token,"(");if(opened) g_string_append_c(args,'(');}else if(!strcmp(token,")")) {g_string_append_c(args,')');closed=TRUE;g_free(token);break;}else if(!strcmp(token,",")) g_string_append(args,", ");else g_string_append(args,token);g_free(token);}
  clang_disposeTokens(unit,tokens,count);if(closed) return g_string_free(args,FALSE);g_string_free(args,TRUE);return g_strdup(fallback);
 }
 int count=clang_Cursor_getNumArguments(cursor);if(count<0 || count>1024) return g_strdup(fallback);GString *args=g_string_new("(");gboolean valid=TRUE;
 for(int i=0;i<count;i++) {CXCursor arg=clang_Cursor_getArgument(cursor,(unsigned)i);CXSourceRange range=clang_getCursorExtent(arg);guint first=0,last=0;
  if(!location(clang_getRangeStart(range),p->path,NULL,NULL,&first) || !location(clang_getRangeEnd(range),p->path,NULL,NULL,&last) || first>=last || last>p->length || last-first>4096 || memchr(p->text+first,'\n',last-first) || memchr(p->text+first,'\r',last-first)) {valid=FALSE;break;}
  if(i) g_string_append(args,", ");g_string_append_len(args,p->text+first,last-first);
 }
 CXType type=clang_getCursorType(cursor);if(valid && clang_isFunctionTypeVariadic(type)) {if(count) g_string_append(args,", ");g_string_append(args,"...");}
 if(valid && !count && !clang_isFunctionTypeVariadic(type)) {gchar *spelling=string(clang_getTypeSpelling(type));if(strstr(spelling,"(void)")) g_string_append(args,"void");g_free(spelling);}
 g_string_append_c(args,')');if(valid && clang_getCursorKind(cursor)==CXCursor_CXXMethod && clang_CXXMethod_isConst(cursor)) g_string_append(args," const");if(valid && args->len<=16384) return g_string_free(args,FALSE);g_string_free(args,TRUE);return g_strdup(fallback);
}
static enum CXChildVisitResult base_type(CXCursor cursor,CXCursor parent,CXClientData data) {(void)parent;if(clang_getCursorKind(cursor)==CXCursor_CXXBaseSpecifier) {GString *out=data;gchar *name=string(clang_getTypeSpelling(clang_getCursorType(cursor)));if(out->len) g_string_append_c(out,',');g_string_append(out,name);g_free(name);}return CXChildVisit_Continue;}
typedef struct {Parse *parse;const gchar *usr;guint line,column,offset;gboolean found;} CallLocation;
static enum CXChildVisitResult callee_location(CXCursor cursor,CXCursor parent,CXClientData data) {
 (void)parent;CallLocation *at=data;if(at->found) return CXChildVisit_Break;enum CXCursorKind k=clang_getCursorKind(cursor);
 if(k==CXCursor_DeclRefExpr || k==CXCursor_MemberRefExpr) {CXCursor target=clang_getCursorReferenced(cursor);gchar *usr=string(clang_getCursorUSR(target));if(!strcmp(usr,at->usr)) at->found=location(clang_getCursorLocation(cursor),at->parse->path,&at->line,&at->column,&at->offset);g_free(usr);if(at->found) return CXChildVisit_Break;}
 return CXChildVisit_Recurse;
}
typedef struct {Parse *parse;CXCursor function;guint line,column,end_line,end_column;gboolean found;} Body;
static enum CXChildVisitResult function_body(CXCursor cursor,CXCursor parent,CXClientData data) {
 (void)parent;Body *body=data;if(clang_getCursorKind(cursor)!=CXCursor_CompoundStmt) return CXChildVisit_Continue;
 CXSourceRange range=clang_getCursorExtent(cursor);body->found=location(clang_getRangeStart(range),body->parse->path,&body->line,&body->column,NULL) && location(clang_getRangeEnd(range),body->parse->path,&body->end_line,&body->end_column,NULL);return body->found ? CXChildVisit_Break:CXChildVisit_Continue;
}
static enum CXChildVisitResult visit(CXCursor cursor,CXCursor parent,CXClientData data) {
 (void)parent;Parse *p=data;if((p->cancel && g_cancellable_is_cancelled(p->cancel)) || ++p->visited>200000) {p->result->limited=TRUE;return CXChildVisit_Break;}
 guint line=0,column=0,offset=0;if(!location(clang_getCursorLocation(cursor),p->path,&line,&column,&offset)) return CXChildVisit_Continue;
 if(offset>p->length) return CXChildVisit_Continue;enum CXCursorKind ck=clang_getCursorKind(cursor);const gchar *k=kind(cursor);gchar *name=own(p,string(clang_getCursorSpelling(cursor)));
 if(p->symbols->len>=50000 || p->relations->len>=100000) {p->result->limited=TRUE;return CXChildVisit_Break;}
 if(k && *name && p->symbols->len<50000) {
  gchar *display=own(p,string(clang_getCursorDisplayName(cursor))),*signature=strchr(display,'(');InsightSymbol row={name,p->path,own(p,scope(cursor)),k,line,signature ? signature:"",own(p,string(clang_getTypeSpelling(clang_getCursorType(cursor)))),""};if(strchr("csu",*k)) {GString *bases=g_string_new(NULL);clang_visitChildren(cursor,base_type,bases);row.inheritance=own(p,g_string_free(bases,FALSE));}if(ck==CXCursor_TypedefDecl || ck==CXCursor_TypeAliasDecl) row.type=own(p,string(clang_getTypeSpelling(clang_getTypedefDeclUnderlyingType(cursor))));if(!strcmp(k,"f") || !strcmp(k,"p")) row.type=own(p,string(clang_getTypeSpelling(clang_getCursorResultType(cursor))));if(!strcmp(k,"f") || !strcmp(k,"p") || ck==CXCursor_MacroDefinition) row.signature=own(p,parameters(p,cursor,row.signature));guint flags=clang_getCursorLinkage(cursor)==CXLinkage_Internal || *k=='l' || *k=='z' ? INSIGHT_CLANG_INTERNAL:0;enum CX_CXXAccessSpecifier access=clang_getCXXAccessSpecifier(cursor);if(access==CX_CXXPublic) flags|=INSIGHT_CLANG_PUBLIC;else if(access==CX_CXXProtected) flags|=INSIGHT_CLANG_PROTECTED;else if(access==CX_CXXPrivate) flags|=INSIGHT_CLANG_PRIVATE;if(ck==CXCursor_CXXMethod && clang_CXXMethod_isVirtual(cursor)) flags|=INSIGHT_CLANG_VIRTUAL;if(ck==CXCursor_VarDecl && !clang_isCursorDefinition(cursor)) flags|=INSIGHT_CLANG_EXTERN;if(ck==CXCursor_MacroDefinition && clang_Cursor_isMacroFunctionLike(cursor)) flags|=INSIGHT_CLANG_FUNCTION_MACRO;g_hash_table_replace(p->result->attributes,insight_clang_attribute_key(&row),GUINT_TO_POINTER(flags));g_array_append_val(p->symbols,row);
  if(clang_isCursorDefinition(cursor)) {gchar *usr=own(p,string(clang_getCursorUSR(cursor)));if(*usr) {InsightSymbol definition={usr,p->path,flags&INSIGHT_CLANG_INTERNAL ? "internal":"","@clangdef",line,name,!strcmp(k,"f") ? row.signature:row.type,""};g_array_append_val(p->relations,definition);
    if(!strcmp(k,"f")) {Body body={p,cursor,0,0,0,0,FALSE};clang_visitChildren(cursor,function_body,&body);if(body.found) {gchar *range=own(p,g_strdup_printf("%u:%u:%u:%u",body.line,body.column,body.end_line,body.end_column));InsightSymbol span={usr,p->path,"","@clangbody",line,range,name,""};g_array_append_val(p->relations,span);}}}}
 }
 if(ck==CXCursor_CallExpr || ck==CXCursor_DeclRefExpr || ck==CXCursor_MemberRefExpr || ck==CXCursor_TypeRef || ck==CXCursor_TemplateRef || ck==CXCursor_NamespaceRef) {
  CXCursor target=clang_getCursorReferenced(cursor);if(!clang_Cursor_isNull(target)) {gchar *usr=own(p,string(clang_getCursorUSR(target))),*target_name=own(p,string(clang_getCursorSpelling(target)));guint end=offset;location(clang_getRangeEnd(clang_getCursorExtent(cursor)),p->path,NULL,NULL,&end);
   if(ck==CXCursor_CallExpr) {CallLocation at={p,usr,line,column,offset,FALSE};clang_visitChildren(cursor,callee_location,&at);if(at.found) {line=at.line;column=at.column;offset=at.offset;}end=offset+strlen(target_name);}
   if(*usr && *target_name && p->relations->len<100000) {
    relation(p,target_name,usr,ck==CXCursor_CallExpr ? "@call":"@clangref",line,column,offset,MIN(end,(guint)p->length));
    if(ck==CXCursor_CallExpr && p->owner) {gboolean virtual_call=clang_getCursorKind(target)==CXCursor_CXXMethod && clang_CXXMethod_isVirtual(target);relation(p,target_name,usr,virtual_call ? "@clangvirtual":"@clangcall",line,column,offset,MIN(end,(guint)p->length));InsightSymbol *call=&g_array_index(p->relations,InsightSymbol,p->relations->len-1);call->type=own(p,g_strdup(p->owner));CXCursor definition=clang_getCursorDefinition(target);CXFile target_file=NULL;if(!clang_Cursor_isNull(definition)) clang_getSpellingLocation(clang_getCursorLocation(definition),&target_file,NULL,NULL,NULL);call->inheritance=target_file ? own(p,string(clang_getFileName(target_file))):"";}
   }
  }
 }
 if((k && !strcmp(k,"f") && clang_isCursorDefinition(cursor)) || ck==CXCursor_LambdaExpr) {const gchar *previous=p->owner;p->owner=ck==CXCursor_LambdaExpr ? NULL:own(p,string(clang_getCursorUSR(cursor)));clang_visitChildren(cursor,visit,p);p->owner=previous;return CXChildVisit_Continue;}
 return CXChildVisit_Recurse;
}
static void include_dependency(Parse *p,const gchar *name) {
 if(p->relations->len>=100000) {p->result->limited=TRUE;return;}gchar *stamp=own(p,dependency_stamp(name));InsightSymbol row={own(p,g_strdup(name)),p->path,"","@clanginclude",1,stamp,"",""};g_array_append_val(p->relations,row);
}
static void inclusion(CXFile file,CXSourceLocation *stack,unsigned depth,CXClientData data) {(void)stack;(void)depth;Parse *p=data;gchar *name=string(clang_getFileName(file));if(strcmp(name,p->path)) include_dependency(p,name);g_free(name);}
static gboolean path_option(const gchar *s) {return !strcmp(s,"-I") || !strcmp(s,"-isystem") || !strcmp(s,"-iquote") || !strcmp(s,"-include") || !strcmp(s,"-imacros") || !strcmp(s,"-isysroot") || !strcmp(s,"--sysroot");}
static void argument(GPtrArray *args,const gchar *s) {g_ptr_array_add(args,g_strdup(s));}
static GPtrArray *arguments(const gchar *path,const gchar *root,const gchar *resources,gboolean *database) {
 GPtrArray *args=g_ptr_array_new_with_free_func(g_free);CXCompilationDatabase db=NULL;CXCompileCommands commands=NULL;CXCompilationDatabase_Error error;
 if(root) {gchar *manifest=g_build_filename(root,"compile_commands.json",NULL);if(g_file_test(manifest,G_FILE_TEST_IS_REGULAR)) db=clang_CompilationDatabase_fromDirectory(root,&error);g_free(manifest);if(!db) {gchar *build=g_build_filename(root,"build",NULL);manifest=g_build_filename(build,"compile_commands.json",NULL);if(g_file_test(manifest,G_FILE_TEST_IS_REGULAR)) db=clang_CompilationDatabase_fromDirectory(build,&error);g_free(manifest);g_free(build);}if(db) commands=clang_CompilationDatabase_getCompileCommands(db,path);}
 if(commands && clang_CompileCommands_getSize(commands)) {
  *database=TRUE;CXCompileCommand command=clang_CompileCommands_getCommand(commands,0);gchar *cwd=string(clang_CompileCommand_getDirectory(command));
  for(guint i=1;i<clang_CompileCommand_getNumArgs(command) && args->len<2048;i++) {gchar *s=string(clang_CompileCommand_getArg(command,i));
   if(path_option(s) || !strcmp(s,"-D") || !strcmp(s,"-U") || !strcmp(s,"-x") || !strcmp(s,"-target")) {if(i+1<clang_CompileCommand_getNumArgs(command)) {gchar *value=string(clang_CompileCommand_getArg(command,++i));argument(args,s);if(path_option(s)) {gchar *absolute=g_canonicalize_filename(value,cwd);argument(args,absolute);g_free(absolute);}else argument(args,value);g_free(value);}}
   else if(g_str_has_prefix(s,"-I") && s[2]) {gchar *absolute=g_canonicalize_filename(s+2,cwd),*joined=g_strconcat("-I",absolute,NULL);argument(args,joined);g_free(joined);g_free(absolute);}
   else if(g_str_has_prefix(s,"-isystem") || g_str_has_prefix(s,"-iquote") || g_str_has_prefix(s,"--sysroot=")) {gsize prefix=g_str_has_prefix(s,"-isystem") ? 8:g_str_has_prefix(s,"-iquote") ? 7:10;gchar *absolute=g_canonicalize_filename(s+prefix,cwd);argument(args,prefix==8 ? "-isystem":prefix==7 ? "-iquote":"--sysroot");argument(args,absolute);g_free(absolute);}
   else if(g_str_has_prefix(s,"-O") || (g_str_has_prefix(s,"-m") && !g_str_has_prefix(s,"-mllvm")) || !strcmp(s,"-pthread") || !strcmp(s,"-ffreestanding") || !strcmp(s,"-fno-builtin") || !strcmp(s,"-fPIC") || !strcmp(s,"-fpic") || !strcmp(s,"-fPIE") || !strcmp(s,"-fpie")) argument(args,s);
   else if(g_str_has_prefix(s,"-D") || g_str_has_prefix(s,"-U") || g_str_has_prefix(s,"-std=") || g_str_has_prefix(s,"--target=") || !strcmp(s,"-m32") || !strcmp(s,"-m64") || !strcmp(s,"-fms-extensions") || !strcmp(s,"-fms-compatibility") || !strcmp(s,"-fno-exceptions") || !strcmp(s,"-fexceptions") || !strcmp(s,"-fno-rtti") || !strcmp(s,"-frtti")) argument(args,s);
   g_free(s);
  }g_free(cwd);
 }
 if(commands) clang_CompileCommands_dispose(commands);if(db) clang_CompilationDatabase_dispose(db);
 if(!*database) {const gchar *dot=strrchr(path,'.');argument(args,"-x");argument(args,dot && (!strcmp(dot,".c") || !strcmp(dot,".h")) ? "c":"c++");}
 if(root) {argument(args,"-I");argument(args,root);}if(resources && g_file_test(resources,G_FILE_TEST_IS_DIR)) {argument(args,"-resource-dir");argument(args,resources);}
 argument(args,"-fsyntax-only");argument(args,"-fno-modules");argument(args,"-fno-implicit-modules");return args;
}
gboolean insight_clang_available(void) {return TRUE;}
InsightClangResult *insight_clang_parse(const gchar *path,const gchar *text,const gchar *root,const gchar *resources,GHashTable *overlays,GCancellable *cancel,GError **error) {
 if(!path || !text || !insight_clang_source(path) || strlen(text)>2u*1024u*1024u || !g_utf8_validate(text,-1,NULL)) {g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Clang requires a C/C++ UTF-8 source up to 2 MiB");return NULL;}
 if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) return NULL;
 InsightClangResult *r=g_new0(InsightClangResult,1);r->symbols=insight_index_new();r->relations=insight_index_new();r->tokens=g_array_new(FALSE,FALSE,sizeof(InsightClangToken));r->attributes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);r->digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
 GPtrArray *args=arguments(path,root,resources,&r->compilation_database);GArray *unsaved=g_array_new(FALSE,FALSE,sizeof(struct CXUnsavedFile));gsize bytes=0;
 if(overlays) {GHashTableIter it;gpointer key,value;g_hash_table_iter_init(&it,overlays);while(g_hash_table_iter_next(&it,&key,&value)) if(value && strcmp(key,path) && unsaved->len<256 && bytes+strlen(value)<=32u*1024u*1024u) {struct CXUnsavedFile file={key,value,strlen(value)};g_array_append_val(unsaved,file);bytes+=strlen(value);}}
 struct CXUnsavedFile main_file={path,text,strlen(text)};g_array_append_val(unsaved,main_file);CXIndex index=clang_createIndex(1,0);CXTranslationUnit tu=NULL;
 enum CXErrorCode code=clang_parseTranslationUnit2(index,path,(const gchar *const *)args->pdata,args->len,(struct CXUnsavedFile *)unsaved->data,unsaved->len,CXTranslationUnit_DetailedPreprocessingRecord|CXTranslationUnit_KeepGoing,&tu);g_array_unref(unsaved);g_ptr_array_unref(args);
 if(code!=CXError_Success || !tu) {clang_disposeIndex(index);insight_clang_result_free(r);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Clang could not parse the translation unit");return NULL;}
 GString *diagnostics=g_string_new(NULL);for(guint i=0;i<clang_getNumDiagnostics(tu) && diagnostics->len<65536;i++) {CXDiagnostic d=clang_getDiagnostic(tu,i);if(clang_getDiagnosticSeverity(d)>=CXDiagnostic_Error) r->errors=TRUE;gchar *message=string(clang_formatDiagnostic(d,clang_defaultDiagnosticDisplayOptions()));g_string_append_printf(diagnostics,"%s\n",message);g_free(message);clang_disposeDiagnostic(d);}r->diagnostics=g_string_free(diagnostics,FALSE);
 Parse p={r,path,text,strlen(text),g_array_new(FALSE,FALSE,sizeof(InsightSymbol)),g_array_new(FALSE,FALSE,sizeof(InsightSymbol)),g_ptr_array_new_with_free_func(g_free),cancel,0};clang_visitChildren(clang_getTranslationUnitCursor(tu),visit,&p);
 clang_getInclusions(tu,inclusion,&p);if(root) {gchar *manifest=g_build_filename(root,"compile_commands.json",NULL);include_dependency(&p,manifest);g_free(manifest);manifest=g_build_filename(root,"build","compile_commands.json",NULL);include_dependency(&p,manifest);g_free(manifest);}
 CXFile file=clang_getFile(tu,path);if(file && !(cancel && g_cancellable_is_cancelled(cancel))) {CXSourceRange range=clang_getRange(clang_getLocationForOffset(tu,file,0),clang_getLocationForOffset(tu,file,p.length));CXToken *tokens=NULL;unsigned n=0;clang_tokenize(tu,range,&tokens,&n);if(n<=200000) {CXCursor *cursors=g_new0(CXCursor,n);clang_annotateTokens(tu,tokens,n,cursors);for(guint i=0;i<n;i++) if(clang_getTokenKind(tokens[i])==CXToken_Identifier) {CXCursor cursor=clang_getCursorReferenced(cursors[i]);if(clang_Cursor_isNull(cursor)) cursor=cursors[i];if(!kind(cursor)) continue;guint begin=0,end=0;CXSourceRange extent=clang_getTokenExtent(tu,tokens[i]);if(location(clang_getRangeStart(extent),path,NULL,NULL,&begin) && location(clang_getRangeEnd(extent),path,NULL,NULL,&end) && begin<end && end<=p.length) {InsightClangToken token={begin,end,role(cursor)};g_array_append_val(r->tokens,token);}}g_free(cursors);}else r->limited=TRUE;clang_disposeTokens(tu,tokens,n);}
 InsightSymbol coverage={"@file",path,"","@file",1,"","",r->digest};g_array_append_val(p.relations,coverage);InsightSymbol clang_coverage={"@clangfile",path,"","@clangfile",1,"",r->errors ? "partial":"complete",r->digest};g_array_append_val(p.relations,clang_coverage);
 if(!insight_index_update(r->symbols,path,(InsightSymbol *)p.symbols->data,p.symbols->len) || !insight_index_update(r->relations,path,(InsightSymbol *)p.relations->data,p.relations->len)) r->limited=TRUE;
 g_ptr_array_unref(p.owned);g_array_unref(p.symbols);g_array_unref(p.relations);clang_disposeTranslationUnit(tu);clang_disposeIndex(index);
 if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) {insight_clang_result_free(r);return NULL;}return r;
}
#else
gboolean insight_clang_available(void) {return FALSE;}
InsightClangResult *insight_clang_parse(const gchar *path,const gchar *text,const gchar *root,const gchar *resources,GHashTable *overlays,GCancellable *cancel,GError **error) {(void)path;(void)text;(void)root;(void)resources;(void)overlays;(void)cancel;g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"This build has no bundled Clang backend");return NULL;}
#endif
