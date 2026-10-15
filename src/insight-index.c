/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-index.h"
#include "tagmanager/tm_ctags.h"
#include "../insight/desktop/type_resolve.h"
#include "../insight/desktop/relation_query.h"
#include "../insight/desktop/source_mask.h"
#include <string.h>
gboolean insight_native_import(InsightIndex *index,TMSourceFile *file,const char *path) {
 if(!index || !path || !*path || !g_utf8_validate(path,-1,NULL)) return FALSE;
 if(!file || !file->tags_array) return insight_index_update(index,path,NULL,0);
 guint count=file->tags_array->len,used=0;
 if(count>250000) return FALSE;
 InsightSymbol *symbols=g_new0(InsightSymbol,count); gchar (*kinds)[2]=g_malloc0((gsize)count*2);
 for(guint i=0;i<count;i++) {
  TMTag *tag=g_ptr_array_index(file->tags_array,i);
  if(!tag || !tag->name || !*tag->name || !tag->line || !g_utf8_validate(tag->name,-1,NULL)) continue;
  const char *scope=tag->scope && g_utf8_validate(tag->scope,-1,NULL) ? tag->scope : "";
  kinds[used][0]=tag->kind_letter;
  if(file->lang==TM_PARSER_CPP && tag->kind_letter=='A') kinds[used][0]='t';
  const char *signature=tag->arglist && g_utf8_validate(tag->arglist,-1,NULL) ? tag->arglist : "";
  const char *type=tag->var_type && g_utf8_validate(tag->var_type,-1,NULL) ? tag->var_type : "";
  const char *inheritance=tag->inheritance && g_utf8_validate(tag->inheritance,-1,NULL) ? tag->inheritance : "";
  symbols[used]=(InsightSymbol){tag->name,path,scope,kinds[used],tag->line,signature,type,inheritance}; used++;
 }
 size_t old=insight_index_query(index,"",FALSE,NULL,NULL,path,0,NULL,0);
 gboolean ok=insight_index_count(index)-old+used<=250000 && insight_index_update(index,path,symbols,used);
 g_free(kinds); g_free(symbols); return ok;
}
gboolean insight_native_outline_import(InsightIndex *index,TMSourceFile *file,const char *path) {
 if(!index || !path || !*path || !g_utf8_validate(path,-1,NULL)) return FALSE;
 guint count=file && file->tags_array ? file->tags_array->len : 0,used=0;if(count>250000) return FALSE;
 InsightSymbol *symbols=g_new0(InsightSymbol,count);GPtrArray *kinds=g_ptr_array_new_with_free_func(g_free);
 for(guint n=0;n<count;n++) { TMTag *tag=g_ptr_array_index(file->tags_array,n);
  if(!tag || !tag->name || !*tag->name || !tag->line || !g_utf8_validate(tag->name,-1,NULL) || tag->type==tm_tag_undef_t || (tag->type==tm_tag_local_var_t && tag->kind_letter=='z')) continue;
  gchar *kind=g_strdup_printf("@decl:%u:%c",tag->type,tag->kind_letter ? tag->kind_letter : '?');g_ptr_array_add(kinds,kind);
  const char *scope=tag->scope && g_utf8_validate(tag->scope,-1,NULL) ? tag->scope : "",*args=tag->arglist && g_utf8_validate(tag->arglist,-1,NULL) ? tag->arglist : "",*type=tag->var_type && g_utf8_validate(tag->var_type,-1,NULL) ? tag->var_type : "",*bases=tag->inheritance && g_utf8_validate(tag->inheritance,-1,NULL) ? tag->inheritance : "";
  symbols[used++]=(InsightSymbol){tag->name,path,scope,kind,tag->line,args,type,bases};
 }
 gsize old=insight_index_query(index,"",FALSE,NULL,NULL,path,0,NULL,0);gboolean ok=insight_index_count(index)-old+used<=250000 && insight_index_update(index,path,symbols,used);g_ptr_array_unref(kinds);g_free(symbols);return ok;
}
guint insight_native_definition_rank(const InsightSymbol *symbol,const char *path) {
 return (!g_strcmp0(symbol->path,path) ? 4u : 0u) + (!strcmp(symbol->kind,"f") ? 8u : 0u);
}

