/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "../desktop/clang_backend.h"
#ifdef HAVE_CLANG_CPP
#include "../desktop/clang_calls.h"
#endif
#include "../desktop/call_paths.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *load_source(const gchar *path,GCancellable *cancel,gpointer text) {(void)path;(void)cancel;return g_strdup(text);}
static gchar *json_escape(const gchar *path) {GString *out=g_string_new(NULL);for(const guchar *p=(const guchar *)path;*p;p++) {if(*p=='"' || *p=='\\') {g_string_append_c(out,'\\');g_string_append_c(out,*p);}else if(*p<32) g_string_append_printf(out,"\\u%04x",*p);else g_string_append_c(out,*p);}return g_string_free(out,FALSE);}
static void header_commands(void) {
 GError *error=NULL;gchar *root=g_dir_make_tmp("clang-header-flags-XXXXXX",&error);g_assert_no_error(error);
 gchar *src=g_build_filename(root,"src",NULL),*inc=g_build_filename(root,"include",NULL),*sdk=g_build_filename(root,"sdk",NULL),*build=g_build_filename(root,"build",NULL);
 g_assert_cmpint(g_mkdir(src,0700),==,0);g_assert_cmpint(g_mkdir(inc,0700),==,0);g_assert_cmpint(g_mkdir(sdk,0700),==,0);g_assert_cmpint(g_mkdir(build,0700),==,0);
 gchar *header=g_build_filename(inc,"widget.h",NULL),*cpp=g_build_filename(src,"widget.cpp",NULL),*config=g_build_filename(sdk,"sdk_config.h",NULL),*db=g_build_filename(root,"compile_commands.json",NULL);
 gchar *root_json=json_escape(root),*build_json=json_escape(build),*common_json=NULL,*a_json=NULL,*z_json=NULL;
 const gchar *text="#include <sdk_config.h>\n#if !defined(FROM_CPP) || !defined(__cplusplus)\n#error missing C++ project flags\n#endif\nclass Widget {public:int method(int value){return value+SDK_VALUE;}};\ninline int via(Widget &w){return w.method(1);}\n";
 g_assert_true(g_file_set_contents(header,text,-1,NULL));g_assert_true(g_file_set_contents(cpp,"#include \"widget.h\"\n",-1,NULL));g_assert_true(g_file_set_contents(config,"#define SDK_VALUE 7\n",-1,NULL));
 gchar *manifest=g_strdup_printf("[{\"directory\":\"%s\",\"file\":\"../src/widget.cpp\",\"arguments\":[\"clang++\",\"-std=c++17\",\"-DFROM_CPP=1\",\"-I../sdk\",\"-c\",\"../src/widget.cpp\",\"-o\",\"widget.o\"]}]",build_json);
 g_assert_true(g_file_set_contents(db,manifest,-1,NULL));g_free(manifest);
 InsightClangResult *r=insight_clang_parse(header,text,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);if(r->errors) g_printerr("%s",r->diagnostics);g_assert_false(r->errors);g_assert_true(r->compilation_database);g_assert_true(r->inferred_command);g_assert_cmpstr(r->parse_language,==,"c++");g_assert_false(r->ambiguous_command);g_assert_cmpstr(r->command_source,==,cpp);
 const InsightSymbol *found[8];g_assert_cmpuint(insight_index_find(r->symbols,"Widget",TRUE,found,8),==,1);g_assert_cmpuint(insight_index_query(r->relations,"method",TRUE,NULL,"@clangcall",header,0,found,8),==,1);g_assert_true(insight_clang_dependencies_fresh(r->relations,header));
 g_assert_true(g_file_set_contents(config,"#define SDK_VALUE 123\n",-1,NULL));g_assert_false(insight_clang_dependencies_fresh(r->relations,header));insight_clang_result_free(r);
 gchar *complete=g_strconcat(text,"inline void hints(){Widget w;w.met}\n",NULL);guint offset=(guint)(strstr(complete,"w.met}")-complete+5);GPtrArray *items=insight_clang_complete(header,complete,offset,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(items);gboolean method=FALSE;
 for(guint i=0;i<items->len;i++) {InsightClangCompletion *item=g_ptr_array_index(items,i);if(!strcmp(item->name,"method")) method=TRUE;}g_assert_true(method);g_ptr_array_unref(items);g_free(complete);
 /* Replacing the database invalidates saved header references. */
 r=insight_clang_parse(header,text,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);
 gchar *plain=g_build_filename(root,"plain.c",NULL),*other=g_build_filename(root,"other.cpp",NULL),*common=g_build_filename(root,"common.h",NULL),*hpp=g_build_filename(root,"common.hpp",NULL);
 manifest=g_strdup_printf("[{\"directory\":\"%s\",\"file\":\"plain.c\",\"arguments\":[\"cc\",\"-DFROM_C=1\",\"-c\",\"plain.c\"]},{\"directory\":\"%s\",\"file\":\"other.cpp\",\"arguments\":[\"clang++\",\"-DFROM_CPP=1\",\"-c\",\"other.cpp\"]}]",root_json,root_json);
 g_assert_true(g_file_set_contents(db,manifest,-1,NULL));g_free(manifest);g_assert_false(insight_clang_dependencies_fresh(r->relations,header));insight_clang_result_free(r);
 common_json=json_escape(common);
 const gchar *c_source="#if defined(FROM_C) || defined(FROM_CPP) || defined(__cplusplus)\n#error ambiguous flags must not be selected\n#endif\nint common;\n";
 g_assert_true(g_file_set_contents(common,c_source,-1,NULL));r=insight_clang_parse(common,c_source,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);g_assert_false(r->compilation_database);g_assert_true(r->ambiguous_command);g_assert_cmpstr(r->parse_language,==,"c");g_assert_null(r->command_source);g_assert_false(insight_clang_result_usable(r));const InsightSymbol *coverage=NULL;g_assert_cmpuint(insight_index_query(r->relations,"@clangfile",TRUE,NULL,"@clangfile",common,0,&coverage,1),==,1);g_assert_cmpstr(coverage->type,==,"partial");InsightIndex *symbols=insight_index_new(),*relations=insight_index_new();g_assert_false(insight_clang_import(r,common,symbols,relations));g_assert_cmpuint(insight_index_count(symbols),==,0);insight_index_free(symbols);insight_index_free(relations);items=insight_clang_complete(common,c_source,(guint)strlen(c_source),root,NULL,NULL,NULL,&error);g_assert_null(items);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED);g_clear_error(&error);insight_clang_result_free(r);
 const gchar *cpp_source="#ifndef FROM_CPP\n#error missing inferred C++ flags\n#endif\nclass Common {};\n";g_assert_true(g_file_set_contents(hpp,cpp_source,-1,NULL));r=insight_clang_parse(hpp,cpp_source,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);g_assert_true(r->inferred_command);g_assert_cmpstr(r->command_source,==,other);insight_clang_result_free(r);
 /* C++ driver semantics also apply to an explicit .h command without -x. */
 manifest=g_strdup_printf("[{\"directory\":\"%s\",\"file\":\"%s\",\"arguments\":[\"clang++\",\"-DFROM_CPP=1\",\"-c\",\"%s\"]}]",root_json,common_json,common_json);g_assert_true(g_file_set_contents(db,manifest,-1,NULL));g_free(manifest);
 r=insight_clang_parse(common,cpp_source,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);g_assert_true(r->compilation_database);g_assert_false(r->inferred_command);g_assert_cmpstr(r->command_source,==,common);insight_clang_result_free(r);
 /* An explicit C language overrides the C++ driver. */
 manifest=g_strdup_printf("[{\"directory\":\"%s\",\"file\":\"%s\",\"arguments\":[\"clang++\",\"-x\",\"c\",\"-c\",\"%s\"]}]",root_json,common_json,common_json);g_assert_true(g_file_set_contents(db,manifest,-1,NULL));g_free(manifest);
 r=insight_clang_parse(common,c_source,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);g_assert_true(r->compilation_database);g_assert_false(r->inferred_command);insight_clang_result_free(r);
 /* Include-directory names do not masquerade as C++ driver names. */
 manifest=g_strdup_printf("[{\"directory\":\"%s\",\"file\":\"%s\",\"arguments\":[\"cc\",\"-I\",\"c++\",\"-c\",\"%s\"]}]",root_json,common_json,common_json);g_assert_true(g_file_set_contents(db,manifest,-1,NULL));g_free(manifest);r=insight_clang_parse(common,c_source,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);g_assert_cmpstr(r->parse_language,==,"c");insight_clang_result_free(r);
 /* Same-language ties do not depend on JSON command order. */
 gchar *z=g_build_filename(root,"z.cpp",NULL),*a=g_build_filename(root,"a.cpp",NULL);
 a_json=json_escape(a);z_json=json_escape(z);
 for(guint reverse=0;reverse<2;reverse++) {manifest=g_strdup_printf("[{\"directory\":\"%s\",\"file\":\"%s\",\"arguments\":[\"clang++\",\"-DFROM_CPP=1\",\"-c\",\"%s\"]},{\"directory\":\"%s\",\"file\":\"%s\",\"arguments\":[\"clang++\",\"-DFROM_CPP=1\",\"-c\",\"%s\"]}]",root_json,reverse ? a_json:z_json,reverse ? a_json:z_json,root_json,reverse ? z_json:a_json,reverse ? z_json:a_json);g_assert_true(g_file_set_contents(db,manifest,-1,NULL));g_free(manifest);r=insight_clang_parse(hpp,cpp_source,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);g_assert_cmpstr(r->command_source,==,a);insight_clang_result_free(r);}
 /* Malformed/relative working directories cannot crash command selection. */
 g_assert_true(g_file_set_contents(db,"[{\"directory\":\".\",\"file\":\"relative.cpp\",\"arguments\":[\"clang++\",\"-c\",\"relative.cpp\"]}]",-1,NULL));r=insight_clang_parse(common,"int relative_directory;\n",root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);insight_clang_result_free(r);
 g_free(root_json);g_free(build_json);g_free(common_json);g_free(a_json);g_free(z_json);g_free(z);g_free(a);g_remove(common);g_remove(hpp);g_free(common);g_free(hpp);g_free(plain);g_free(other);g_remove(db);g_remove(header);g_remove(cpp);g_remove(config);g_rmdir(src);g_rmdir(inc);g_rmdir(sdk);g_rmdir(build);g_rmdir(root);g_free(db);g_free(header);g_free(cpp);g_free(config);g_free(src);g_free(inc);g_free(sdk);g_free(build);g_free(root);
 g_print("Clang headers: companion flags, relative includes, inherited C++, ambiguous C/C++, explicit -x, deterministic ties, completion and dependency invalidation passed\n");
}
/* Same-spelling locals must remain distinct; member ranges cover the token,
 * not the containing object expression, and columns count UTF-8 characters. */