gint insight_native_definition_rank_at(const InsightSymbol *symbol,const char *path,const char *scope,gulong line) {
 if(!strcmp(symbol->kind,"Z")) {
  gsize length=strlen(symbol->scope);
  if(g_strcmp0(symbol->path,path) || !scope || !length || symbol->line>line || strncmp(scope,symbol->scope,length) || (scope[length] && !g_str_has_prefix(scope+length,"::"))) return -1;
  return 10000+(gint)MIN(symbol->line,(gulong)G_MAXINT-10000ul);
 }
 const gchar *extension=strrchr(symbol->path,'.');
 gboolean assembly=extension && (!g_ascii_strcasecmp(extension,".asm") || !g_ascii_strcasecmp(extension,".s") || !g_ascii_strcasecmp(extension,".sx") || !g_ascii_strcasecmp(extension,".asm51") || !g_ascii_strcasecmp(extension,".a51"));
 if((!strcmp(symbol->kind,"l") && !assembly) || !strcmp(symbol->kind,"z")) {
  if(g_strcmp0(symbol->path,path) || !scope || !*scope || g_strcmp0(symbol->scope,scope) || symbol->line>line) return -1;
  return 10000+(gint)MIN(symbol->line,(gulong)G_MAXINT-10000ul);
 }
 return (gint)insight_native_definition_rank(symbol,path);
}
gchar *insight_native_function_scope(const InsightIndex *index,const char *path,gulong line) {
 const InsightSymbol *nearest=NULL;
 for(gsize n=0;n<insight_index_count(index);n++) {
  const InsightSymbol *tag=insight_index_at(index,n);
  if(g_strcmp0(tag->path,path) || tag->line>line || (strcmp(tag->kind,"f") && (strcmp(tag->kind,"m") || !tag->signature || !*tag->signature))) continue;
  if(!nearest || tag->line>nearest->line) nearest=tag;
 }
 if(!nearest) return NULL;
 return *nearest->scope ? g_strconcat(nearest->scope,"::",nearest->name,NULL) : g_strdup(nearest->name);
}

static gboolean identifier_byte(gchar c) { return g_ascii_isalnum(c) || c=='_' || (guchar)c>=128; }
static gchar *normalized_scope(const gchar *scope) {
 GString *name=g_string_new(NULL);if(scope) { while(g_str_has_prefix(scope,"::")) scope+=2;for(;*scope;scope++) { if(*scope=='.') g_string_append(name,"::");else g_string_append_c(name,*scope); } }return g_string_free(name,FALSE);
}
static gboolean scope_contains(const gchar *scope,const gchar *outer) { gsize size=strlen(outer);return !size || !strcmp(scope,outer) || (g_str_has_prefix(scope,outer) && scope[size]==':' && scope[size+1]==':'); }
static gsize receiver_start(const InsightIndex *index,const gchar *name,const gchar *path) {
 gsize left=0,right=insight_index_count(index);while(left<right) { gsize mid=left+(right-left)/2;const InsightSymbol *symbol=insight_index_at(index,mid);gint compare=strcmp(symbol->name,name);if(!compare && path) compare=strcmp(symbol->path,path);if(compare<0) left=mid+1;else right=mid; }return left;
}
static void choose_receiver(const InsightSymbol *symbol,gint64 score,const InsightSymbol **best,gint64 *best_score,gboolean *ambiguous) {
 if(!*best || score>*best_score) { *best=symbol;*best_score=score;*ambiguous=FALSE; }
 else if(score==*best_score) {
  gchar *one=insight_type_name(symbol->type,NULL),*two=insight_type_name((*best)->type,NULL);
  if(!one || !two || strcmp(one,two) || g_strcmp0(symbol->scope,(*best)->scope) || g_strcmp0(symbol->path,(*best)->path)) *ambiguous=TRUE;
  g_free(one);g_free(two);
 }
}
static gchar *receiver_owner(const InsightIndex *index,const gchar *path,const gchar *scope,gulong line,const gchar *receiver,const gchar *explicit_scope,gboolean explicit_global,InsightTypeEligible eligible,void *data) {
 const InsightSymbol *best=NULL;gint64 best_score=0;gboolean ambiguous=FALSE;gsize count=insight_index_count(index);
 /* Local/parameter names can be common across a large project. Binary-search
  * the current-file slice first rather than truncating the first 512 names. */
 if(!explicit_scope && path) for(gsize i=receiver_start(index,receiver,path);i<count;i++) {
  const InsightSymbol *symbol=insight_index_at(index,i);if(strcmp(symbol->name,receiver) || strcmp(symbol->path,path)) break;
  if((strcmp(symbol->kind,"l") && strcmp(symbol->kind,"z")) || (eligible && !eligible(symbol,data))) continue;
  gint rank=insight_native_definition_rank_at(symbol,path,scope,line);if(rank<0 || !*symbol->type) continue;
  choose_receiver(symbol,rank,&best,&best_score,&ambiguous);
 }
 if(!best) {
  gchar *context=normalized_scope(scope);guint scanned=0;
  for(gsize i=receiver_start(index,receiver,NULL);i<count;i++) {
   const InsightSymbol *symbol=insight_index_at(index,i);if(strcmp(symbol->name,receiver)) break;
   if(strcmp(symbol->kind,"v") || !*symbol->type || (eligible && !eligible(symbol,data))) continue;
   if(++scanned>4096) { ambiguous=TRUE;break; }
   gchar *owner=normalized_scope(symbol->scope);gboolean visible=explicit_scope ? !strcmp(owner,explicit_scope) : scope_contains(context,owner);
   if(explicit_scope && !explicit_global && !visible && *explicit_scope) {
    gchar *suffix=g_strconcat("::",explicit_scope,NULL);if(g_str_has_suffix(owner,suffix)) { gsize prefix=strlen(owner)-strlen(suffix);gchar saved=owner[prefix];owner[prefix]=0;visible=scope_contains(context,owner);owner[prefix]=saved; }g_free(suffix);
   }
   if(visible) { guint depth=*owner ? 1 : 0;for(const gchar *p=owner;*p;p++) if(p[0]==':' && p[1]==':') { depth++;p++; }choose_receiver(symbol,(gint64)depth*1024+insight_native_definition_rank(symbol,path),&best,&best_score,&ambiguous); }
   g_free(owner);
  }
  g_free(context);
 }
 if(!best || ambiguous) return NULL;
 InsightTypeResult result=insight_type_resolve(index,best->type,best->scope,best->path,eligible,data);return result.owner;
}
static gboolean callable_symbol(const InsightSymbol *symbol) { return !strcmp(symbol->kind,"f") || !strcmp(symbol->kind,"p") || (!strcmp(symbol->kind,"m") && *symbol->signature); }
static gboolean call_name_available(const InsightIndex *index,const gchar *name,InsightTypeEligible eligible,void *data) {
 guint count=0;for(gsize i=receiver_start(index,name,NULL);i<insight_index_count(index);i++) { const InsightSymbol *symbol=insight_index_at(index,i);if(strcmp(symbol->name,name)) break;if(++count>4096) return FALSE;if(!strcmp(symbol->kind,"d") && (!eligible || eligible(symbol,data))) return FALSE; }return TRUE;
}
static gchar *call_result_owner(const InsightIndex *index,const InsightSymbol **symbols,gsize count,InsightTypeEligible eligible,void *data) {
 if(!count || count>256) return NULL;
 gchar *owner=NULL;
 for(gsize i=0;i<count;i++) {
  const InsightSymbol *symbol=symbols[i];if(!callable_symbol(symbol) || !*symbol->type) { g_free(owner);return NULL; }
  InsightTypeResult type=insight_type_resolve_return(index,symbol,eligible,data);
  if(type.status!=INSIGHT_TYPE_FOUND || (owner && strcmp(owner,type.owner))) { insight_type_result_clear(&type);g_free(owner);return NULL; }
  if(!owner) owner=g_strdup(type.owner);
  insight_type_result_clear(&type);
 }
 return owner;
}
static gchar *function_receiver_owner(const InsightIndex *index,const gchar *path,const gchar *scope,gulong line,const gchar *name,const gchar *explicit_scope,gboolean explicit_global,InsightTypeEligible eligible,void *data) {
 gsize total=insight_index_count(index),start=receiver_start(index,name,NULL);guint scanned=0;
 /* A local callable object/function pointer can hide a global function. Its
  * invocation result is not inferred from the object's nominal type. */
 for(gsize i=start;i<total;i++) {
  const InsightSymbol *symbol=insight_index_at(index,i);if(strcmp(symbol->name,name)) break;if(++scanned>4096) return NULL;
  if(!strcmp(symbol->kind,"d") && (!eligible || eligible(symbol,data))) return NULL;
  if(!explicit_scope && (!strcmp(symbol->kind,"l") || !strcmp(symbol->kind,"z")) && (!eligible || eligible(symbol,data)) && insight_native_definition_rank_at(symbol,path,scope,line)>=0) return NULL;
 }
 const InsightSymbol *found[256];
 if(!explicit_scope) {
  gchar *context=normalized_scope(scope),*tail=g_strrstr(context,"::");if(tail) *tail=0;else *context=0;
  while(*context) {
   InsightTypeResult type=insight_type_resolve(index,context,"",path,eligible,data);
   if(type.status==INSIGHT_TYPE_FOUND && (!strcmp(type.symbol->kind,"c") || !strcmp(type.symbol->kind,"s") || !strcmp(type.symbol->kind,"i") || !strcmp(type.symbol->kind,"u") || !strcmp(type.symbol->kind,"class") || !strcmp(type.symbol->kind,"struct") || !strcmp(type.symbol->kind,"interface") || !strcmp(type.symbol->kind,"union"))) {
    int limited=0,ambiguous=0;gsize count=insight_member_query(index,type.owner,name,path,eligible,data,found,G_N_ELEMENTS(found),&limited,&ambiguous);insight_type_result_clear(&type);
    if(limited || ambiguous) { g_free(context);return NULL; }
    if(count) { g_free(context);return call_result_owner(index,found,count,eligible,data); }
    break;
   }
   gboolean invalid=(type.status!=INSIGHT_TYPE_UNKNOWN && type.status!=INSIGHT_TYPE_FOUND) || (type.status==INSIGHT_TYPE_UNKNOWN && type.aliases);insight_type_result_clear(&type);if(invalid) { g_free(context);return NULL; }
   tail=g_strrstr(context,"::");if(tail) *tail=0;else *context=0;
  }
  g_free(context);
 }
 gchar *context=normalized_scope(scope);gsize count=0;guint best_depth=0;gboolean blocked=FALSE;
 for(gsize i=start;i<total;i++) {
  const InsightSymbol *symbol=insight_index_at(index,i);if(strcmp(symbol->name,name)) break;if(eligible && !eligible(symbol,data)) continue;
  gboolean callable=callable_symbol(symbol),variable=!callable && (!strcmp(symbol->kind,"v") || !strcmp(symbol->kind,"x") || !strcmp(symbol->kind,"m") || !strcmp(symbol->kind,"l") || !strcmp(symbol->kind,"z") || !strcmp(symbol->kind,"e") || !strcmp(symbol->kind,"t") || !strcmp(symbol->kind,"c") || !strcmp(symbol->kind,"s") || !strcmp(symbol->kind,"u") || !strcmp(symbol->kind,"g") || !strcmp(symbol->kind,"n") || !strcmp(symbol->kind,"i"));if(!callable && !variable) continue;
  if((!strcmp(symbol->kind,"l") || !strcmp(symbol->kind,"z")) && insight_native_definition_rank_at(symbol,path,scope,line)<0) continue;
  gchar *owner=normalized_scope(symbol->scope);gboolean visible=explicit_scope ? !strcmp(owner,explicit_scope) : scope_contains(context,owner);
  if(explicit_scope && !explicit_global && !visible && *explicit_scope) { gchar *suffix=g_strconcat("::",explicit_scope,NULL);if(g_str_has_suffix(owner,suffix)) { gsize prefix=strlen(owner)-strlen(suffix);gchar saved=owner[prefix];owner[prefix]=0;visible=scope_contains(context,owner);owner[prefix]=saved; }g_free(suffix); }
  if(visible) {
   guint depth=*owner ? 1 : 0;for(const gchar *p=owner;*p;p++) if(p[0]==':' && p[1]==':') { depth++;p++; }
   if(!count && !blocked) best_depth=depth;
   if(depth>best_depth) { best_depth=depth;count=0;blocked=FALSE; }
   if(depth==best_depth) { if(variable) blocked=TRUE;else { if(count==G_N_ELEMENTS(found)) { g_free(owner);g_free(context);return NULL; }found[count++]=symbol; } }
  }
  g_free(owner);
 }
 g_free(context);return blocked ? NULL : call_result_owner(index,found,count,eligible,data);
}
/* Decode only a bounded C-family call suffix. Argument text is not used for
 * overload selection. A lexical mask protects brackets in strings/comments. */