static void expression_references(void) {
 gchar *root=g_dir_make_tmp("clang-expression-refs-XXXXXX",NULL),*path=g_build_filename(root,"refs.cpp",NULL);
 const gchar *text="struct Obj {int field;};\nint take(int argument){return argument;}\nint sample(){Obj o; int value=1; /* русский */ o.field=value; {int value=2; o.field+=value;} return take(o.field)+value;}\n";
 GError *error=NULL;InsightClangResult *r=insight_clang_parse(path,text,root,NULL,NULL,NULL,&error);
 g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);
#ifdef HAVE_CLANG_CPP
 g_assert_true(r->internal_calls);
#endif
 const InsightSymbol *found[16];
 g_assert_cmpuint(insight_index_query(r->relations,"field",TRUE,NULL,"@clangref",path,0,found,16),==,3);
 for(guint i=0;i<3;i++) {
  gchar **range=g_strsplit(found[i]->signature,":",3);guint begin=(guint)g_ascii_strtoull(range[0],NULL,10),end=(guint)g_ascii_strtoull(range[1],NULL,10),column=(guint)g_ascii_strtoull(range[2],NULL,10);
  g_assert_cmpuint(end-begin,==,5);g_assert_cmpmem(text+begin,5,"field",5);
  const gchar *line=text+begin;while(line>text && line[-1]!='\n') line--;
  g_assert_cmpuint(column,==,(guint)g_utf8_strlen(line,text+begin-line)+1);g_assert_nonnull(strstr(found[i]->type,"o.field"));g_strfreev(range);
 }
 g_assert_cmpuint(insight_index_query(r->relations,"value",TRUE,NULL,"@clangref",path,0,found,16),==,3);
 guint same=0;for(guint i=0;i<3;i++) for(guint j=i+1;j<3;j++) if(!strcmp(found[i]->scope,found[j]->scope)) same++;
 // Two uses of the outer declaration and one use of the shadowing declaration.
 g_assert_cmpuint(same,==,1);
 g_assert_cmpuint(insight_index_query(r->relations,"take",TRUE,NULL,"@clangref",path,0,found,16),==,1);
 insight_clang_result_free(r);g_rmdir(root);g_free(path);g_free(root);
 g_print("Clang expression references: exact member tokens, shadowed locals, UTF-8 columns and no duplicate occurrences passed\n");
}
static void assert_destination(InsightClangResult *r,const gchar *source,const gchar *identity,const gchar *path,guint line,const gchar *kind) {
 const InsightSymbol *found[8];g_assert_cmpuint(insight_index_query(r->relations,identity,TRUE,NULL,"@clangtarget",source,0,found,8),==,1);
 g_assert_cmpstr(found[0]->scope,==,path);g_assert_cmpuint(found[0]->line,==,line);g_assert_cmpstr(found[0]->inheritance,==,kind);
 gchar **position=g_strsplit(found[0]->signature,":",2);g_assert_cmpuint(g_ascii_strtoull(position[0],NULL,10),==,line);g_assert_cmpuint(g_ascii_strtoull(position[1],NULL,10),>,0);g_strfreev(position);
}
static void expression_destinations(void) {
 gchar *root=g_dir_make_tmp("clang-expression-targets-XXXXXX",NULL),*path=g_build_filename(root,"targets.cpp",NULL),*header=g_build_filename(root,"targets.h",NULL);
 const gchar *api="int choose(int);\nint choose(double);\nextern int shared;\nstruct Owner {int field; int method(int);};\nint external_only(int);\n";
 const gchar *text="#include \"targets.h\"\nint choose(int x){return x;}\nint choose(double x){return (int)x;}\nint shared=7;\nint Owner::method(int x){return field+x;}\nint main(){Owner o; return choose(1)+choose(1.0)+o.method(shared)+external_only(2);}\n";
 g_assert_true(g_file_set_contents(header,api,-1,NULL));gchar *changed=g_strconcat("\n\n",api,NULL);GHashTable *overlays=g_hash_table_new(g_str_hash,g_str_equal);g_hash_table_insert(overlays,header,changed);
 GError *error=NULL;InsightClangResult *r=insight_clang_parse(path,text,root,NULL,overlays,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);
#ifdef HAVE_CLANG_CPP
 g_assert_true(r->internal_calls);
#endif
 const InsightSymbol *found[8];g_assert_cmpuint(insight_index_query(r->relations,"choose",TRUE,NULL,"@clangref",path,0,found,8),==,2);
 g_assert_cmpstr(found[0]->scope,!=,found[1]->scope);
 for(guint i=0;i<2;i++) {const InsightSymbol *definition=NULL;g_assert_cmpuint(insight_index_query(r->relations,found[i]->scope,TRUE,NULL,"@clangdef",path,0,&definition,1),==,1);g_assert_true(definition->line==2 || definition->line==3);assert_destination(r,path,found[i]->scope,path,(guint)definition->line,"f");}
 g_assert_cmpuint(insight_index_query(r->relations,"method",TRUE,NULL,"@clangref",path,0,found,8),==,1);assert_destination(r,path,found[0]->scope,path,5,"f");
 g_assert_cmpuint(insight_index_query(r->relations,"shared",TRUE,NULL,"@clangref",path,0,found,8),==,1);assert_destination(r,path,found[0]->scope,path,4,"v");
 g_assert_cmpuint(insight_index_query(r->relations,"field",TRUE,NULL,"@clangref",path,0,found,8),==,1);assert_destination(r,path,found[0]->scope,header,6,"m");
 g_assert_cmpuint(insight_index_query(r->relations,"external_only",TRUE,NULL,"@clangref",path,0,found,8),==,1);assert_destination(r,path,found[0]->scope,header,7,"p");
#ifdef HAVE_CLANG_CPP
 // Start without C frontend rows: these destinations must come from the
 // direct C++ API, not silently pass by retaining libclang's fallback.
 GArray *rows=g_array_new(FALSE,FALSE,sizeof(InsightSymbol));GPtrArray *owned=g_ptr_array_new_with_free_func(g_free);
 const gchar *args[]={"-xc++","-std=c++17","-I",root};
 g_assert_true(insight_clang_cpp_calls(path,text,args,G_N_ELEMENTS(args),overlays,rows,owned,NULL));
 InsightClangResult direct={0};direct.relations=insight_index_new();g_assert_true(insight_index_update(direct.relations,path,(const InsightSymbol *)rows->data,rows->len));
 g_assert_cmpuint(insight_index_query(direct.relations,"choose",TRUE,NULL,"@clangref",path,0,found,8),==,2);
 for(guint i=0;i<2;i++) {const InsightSymbol *definition=NULL;g_assert_cmpuint(insight_index_query(r->relations,found[i]->scope,TRUE,NULL,"@clangdef",path,0,&definition,1),==,1);assert_destination(&direct,path,found[i]->scope,path,(guint)definition->line,"f");}
 g_assert_cmpuint(insight_index_query(direct.relations,"method",TRUE,NULL,"@clangref",path,0,found,8),==,1);assert_destination(&direct,path,found[0]->scope,path,5,"f");
 g_assert_cmpuint(insight_index_query(direct.relations,"shared",TRUE,NULL,"@clangref",path,0,found,8),==,1);assert_destination(&direct,path,found[0]->scope,path,4,"v");
 g_assert_cmpuint(insight_index_query(direct.relations,"field",TRUE,NULL,"@clangref",path,0,found,8),==,1);assert_destination(&direct,path,found[0]->scope,header,6,"m");
 g_assert_cmpuint(insight_index_query(direct.relations,"external_only",TRUE,NULL,"@clangref",path,0,found,8),==,1);assert_destination(&direct,path,found[0]->scope,header,7,"p");
 GBytes *before=g_bytes_new(rows->data,rows->len*sizeof(InsightSymbol));guint old_rows=rows->len,old_owned=owned->len;
 GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);
 g_assert_false(insight_clang_cpp_calls(path,text,args,G_N_ELEMENTS(args),overlays,rows,owned,cancel));g_object_unref(cancel);
 g_assert_false(insight_clang_cpp_calls(path,"#error deliberately invalid source\n",args,G_N_ELEMENTS(args),overlays,rows,owned,NULL));
 g_assert_cmpuint(rows->len,==,old_rows);g_assert_cmpuint(owned->len,==,old_owned);g_assert_cmpmem(rows->data,rows->len*sizeof(InsightSymbol),g_bytes_get_data(before,NULL),g_bytes_get_size(before));
 g_bytes_unref(before);g_array_unref(rows);g_ptr_array_unref(owned);insight_index_free(direct.relations);