typedef struct { const gchar *text,*mask;gsize length;gchar *owned_mask; } ReceiverSource;
static gboolean receiver_call_start(ReceiverSource *source,gsize end,gsize *open) {
 if(!end || end>source->length) return FALSE;
 if(!source->mask) { source->owned_mask=insight_source_code_mask(source->text,source->length,INSIGHT_CODE_C,NULL);source->mask=source->owned_mask; }
 const gchar *mask=source->mask;if(!mask || mask[end-1]!=')') return FALSE;
 gsize begin=end>4096 ? end-4096 : 0;gchar stack[256];guint depth=0;
 for(gsize at=end;at>begin;at--) {
  gchar c=mask[at-1];if(c==')' || c==']' || c=='}') { if(depth==G_N_ELEMENTS(stack)) break;stack[depth++]=c==')' ? '(' : (c==']' ? '[' : '{'); }
  else if(c=='(' || c=='[' || c=='{') { if(!depth || stack[depth-1]!=c) break;if(!--depth) { if(c=='(') { *open=at-1;return TRUE; }break; } }
 }
 return FALSE;
}
static gchar *member_owner_depth(const InsightIndex *index,const char *path,const char *scope,gulong line,ReceiverSource *source,gsize position,gboolean *member,InsightTypeEligible eligible,void *data,gboolean calls,guint depth) {
 const gchar *text=source->text;*member=FALSE;if(!index || !text || position>source->length || depth>32) return NULL;
 gsize raw=position;while(raw && identifier_byte(text[raw-1])) raw--;while(raw && g_ascii_isspace(text[raw-1])) raw--;
 *member=raw && (text[raw-1]=='.' || (raw>=2 && ((text[raw-2]=='-' && text[raw-1]=='>') || (text[raw-2]==':' && text[raw-1]==':'))));
 if(calls) {
  if(!source->mask) { source->owned_mask=insight_source_code_mask(source->text,source->length,INSIGHT_CODE_C,NULL);source->mask=source->owned_mask; }
  if(!source->mask) { *member=TRUE;return NULL; }
  gsize word=position;if(word>=source->length || !identifier_byte(text[word])) { if(word && identifier_byte(text[word-1])) word--;else word=source->length; }
  if(word<source->length && source->mask[word]!=text[word]) { *member=TRUE;return NULL; }
  text=source->mask;
 }
 gsize at=position;while(at && identifier_byte(text[at-1])) at--;while(at && g_ascii_isspace(text[at-1])) at--;
 if(at>=2 && text[at-2]==':' && text[at-1]==':') {
  *member=TRUE;at-=2;while(at && g_ascii_isspace(text[at-1])) at--;gsize end=at;guint templates=0;
  while(at) {
   gchar c=text[at-1];if(c=='>') templates++;else if(c=='<') { if(!templates) break;templates--; }
   else if(!templates && !identifier_byte(c) && c!=':') {
    if(g_ascii_isspace(c)) { gsize before=at-1,after=at;while(before && g_ascii_isspace(text[before-1])) before--;while(after<end && g_ascii_isspace(text[after])) after++;if((before && text[before-1]==':') || (after<end && text[after]==':')) { at--;continue; } }
    break;
   }
   at--;
  }
  if(templates || (end==at && end)) return NULL;
  gchar *qualified=g_strndup(text+at,end-at);if(!*qualified) return qualified;
  InsightTypeResult result=insight_type_resolve(index,qualified,scope,path,eligible,data);
  if(result.status==INSIGHT_TYPE_FOUND) { g_free(qualified);return result.owner; }
  gchar *owner=result.status==INSIGHT_TYPE_UNKNOWN && !result.aliases ? insight_type_name(qualified,NULL) : NULL;g_free(qualified);insight_type_result_clear(&result);return owner;
 }
 if(at>=2 && text[at-2]=='-' && text[at-1]=='>') at-=2;else if(at && text[at-1]=='.') at--;else return NULL;
 *member=TRUE;while(at && g_ascii_isspace(text[at-1])) at--;gboolean call=at && text[at-1]==')';
 if(call) { gsize open=0;if(!calls || !receiver_call_start(source,at,&open)) return NULL;at=open;while(at && g_ascii_isspace(text[at-1])) at--; }
 gsize end=at;while(at && identifier_byte(text[at-1])) at--;if(end==at) return NULL;
 gchar *receiver=g_strndup(text+at,end-at),*explicit_scope=NULL;gboolean explicit_global=FALSE;
 if(call && !call_name_available(index,receiver,eligible,data)) { g_free(receiver);return NULL; }
 gsize prefix=at;while(prefix && g_ascii_isspace(text[prefix-1])) prefix--;
 if(prefix && (text[prefix-1]=='.' || (prefix>=2 && ((text[prefix-2]=='-' && text[prefix-1]=='>') || (text[prefix-2]==':' && text[prefix-1]==':'))))) {
  gboolean nested=FALSE;gchar *parent=member_owner_depth(index,path,scope,line,source,end,&nested,eligible,data,calls,depth+1),*result=NULL;
  if(parent) {
   const InsightSymbol *fields[256];size_t count=insight_member_query(index,parent,receiver,path,eligible,data,fields,G_N_ELEMENTS(fields),NULL,NULL);
   if(call) result=call_result_owner(index,fields,count,eligible,data);
   gboolean valid=!call && count>0 && count<=G_N_ELEMENTS(fields);
   for(size_t i=0;valid && i<count;i++) {
    const InsightSymbol *field=fields[i];
    if((strcmp(field->kind,"m") && strcmp(field->kind,"v")) || *field->signature || !*field->type) { valid=FALSE;break; }
    InsightTypeResult type=insight_type_resolve(index,field->type,field->scope,field->path,eligible,data);
    if(type.status!=INSIGHT_TYPE_FOUND || (result && strcmp(result,type.owner))) valid=FALSE;
    else if(!result) result=g_strdup(type.owner);
    insight_type_result_clear(&type);
   }
   if(!call && !valid) g_clear_pointer(&result,g_free);
  }
  g_free(parent);
  /* Namespace variables may have no explicit namespace tag. Retain the
   * existing lexical global receiver lookup for that case. */
  if(result || text[prefix-1]!=':') { g_free(receiver);return result; }
 }
 if(prefix>=2 && text[prefix-2]==':' && text[prefix-1]==':') {
  gsize stop=prefix-2;while(stop && g_ascii_isspace(text[stop-1])) stop--;gsize start=stop;
  while(start) {
   if(identifier_byte(text[start-1]) || text[start-1]==':') { start--;continue; }
   if(g_ascii_isspace(text[start-1])) { gsize before=start-1,after=start;while(before && g_ascii_isspace(text[before-1])) before--;while(after<stop && g_ascii_isspace(text[after])) after++;if((before && text[before-1]==':') || (after<stop && text[after]==':')) { start--;continue; } }
   break;
  }
  gchar *raw=g_strndup(text+start,stop-start);if(!*raw) { explicit_scope=g_strdup("");explicit_global=TRUE; }else explicit_scope=insight_type_name(raw,&explicit_global);g_free(raw);
  if(!explicit_scope) { g_free(receiver);return NULL; }
 }
 if(!call && !strcmp(receiver,"this") && !explicit_scope) {
  gchar *owner=normalized_scope(scope);gchar *last=g_strrstr(owner,"::");if(last) *last=0;else *owner=0;
  while(*owner) { InsightTypeResult result=insight_type_resolve(index,owner,NULL,path,eligible,data);if(result.status==INSIGHT_TYPE_FOUND) { g_free(receiver);g_free(owner);return result.owner; }insight_type_result_clear(&result);last=g_strrstr(owner,"::");if(last) *last=0;else *owner=0; }
  g_free(receiver);g_free(owner);return NULL;
 }
 gchar *owner=call ? function_receiver_owner(index,path,scope,line,receiver,explicit_scope,explicit_global,eligible,data) : receiver_owner(index,path,scope,line,receiver,explicit_scope,explicit_global,eligible,data);g_free(receiver);g_free(explicit_scope);return owner;
}
gchar *insight_native_member_owner_source_filtered(const InsightIndex *index,const char *path,const char *scope,gulong line,const InsightNativeSource *text,gsize position,gboolean *member,gboolean calls,InsightTypeEligible eligible,void *data) {
 if(!text) { *member=FALSE;return NULL; }ReceiverSource source={text->text,text->mask,text->length,NULL};gchar *owner=member_owner_depth(index,path,scope,line,&source,position,member,eligible,data,calls,0);g_free(source.owned_mask);return owner;
}
gchar *insight_native_member_owner_calls_filtered(const InsightIndex *index,const char *path,const char *scope,gulong line,const char *text,gsize position,gboolean *member,gboolean calls,InsightTypeEligible eligible,void *data) { InsightNativeSource source={text,NULL,text ? strlen(text) : 0,NULL};return insight_native_member_owner_source_filtered(index,path,scope,line,&source,position,member,calls,eligible,data); }
gchar *insight_native_member_owner_filtered(const InsightIndex *index,const char *path,const char *scope,gulong line,const char *text,gsize position,gboolean *member,InsightTypeEligible eligible,void *data) { return insight_native_member_owner_calls_filtered(index,path,scope,line,text,position,member,TRUE,eligible,data); }
gchar *insight_native_member_owner(const InsightIndex *index,const char *path,const char *scope,gulong line,const char *text,gsize position,gboolean *member) { return insight_native_member_owner_filtered(index,path,scope,line,text,position,member,NULL,NULL); }
gboolean insight_native_member_matches(const InsightSymbol *symbol,const char *owner) {
 if(!owner || !symbol->scope || !strcmp(symbol->kind,"l") || !strcmp(symbol->kind,"z")) return FALSE;
 gchar *one=normalized_scope(symbol->scope),*two=normalized_scope(owner);gboolean match=!strcmp(one,two);g_free(one);g_free(two);return match;
}

gboolean insight_native_parse(InsightIndex *index,TMSourceFile *file,const char *path,const char *text,gsize length) {
 if(!index || !file || !file->file_name || file->lang<0 || (guint)file->lang>=tm_ctags_get_lang_count() || !path || !*path || !g_utf8_validate(path,-1,NULL) || length>2u*1024u*1024u || (length && (!text || memchr(text,0,length) || !g_utf8_validate(text,(gssize)length,NULL)))) return FALSE;
 /* tm_source_file_parse currently fills tags but returns FALSE for nonempty
  * sources. Use the actual tag array, not that obsolete retry result. */
 tm_source_file_parse(file,(guchar *)text,length,TRUE);
 return insight_native_import(index,file,path);
}

/* C/C++ headers and assembly share native symbols. Other parser languages
 * must match; an unknown language never authorizes a cross-file jump. */
gboolean insight_native_language_compatible(TMParserType from,TMParserType to) {
 if(from<0 || to<0) return FALSE;
 gboolean native_from=from==TM_PARSER_C || from==TM_PARSER_CPP || from==TM_PARSER_ASM || from==TM_PARSER_CUDA;
 gboolean native_to=to==TM_PARSER_C || to==TM_PARSER_CPP || to==TM_PARSER_ASM || to==TM_PARSER_CUDA;
 return from==to || (native_from && native_to);
}