#endif
 gchar *disk=NULL;g_assert_true(g_file_get_contents(header,&disk,NULL,NULL));g_assert_cmpstr(disk,==,api);g_free(disk);
 insight_clang_result_free(r);g_hash_table_unref(overlays);g_free(changed);g_remove(header);g_rmdir(root);g_free(path);g_free(header);g_free(root);
 g_print("Clang destinations: exact overload definitions, out-of-line methods, extern definitions, unsaved-header fields and declaration-only fallback passed\n");
}
int main(void) {
 if(!insight_clang_available()) return 77;
 header_commands();
 expression_references();
 expression_destinations();
 gchar *root=g_dir_make_tmp("insight-clang-XXXXXX",NULL),*path=g_build_filename(root,"test.cpp",NULL),*header=g_build_filename(root,"api.h",NULL);
 const gchar *text="#include \"api.h\"\nint pick(int x){return x;}\nint pick(double x){return 2;}\nint main(){ Box b; int v=pick(1); return b.run(v); }\n";
 g_assert_true(g_file_set_contents(header,"struct Box { int run(int x){return x;} };\n",-1,NULL));g_assert_true(g_file_set_contents(path,text,-1,NULL));
 GError *error=NULL;InsightClangResult *r=insight_clang_parse(path,text,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);g_assert_cmpuint(r->tokens->len,>,5);
#ifdef HAVE_CLANG_CPP
 g_assert_true(r->internal_calls);
#endif
 const InsightSymbol *matches[8];g_assert_cmpuint(insight_index_query(r->symbols,"pick",TRUE,NULL,"f",path,0,matches,8),==,2);for(guint i=0;i<2;i++) if(matches[i]->line==2) g_assert_cmpstr(matches[i]->signature,==,"(int x)");
 g_assert_cmpuint(insight_index_query(r->relations,"pick",TRUE,NULL,"@call",path,0,matches,8),==,1);gchar **positions=g_strsplit(matches[0]->signature,":",3);guint start=(guint)g_ascii_strtoull(positions[0],NULL,10),end=(guint)g_ascii_strtoull(positions[1],NULL,10);g_assert_cmpuint(end-start,==,4);g_assert_cmpmem(text+start,4,"pick",4);g_strfreev(positions);gchar *called_usr=g_strdup(matches[0]->scope);g_assert_nonnull(strstr(called_usr,"pick"));g_assert_cmpuint(insight_index_query(r->relations,called_usr,TRUE,NULL,"@clangdef",path,0,matches,8),==,1);g_assert_cmpuint(matches[0]->line,==,2);
 g_assert_cmpuint(insight_index_query(r->relations,"run",TRUE,NULL,"@call",path,0,matches,8),==,1);g_assert_nonnull(strstr(matches[0]->scope,"Box"));g_assert_true(insight_clang_dependencies_fresh(r->relations,path));
 g_assert_cmpuint(insight_index_query(r->relations,"",FALSE,NULL,"@clangtarget",path,0,matches,8),>,0);gboolean header_target=FALSE;for(gsize n=0;n<insight_index_count(r->relations);n++) {const InsightSymbol *target=insight_index_at(r->relations,n);if(!strcmp(target->kind,"@clangtarget") && !strcmp(target->type,"run") && !strcmp(target->scope,header)) header_target=TRUE;}g_assert_true(header_target);
 const gchar *completion="struct Base {int inherited; private: int hidden;};\nstruct Derived: Base {int own;int run(int count);double run(double count);};\nvoid entry(){Derived d; d.}\n";guint complete_at=(guint)(strstr(completion,"d.}")-completion+2);GPtrArray *candidates=insight_clang_complete(path,completion,complete_at,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(candidates);guint inherited=0,own=0,overloads=0;for(guint n=0;n<candidates->len;n++) {InsightClangCompletion *item=g_ptr_array_index(candidates,n);g_assert_cmpstr(item->name,!=,"hidden");if(!strcmp(item->name,"inherited")) inherited++;if(!strcmp(item->name,"own")) own++;if(!strcmp(item->name,"run")) {overloads++;g_assert_true(item->callable);g_assert_cmpuint(item->parameters->len,==,1);g_assert_nonnull(strstr(g_ptr_array_index(item->parameters,0),"count"));}}g_assert_cmpuint(inherited,==,1);g_assert_cmpuint(own,==,1);g_assert_cmpuint(overloads,==,2);g_ptr_array_unref(candidates);
 gchar *identity=insight_clang_symbol_at(r->relations,path,start,4,"pick");g_assert_cmpstr(identity,==,called_usr);g_free(identity);
 GPtrArray *paths=g_ptr_array_new();g_ptr_array_add(paths,path);InsightCallEndpoint from={"main",path,4},to={"pick",path,2};InsightCallPaths *graph=insight_call_paths_find_semantic(r->symbols,r->relations,paths,&from,&to,FALSE,load_source,(gpointer)text,NULL,&error);g_assert_no_error(error);g_assert_nonnull(graph);g_assert_cmpuint(graph->paths->len,==,1);g_assert_cmpuint(graph->semantic_functions,>,0);g_assert_cmpuint(graph->lexical_functions,==,0);insight_call_paths_free(graph);
 to.line=3;graph=insight_call_paths_find_semantic(r->symbols,r->relations,paths,&from,&to,FALSE,load_source,(gpointer)text,NULL,&error);g_assert_no_error(error);g_assert_cmpuint(graph->paths->len,==,0);insight_call_paths_free(graph);g_ptr_array_unref(paths);

 g_assert_true(g_file_set_contents(header,"struct Box { int run(int x){return x+100;} };\n",-1,NULL));g_assert_false(insight_clang_dependencies_fresh(r->relations,path));insight_clang_result_free(r);
 GHashTable *overlays=g_hash_table_new(g_str_hash,g_str_equal);g_hash_table_insert(overlays,header,"struct Box { int missing; };\n");r=insight_clang_parse(path,text,root,NULL,overlays,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_true(r->errors);g_assert_nonnull(strstr(r->diagnostics,"run"));insight_clang_result_free(r);g_hash_table_unref(overlays);
 gchar *db=g_build_filename(root,"compile_commands.json",NULL),*source=g_build_filename(root,"flags.c",NULL);gchar *manifest=g_strdup_printf("[{\"directory\":\"%s\",\"file\":\"%s\",\"arguments\":[\"cc\",\"-DFEATURE=1\",\"-O2\",\"-c\",\"%s\"]}]",root,source,source);g_assert_true(g_file_set_contents(db,manifest,-1,NULL));const gchar *conditional="#if defined(FEATURE) && defined(__OPTIMIZE__)\nint enabled(void){return 1;}\n#else\nint disabled(void){return 0;}\n#endif\n";g_assert_true(g_file_set_contents(source,conditional,-1,NULL));r=insight_clang_parse(source,conditional,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_true(r->compilation_database);g_assert_cmpuint(insight_index_find(r->symbols,"enabled",TRUE,matches,8),==,1);g_assert_cmpuint(insight_index_find(r->symbols,"disabled",TRUE,matches,8),==,0);insight_clang_result_free(r);
 const gchar *pointer="int target(){return 1;}\nint main(){int (*fp)()=target;return fp();}\n";r=insight_clang_parse(path,pointer,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_cmpuint(insight_index_query(r->relations,"fp",TRUE,NULL,"@clangcall",path,0,matches,8),==,0);insight_clang_result_free(r);
 const gchar *lambda="int sink(){return 1;}\nint main(){auto deferred=[](){sink();};return 0;}\n";r=insight_clang_parse(path,lambda,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);paths=g_ptr_array_new();g_ptr_array_add(paths,path);from=(InsightCallEndpoint){"main",path,2};to=(InsightCallEndpoint){"sink",path,1};graph=insight_call_paths_find_semantic(r->symbols,r->relations,paths,&from,&to,FALSE,load_source,(gpointer)lambda,NULL,&error);g_assert_no_error(error);g_assert_cmpuint(graph->paths->len,==,0);g_assert_cmpuint(graph->semantic_functions,>,0);insight_call_paths_free(graph);insight_clang_result_free(r);
#ifdef HAVE_CLANG_CPP
 const gchar *capture="int sink(){return 1;}\nint main(){auto deferred=[value=sink()](){return sink()+value;};return 0;}\n";
 r=insight_clang_parse(path,capture,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);g_assert_true(r->internal_calls);
 g_assert_cmpuint(insight_index_query(r->relations,"sink",TRUE,NULL,"@clangcall",path,0,matches,8),==,1);
 gchar **capture_range=g_strsplit(matches[0]->signature,":",3);g_assert_cmpuint(g_ascii_strtoull(capture_range[0],NULL,10),==,(guint)(strstr(capture,"=sink()")-capture+1));g_strfreev(capture_range);
 g_assert_cmpuint(insight_index_query(r->relations,"sink",TRUE,NULL,"@clangref",path,0,matches,8),==,2);
 to=(InsightCallEndpoint){"sink",path,1};graph=insight_call_paths_find_semantic(r->symbols,r->relations,paths,&from,&to,FALSE,load_source,(gpointer)capture,NULL,&error);g_assert_no_error(error);g_assert_cmpuint(graph->paths->len,==,1);insight_call_paths_free(graph);insight_clang_result_free(r);
#endif
 const gchar *virtual_source="struct V{virtual int run(){return 1;}};\nint main(){V v;return v.run();}\n";r=insight_clang_parse(path,virtual_source,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_cmpuint(insight_index_find(r->symbols,"run",TRUE,matches,8),==,1);g_assert_true((insight_clang_attributes(r,matches[0])&INSIGHT_CLANG_VIRTUAL)!=0);to=(InsightCallEndpoint){"run",path,1};graph=insight_call_paths_find_semantic(r->symbols,r->relations,paths,&from,&to,FALSE,load_source,(gpointer)virtual_source,NULL,&error);g_assert_no_error(error);g_assert_cmpuint(graph->paths->len,==,0);insight_call_paths_free(graph);graph=insight_call_paths_find_semantic(r->symbols,r->relations,paths,&from,&to,TRUE,load_source,(gpointer)virtual_source,NULL,&error);g_assert_no_error(error);g_assert_cmpuint(graph->paths->len,==,1);g_assert_true(g_array_index(graph->edges,InsightCallEdge,0).uncertain);insight_call_paths_free(graph);g_ptr_array_unref(paths);insight_clang_result_free(r);
 const gchar *macro="#define APPLY(value, callback) callback(value)\ntypedef struct Cell { int value; } Alias;\nint pass(int x){return x;}\nint variadic(const char *format, ...){return 0;}\n";r=insight_clang_parse(source,macro,root,NULL,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);g_assert_cmpuint(insight_index_find(r->symbols,"APPLY",TRUE,matches,8),==,1);g_assert_cmpstr(matches[0]->signature,==,"(value, callback)");g_assert_cmpuint(insight_index_find(r->symbols,"Alias",TRUE,matches,8),==,1);g_assert_nonnull(strstr(matches[0]->type,"Cell"));g_assert_cmpuint(insight_index_find(r->symbols,"variadic",TRUE,matches,8),==,1);g_assert_cmpstr(matches[0]->signature,==,"(const char *format, ...)");insight_clang_result_free(r);
 const gchar *prefix=g_getenv("GEANY_INSIGHT_PREFIX");if(prefix) {
#ifdef G_OS_WIN32
 gchar *resources=g_build_filename(prefix,"data","insight","clang","resources",NULL);
#else
 gchar *resources=g_build_filename(prefix,"share","geany","insight","clang","resources",NULL);
#endif
 gchar *builtin=g_build_filename(resources,"include","stddef.h",NULL);g_assert_true(g_file_test(builtin,G_FILE_TEST_IS_REGULAR));const gchar *builtins="#include <stddef.h>\nsize_t count;\n";r=insight_clang_parse(source,builtins,root,resources,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_false(r->errors);g_assert_cmpuint(insight_index_find(r->symbols,"count",TRUE,matches,8),==,1);g_assert_cmpuint(insight_index_query(r->relations,builtin,TRUE,NULL,"@clanginclude",source,0,matches,8),==,1);insight_clang_result_free(r);g_free(builtin);g_free(resources);}
 GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);r=insight_clang_parse(path,text,root,NULL,NULL,cancel,&error);g_assert_null(r);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&error);g_object_unref(cancel);
 g_free(manifest);g_free(called_usr);g_remove(path);g_remove(header);g_remove(db);g_remove(source);g_rmdir(root);g_free(path);g_free(header);g_free(db);g_free(source);g_free(root);g_print("Clang: overload identity, member calls, semantic tokens, unsaved headers, dependency invalidation, compilation database and cancellation passed\n");return 0;
}
