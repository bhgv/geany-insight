/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-index.h"
#include "../insight/core/structure_select.h"
#include "../insight/desktop/source_mask.h"
#include "../insight/desktop/relation_query.h"
#include <string.h>
#define SCOPE_FUNCTION_LIMIT 10000u
#define SCOPE_RANGE_LIMIT 16384u
#define SCOPE_STRING_LIMIT (8u*1024u*1024u)
#define SCOPE_LINE_LIMIT 65536u
#define SCOPE_BODY_LIMIT (256u*1024u)
typedef struct { gsize begin,end;gchar *scope; } ScopeRange;
typedef struct { gulong line;guint start,count;gboolean built,valid;gchar *single;GPtrArray *ranges; } ScopeGroup;
struct InsightNativeScopes { InsightIndex *index;InsightNativeSource source;gchar *owned_mask;GPtrArray *functions,*groups;GArray *lines;gsize strings;guint ranges; };
static gboolean ident(gchar c) { return g_ascii_isalnum(c) || c=='_' || c=='$' || (guchar)c>=128; }
static gchar *scope_name(const InsightSymbol *tag) { return *tag->scope ? g_strconcat(tag->scope,"::",tag->name,NULL) : g_strdup(tag->name); }
static void range_free(gpointer data) { ScopeRange *range=data;g_free(range->scope);g_free(range); }
static void group_free(gpointer data) { ScopeGroup *group=data;g_free(group->single);if(group->ranges) g_ptr_array_unref(group->ranges);g_free(group); }
static gint function_compare(gconstpointer a,gconstpointer b) {
 const InsightSymbol *one=*(const InsightSymbol *const *)a,*two=*(const InsightSymbol *const *)b;
 if(one->line!=two->line) return one->line<two->line ? -1 : 1;
 gint result=strcmp(one->scope,two->scope);return result ? result : strcmp(one->name,two->name);
}
void insight_native_scopes_free(InsightNativeScopes *cache) {
 if(!cache) return;
 g_clear_pointer(&cache->groups,g_ptr_array_unref);g_clear_pointer(&cache->functions,g_ptr_array_unref);g_clear_pointer(&cache->lines,g_array_unref);g_free(cache->owned_mask);insight_index_free(cache->index);g_free(cache);
}
InsightNativeScopes *insight_native_scopes_new(InsightIndex *index,const gchar *path,const InsightNativeSource *source) {
 if(!index || !source || !source->text || source->length>2u*1024u*1024u || memchr(source->text,0,source->length) || !g_utf8_validate(source->text,source->length,NULL)) return NULL;
 InsightNativeScopes *cache=g_new0(InsightNativeScopes,1);cache->index=insight_index_ref(index);cache->source=*source;cache->functions=g_ptr_array_new();cache->groups=g_ptr_array_new_with_free_func(group_free);cache->lines=g_array_new(FALSE,FALSE,sizeof(guint32));
 for(gsize i=0;i<insight_index_count(index);i++) {
  const InsightSymbol *tag=insight_index_at(index,i);if(g_strcmp0(tag->path,path) || !tag->line || (strcmp(tag->kind,"f") && (strcmp(tag->kind,"m") || !*tag->signature))) continue;
  if(cache->functions->len==SCOPE_FUNCTION_LIMIT) { insight_native_scopes_free(cache);return NULL; }g_ptr_array_add(cache->functions,(gpointer)tag);
 }
 g_ptr_array_sort(cache->functions,function_compare);
 for(guint i=0;i<cache->functions->len;i++) { const InsightSymbol *tag=g_ptr_array_index(cache->functions,i);ScopeGroup *group=cache->groups->len ? g_ptr_array_index(cache->groups,cache->groups->len-1) : NULL;
  if(!group || group->line!=tag->line) { group=g_new0(ScopeGroup,1);group->line=tag->line;group->start=i;g_ptr_array_add(cache->groups,group); }group->count++;
 }
 guint32 offset=0;g_array_append_val(cache->lines,offset);for(gsize i=0;i<source->length;i++) if(source->text[i]=='\n') { offset=(guint32)i+1;g_array_append_val(cache->lines,offset); }
 return cache;
}
static gboolean word_is(const gchar *mask,gsize first,gsize last,const gchar *word) { return last-first==strlen(word) && !memcmp(mask+first,word,last-first); }
static gboolean declaration_before(const gchar *mask,gsize line,gsize at,const InsightSymbol *tag) {
 gsize start=at;while(start>line && g_ascii_isspace(mask[start-1])) start--;
 if(start>=line+2 && mask[start-1]==':' && mask[start-2]==':') {
  gsize stop=start-2;start=stop;
  while(start>line) {
   if(ident(mask[start-1]) || mask[start-1]==':') { start--;continue; }
   if(g_ascii_isspace(mask[start-1])) { gsize before=start-1,after=start;while(before>line && g_ascii_isspace(mask[before-1])) before--;while(after<stop && g_ascii_isspace(mask[after])) after++;if((before>line && mask[before-1]==':') || (after<stop && mask[after]==':')) { start--;continue; } }
   break;
  }
  gchar *raw=g_strndup(mask+start,stop-start);gboolean global=FALSE;gchar *owner=insight_type_name(raw,&global);g_free(raw);if(!owner) return FALSE;
  gboolean same=!strcmp(owner,tag->scope);if(!same && !global) { gchar *suffix=g_strconcat("::",owner,NULL);same=g_str_has_suffix(tag->scope,suffix);g_free(suffix); }g_free(owner);if(!same) return FALSE;
 }
 while(start>line && g_ascii_isspace(mask[start-1])) start--;
 if(start==line) return TRUE;
 gchar previous=mask[start-1];if(strchr("()[]=,.?+-/%!|^~",previous)) return FALSE;
 gsize word=start;while(word>line && ident(mask[word-1])) word--;
 static const gchar *blocked[]={"return","co_return","throw","new","delete","sizeof","alignof","decltype","operator","case"};
 for(guint i=0;i<G_N_ELEMENTS(blocked);i++) if(word_is(mask,word,start,blocked[i])) return FALSE;
 const gchar *tail=g_strrstr(tag->scope,"::");tail=tail ? tail+2 : tag->scope;
 if(!strcmp(tail,tag->name) && (ident(previous) || previous=='*' || previous=='&' || previous=='>')) {
  static const gchar *ctor_words[]={"inline","explicit","constexpr","consteval","friend","export"};gboolean accepted=FALSE;
  for(guint i=0;i<G_N_ELEMENTS(ctor_words);i++) if(word_is(mask,word,start,ctor_words[i])) accepted=TRUE;
  if(!accepted) return FALSE;
 }
 return TRUE;
}
static gboolean declaration_after(const gchar *mask,gsize length,gsize at,const InsightSymbol *tag) {
 while(at<length && g_ascii_isspace(mask[at])) { at++; }
 if(at==length) return FALSE;
 if(mask[at]=='{') return TRUE;
 if(mask[at]=='-' && at+1<length && mask[at+1]=='>') return TRUE;
 if(mask[at]==':' && (at+1==length || mask[at+1]!=':')) { const gchar *tail=g_strrstr(tag->scope,"::");return !strcmp(tail ? tail+2 : tag->scope,tag->name); }
 gsize end=at;while(end<length && ident(mask[end])) end++;
 static const gchar *words[]={"const","volatile","noexcept","override","final","requires","__attribute__","__declspec"};
 for(guint i=0;i<G_N_ELEMENTS(words);i++) if(word_is(mask,at,end,words[i])) return TRUE;
 return FALSE;
}
static void build_group(InsightNativeScopes *cache,ScopeGroup *group) {
 group->built=TRUE;
 if(group->count==1) { const InsightSymbol *tag=g_ptr_array_index(cache->functions,group->start);gchar *name=scope_name(tag);gsize size=strlen(name)+1;if(size<=4096 && cache->strings+size<=SCOPE_STRING_LIMIT) { group->single=name;group->valid=TRUE;cache->strings+=size; }else g_free(name);return; }
 if(group->count>64 || group->line>cache->lines->len) return;
 gsize line=g_array_index(cache->lines,guint32,group->line-1),line_end=group->line<cache->lines->len ? g_array_index(cache->lines,guint32,group->line) : cache->source.length;
 if(line_end-line>SCOPE_LINE_LIMIT) return;
 if(!cache->source.mask) { cache->owned_mask=insight_source_code_mask(cache->source.text,cache->source.length,INSIGHT_CODE_C,NULL);cache->source.mask=cache->owned_mask; }if(!cache->source.mask) return;
 const gchar *mask=cache->source.mask;GPtrArray *ranges=g_ptr_array_new_with_free_func(range_free);GHashTable *seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);gboolean valid=TRUE;guint probes=0;gsize bytes=0;
 for(guint i=0;i<group->count && valid;i++) {
  const InsightSymbol *tag=g_ptr_array_index(cache->functions,group->start+i);gchar *scope=scope_name(tag);gsize scope_size=strlen(scope)+1,name_size=strlen(tag->name);gboolean found=FALSE;
  if(g_hash_table_contains(seen,scope)) { g_free(scope);valid=FALSE;break; }g_hash_table_add(seen,scope);
  if(scope_size>4096 || !name_size || name_size>1024) { valid=FALSE;break; }
  for(gsize at=line;at+name_size<=line_end;) {
   const gchar *hit=g_strstr_len(mask+at,line_end-at,tag->name);if(!hit) break;gsize begin=(gsize)(hit-mask);at=begin+name_size;
   if((begin>line && ident(mask[begin-1])) || (at<cache->source.length && ident(mask[at]))) continue;
   gsize open=at;while(open<cache->source.length && g_ascii_isspace(mask[open])) open++;
   if(open==cache->source.length || mask[open]!='(' || !declaration_before(mask,line,begin,tag)) continue;
   if(++probes>256) { valid=FALSE;break; }
   gsize limit=MIN(cache->source.length,begin+SCOPE_BODY_LIMIT),first=0,after=0,body=0,end=0;
   if(insight_select_match(mask,limit,open,&first,&after)!=1 || first!=open) { valid=FALSE;break; }
   if(!declaration_after(mask,limit,after,tag)) continue;
   if(insight_function_range(cache->source.text,mask,limit,begin,INSIGHT_FUNCTION_BRACES,&body,&end)!=1) continue;
   if(cache->ranges+ranges->len>=SCOPE_RANGE_LIMIT || cache->strings+bytes+scope_size>SCOPE_STRING_LIMIT) { valid=FALSE;break; }
   ScopeRange *range=g_new0(ScopeRange,1);range->begin=begin;range->end=end;range->scope=g_strdup(scope);g_ptr_array_add(ranges,range);bytes+=scope_size;found=TRUE;
  }
  if(!found) valid=FALSE;
 }
 g_hash_table_unref(seen);
 if(valid && ranges->len) { group->ranges=ranges;group->valid=TRUE;cache->ranges+=ranges->len;cache->strings+=bytes; }else g_ptr_array_unref(ranges);
}
gchar *insight_native_scopes_at(InsightNativeScopes *cache,gulong line,gsize position,gboolean *uncertain) {
 if(uncertain) *uncertain=TRUE;
 if(!cache || !line || line>cache->lines->len || position>cache->source.length) return NULL;
 guint first=0,last=cache->groups->len;while(first<last) { guint mid=first+(last-first)/2;ScopeGroup *group=g_ptr_array_index(cache->groups,mid);if(group->line<=line) first=mid+1;else last=mid; }if(!first) { if(uncertain) *uncertain=FALSE;return NULL; }
 ScopeGroup *group=g_ptr_array_index(cache->groups,first-1);if(!group->built) build_group(cache,group);if(!group->valid) return NULL;if(group->single) { if(uncertain) *uncertain=FALSE;return g_strdup(group->single); }
 const ScopeRange *best=NULL;gboolean ambiguous=FALSE;
 for(guint i=0;i<group->ranges->len;i++) { ScopeRange *range=g_ptr_array_index(group->ranges,i);if(position<range->begin || position>=range->end) continue;
  if(!best || range->end-range->begin<best->end-best->begin) { best=range;ambiguous=FALSE; }else if(range->end-range->begin==best->end-best->begin && strcmp(range->scope,best->scope)) ambiguous=TRUE;
 }
 if(best && !ambiguous) { if(uncertain) *uncertain=FALSE;return g_strdup(best->scope); }return NULL;
}

/* Python parser scopes use dots; native owner queries use ::. */
static gchar *python_scope_name(const gchar *name) {
 GString *out=g_string_new(NULL);for(const gchar *p=name ? name : "";*p;p++) { if(*p=='.') g_string_append(out,"::");else g_string_append_c(out,*p); }return g_string_free(out,FALSE);
}
static gchar *python_symbol_scope(const InsightSymbol *tag) {
 gchar *outer=python_scope_name(tag->scope),*name=*outer ? g_strconcat(outer,"::",tag->name,NULL) : g_strdup(tag->name);g_free(outer);return name;
}
static gchar *python_scope_at(const InsightIndex *index,const gchar *path,const InsightNativeSource *source,gsize position,gboolean *valid) {
 *valid=FALSE;
 if(!index || !source || !source->text || !source->mask || source->length>2u*1024u*1024u || position>source->length) return NULL;
 *valid=TRUE;GArray *lines=g_array_new(FALSE,FALSE,sizeof(gsize));gsize offset=0;g_array_append_val(lines,offset);
 for(gsize i=0;i<source->length;i++) if(source->text[i]=='\n') { offset=i+1;g_array_append_val(lines,offset); }
 gchar *scope=NULL;gsize smallest=G_MAXSIZE,work=0;guint visits=0;
 for(gsize i=0;i<insight_index_count(index);i++) {
  const InsightSymbol *tag=insight_index_at(index,i);if(g_strcmp0(tag->path,path) || !tag->line || tag->line>lines->len || (strcmp(tag->kind,"f") && strcmp(tag->kind,"m") && strcmp(tag->kind,"c"))) continue;
  if(++visits>10000) { *valid=FALSE;g_clear_pointer(&scope,g_free);break; }
  gsize start=g_array_index(lines,gsize,tag->line-1),body=0,end=0;if(start>position) continue;
  if(insight_suite_range(source->text,source->mask,source->length,start,INSIGHT_FUNCTION_PYTHON,&body,&end)==1) {
   if(end-start>8u*1024u*1024u-work) { *valid=FALSE;g_clear_pointer(&scope,g_free);break; }work+=end-start;
   if(position<end && end-start<smallest) { smallest=end-start;g_free(scope);scope=python_symbol_scope(tag); }
  }
 }
 g_array_unref(lines);return scope;
}
gchar *insight_native_python_scope(const InsightIndex *index,const gchar *path,const InsightNativeSource *source,gsize position) {
 gboolean valid;return python_scope_at(index,path,source,position,&valid);
}
static gboolean python_within(const gchar *scope,const gchar *outer) {
 gsize size=strlen(outer);return !size || !strcmp(scope,outer) || (g_str_has_prefix(scope,outer) && g_str_has_prefix(scope+size,"::"));
}
/* A declaration affects its complete suite, including earlier references. */
typedef struct { gchar *module,*name;gboolean ambiguous; } PythonImport;
static void python_import_free(gpointer data) { PythonImport *item=data;g_free(item->module);g_free(item->name);g_free(item); }
struct InsightNativePythonBindings { GHashTable *scopes,*imports,*import_names,*modules,*static_methods,*constructors;gboolean valid; };
static void python_space(const gchar *mask,gsize length,gsize *at,gboolean multiline) {
 while(*at<length) {
  if(mask[*at]==' ' || mask[*at]=='\t' || (multiline && (mask[*at]=='\n' || mask[*at]=='\r'))) { (*at)++;continue; }
  if(mask[*at]=='\\' && *at+1<length && (mask[*at+1]=='\n' || mask[*at+1]=='\r')) { *at+=2;if(*at<length && mask[*at-1]=='\r' && mask[*at]=='\n') (*at)++;continue; }
  break;
 }
}
static gchar *python_import_name(const gchar *mask,gsize length,gsize *at,gboolean dotted,gboolean relative) {
 gsize first=*at;if(relative) while(*at<length && mask[*at]=='.') (*at)++;
 if(*at<length && ident(mask[*at]) && !g_ascii_isdigit(mask[*at])) {
  do { while(*at<length && ident(mask[*at])) (*at)++;if(!dotted || *at+1>=length || mask[*at]!='.' || !ident(mask[*at+1]) || g_ascii_isdigit(mask[*at+1])) break;(*at)++; }while(TRUE);
 }
 if(*at==first || *at-first>1024) return NULL;
 return g_strndup(mask+first,*at-first);
}
static gboolean python_import_statement(InsightNativePythonBindings *bindings,const gchar *scope,const gchar *mask,gsize length,gsize *at,gboolean from) {
 gchar *module=NULL;gboolean parenthesized=FALSE,ok=FALSE;guint names=0;
 python_space(mask,length,at,FALSE);
 if(from) {
  module=python_import_name(mask,length,at,TRUE,TRUE);if(!module) goto done;python_space(mask,length,at,FALSE);
  if(*at+6>length || memcmp(mask+*at,"import",6) || (*at+6<length && ident(mask[*at+6]))) goto done;
  *at+=6;python_space(mask,length,at,FALSE);if(*at<length && mask[*at]=='(') { parenthesized=TRUE;(*at)++; }
 }
 while(*at<length) {
  python_space(mask,length,at,parenthesized);
  if(from && *at<length && mask[*at]=='*') { (*at)++;ok=TRUE;break; } /* Wildcards deliberately create no guessed bindings. */
  gchar *name=python_import_name(mask,length,at,!from,FALSE);if(!name) goto done;
  gchar *local=NULL;gboolean aliased=FALSE;python_space(mask,length,at,parenthesized);
  if(*at+2<=length && !memcmp(mask+*at,"as",2) && (*at+2==length || !ident(mask[*at+2]))) {
   aliased=TRUE;*at+=2;python_space(mask,length,at,parenthesized);local=python_import_name(mask,length,at,FALSE,FALSE);
   if(!local) { g_free(name);goto done; }
  }else { local=g_strdup(name);if(!from) { gchar *dot=strchr(local,'.');if(dot) *dot=0; } }
  GHashTable *imports=g_hash_table_lookup(bindings->imports,scope);
  if(!imports) { imports=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,python_import_free);g_hash_table_insert(bindings->imports,g_strdup(scope),imports); }
  g_hash_table_add(bindings->import_names,g_strdup(local));
  PythonImport *item=g_hash_table_lookup(imports,local);const gchar *target=from ? module : name,*symbol=from ? name : "";
  if(!item) { item=g_new0(PythonImport,1);item->module=g_strdup(target);item->name=g_strdup(symbol);g_hash_table_insert(imports,local,item); }
  else { if(strcmp(item->module,target) || strcmp(item->name,symbol)) item->ambiguous=TRUE;g_free(local); }
  if(!from && !aliased && strchr(name,'.')) item->ambiguous=TRUE;
  g_free(name);if(++names>256) goto done;python_space(mask,length,at,parenthesized);
  if(*at<length && mask[*at]==',') { (*at)++;python_space(mask,length,at,parenthesized);if(parenthesized && *at<length && mask[*at]==')') { (*at)++;ok=TRUE;break; }continue; }
  if(parenthesized) { if(*at>=length || mask[*at]!=')') goto done;(*at)++; }
  ok=TRUE;break;
 }
 if(ok) { python_space(mask,length,at,FALSE);if(*at<length && mask[*at]!=';' && mask[*at]!='\n' && mask[*at]!='\r') ok=FALSE; }
done:
 g_free(module);return ok;
}
void insight_native_python_bindings_free(InsightNativePythonBindings *bindings) {
 if(!bindings) return;
 g_hash_table_unref(bindings->scopes);g_hash_table_unref(bindings->imports);g_hash_table_unref(bindings->import_names);g_hash_table_unref(bindings->modules);g_hash_table_unref(bindings->static_methods);g_hash_table_unref(bindings->constructors);g_free(bindings);
}
static gboolean python_static_before(const gchar *mask,gsize position) {
 while(position && mask[position-1]!='\n') position--;
 for(guint lines=0;position && lines<128;lines++) {
  gsize end=position-1,first=end;while(first && mask[first-1]!='\n') first--;position=first;
  while(first<end && g_ascii_isspace(mask[first])) first++;
  while(end>first && g_ascii_isspace(mask[end-1])) end--;
  if(first==end) continue;
  if(mask[first]!='@') break;
  first++;
  while(first<end && (mask[first]==' ' || mask[first]=='\t')) first++;
  if(word_is(mask,first,end,"staticmethod")) return TRUE;
 }
 return FALSE;
}
InsightNativePythonBindings *insight_native_python_bindings_new(const InsightIndex *index,const gchar *path,const InsightNativeSource *source) {
 InsightNativePythonBindings *bindings=g_new0(InsightNativePythonBindings,1);
 bindings->scopes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_hash_table_unref);
 bindings->imports=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_hash_table_unref);
 bindings->import_names=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 bindings->modules=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
 bindings->static_methods=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 bindings->constructors=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
 if(!index || !source || !source->text || !source->mask || source->length>2u*1024u*1024u) return bindings;
 const gchar *mask=source->mask;gsize work=0;guint count=0,names=0;
 for(gsize at=0;at<source->length;) {
  if(!ident(mask[at])) { at++;continue; }
  gsize first=at;while(at<source->length && ident(mask[at])) at++;
  if(word_is(mask,first,at,"def") && python_static_before(mask,first)) {
   if(source->length>32u*1024u*1024u-work) return bindings;
   work+=source->length;
   gboolean valid=FALSE;gchar *method=python_scope_at(index,path,source,first,&valid);if(!valid) { g_free(method);return bindings; }if(method) g_hash_table_add(bindings->static_methods,method);
  }
  guint mode=word_is(mask,first,at,"global") ? 1 : word_is(mask,first,at,"nonlocal") ? 2 : 0;
  gsize before=first;while(before && (mask[before-1]==' ' || mask[before-1]=='\t')) before--;
  gboolean boundary=!before || mask[before-1]=='\n' || mask[before-1]=='\r' || mask[before-1]==';' || mask[before-1]==':';
  gboolean from=boundary && word_is(mask,first,at,"from"),imported=from || (boundary && word_is(mask,first,at,"import"));
  if(!mode && !imported) continue;
  if(++count>64 || source->length>32u*1024u*1024u-work) return bindings;
  work+=source->length;gboolean valid_scope=FALSE;gchar *scope=python_scope_at(index,path,source,first,&valid_scope);if(!valid_scope) { g_free(scope);return bindings; }if(!scope) scope=g_strdup("");
  if(imported) { gboolean ok=python_import_statement(bindings,scope,mask,source->length,&at,from);g_free(scope);if(!ok) return bindings;continue; }
  if(!*scope && mode==2) { g_free(scope);return bindings; }
  GHashTable *declarations=g_hash_table_lookup(bindings->scopes,scope);
  if(!declarations) { declarations=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);g_hash_table_insert(bindings->scopes,scope,declarations); }else g_free(scope);
  gboolean expect_name=TRUE;
  while(at<source->length && mask[at]!=';' && mask[at]!='\n' && mask[at]!='\r') {
   if(mask[at]==' ' || mask[at]=='\t') { at++;continue; }
   if(mask[at]=='\\' && at+1<source->length && (mask[at+1]=='\n' || mask[at+1]=='\r')) { at+=2;if(at<source->length && mask[at-1]=='\r' && mask[at]=='\n') at++;continue; }
   if(expect_name && ident(mask[at]) && !g_ascii_isdigit(mask[at])) {
    gsize name=at;while(at<source->length && ident(mask[at])) at++;if(++names>4096 || at-name>1024) return bindings;
    gchar *word=g_strndup(mask+name,at-name);guint previous=GPOINTER_TO_UINT(g_hash_table_lookup(declarations,word));
    if(previous && previous!=mode) { g_free(word);return bindings; }
    g_hash_table_replace(declarations,word,GUINT_TO_POINTER(mode));expect_name=FALSE;
   }
   else if(!expect_name && mask[at]==',') { at++;expect_name=TRUE; }
   else return bindings;
  }
  if(expect_name) return bindings;
 }
 bindings->valid=TRUE;return bindings;
}
gchar *insight_native_python_binding_scope(const InsightIndex *index,const gchar *path,const gchar *scope,const gchar *word,const InsightNativeSource *source) {
 if(!index || !word || !*word) return NULL;
 InsightNativePythonBindings *owned=NULL,*bindings=source ? source->python : NULL;
 if(!bindings) bindings=owned=insight_native_python_bindings_new(index,path,source);
 if(!bindings->valid) { insight_native_python_bindings_free(owned);return NULL; }
 gchar *current=python_scope_name(scope);gboolean nonlocal=FALSE,initial=TRUE;
 while(*current) {
  GHashTable *imports=g_hash_table_lookup(bindings->imports,current);
  gboolean class_scope=FALSE,bound=imports && g_hash_table_contains(imports,word);
  for(gsize i=0;i<insight_index_count(index);i++) { const InsightSymbol *tag=insight_index_at(index,i);if(g_strcmp0(tag->path,path)) continue;
   if(!strcmp(tag->kind,"c")) { gchar *name=python_symbol_scope(tag);if(!strcmp(name,current)) class_scope=TRUE;g_free(name); }
   if(!strcmp(tag->name,word)) { gchar *owner=python_scope_name(tag->scope);if(!strcmp(owner,current)) bound=TRUE;g_free(owner); }
  }
  if(initial || !class_scope) {
   GHashTable *declarations=g_hash_table_lookup(bindings->scopes,current);guint mode=declarations ? GPOINTER_TO_UINT(g_hash_table_lookup(declarations,word)) : 0;
   if(mode==1) { if(nonlocal) { g_free(current);current=NULL; }else *current=0;break; }
   if(mode==2) nonlocal=TRUE;
   else if(bound) break;
  }
  gchar *parent=g_strrstr(current,"::");if(parent) *parent=0;else *current=0;initial=FALSE;
 }
 if(current && !*current && nonlocal) g_clear_pointer(&current,g_free);
 insight_native_python_bindings_free(owned);return current;
}
gint insight_native_python_rank(const InsightIndex *index,const InsightSymbol *tag,const gchar *path,const gchar *scope,gulong line) {
 (void)line; /* Python locals bind their complete function suite. */
 /* Unqualified project-wide names are not Python imports. */
 if(g_strcmp0(tag->path,path)) return -1;
 gchar *owner=python_scope_name(tag->scope),*current=python_scope_name(scope);gint rank=-1;
 if(python_within(current,owner)) {
  gboolean class_scope=FALSE;
  for(gsize i=0;*owner && i<insight_index_count(index);i++) { const InsightSymbol *candidate=insight_index_at(index,i);if(strcmp(candidate->kind,"c") || g_strcmp0(candidate->path,path)) continue;gchar *name=python_symbol_scope(candidate);class_scope=!strcmp(owner,name);g_free(name);if(class_scope) break; }
  if((!class_scope || !strcmp(current,owner))) rank=100+(gint)strlen(owner)*16+(!strcmp(tag->kind,"l") ? 4 : 0);
 }
 g_free(owner);g_free(current);return rank;
}
/* Infer only a single simple assignment to a directly indexed local class.
 * Competing bindings, aliases, factory functions and compound expressions
 * require type/flow analysis and deliberately have no guessed owner. */
static gchar *python_constructor_owner(const InsightIndex *index,const gchar *path,const InsightSymbol *variable,const InsightNativeSource *source,InsightNativePythonBindings *bindings,InsightTypeEligible eligible,void *data) {
 if(strcmp(variable->kind,"l") && strcmp(variable->kind,"v")) return NULL;
 gchar *key=g_strconcat(variable->scope,"\037",variable->name,NULL);const gchar *cached=g_hash_table_lookup(bindings->constructors,key);
 if(cached) { gchar *owner=*cached ? g_strdup(cached) : NULL;g_free(key);return owner; }
 gchar *owner=NULL,*constructor=NULL,*lookup=NULL,*scope=python_scope_name(variable->scope);gsize at=0;gulong line=1;guint assignments=0;
 for(gsize i=0;i<insight_index_count(index);i++) { const InsightSymbol *tag=insight_index_at(index,i);if(!g_strcmp0(tag->path,path) && !strcmp(tag->scope,variable->scope) && !strcmp(tag->name,variable->name)) assignments++; }
 if(assignments!=1 || !variable->line) goto done;
 while(at<source->length && line<variable->line) if(source->mask[at++]=='\n') line++;
 if(line!=variable->line) goto done;
 const gchar *mask=source->mask;while(at<source->length && (mask[at]==' ' || mask[at]=='\t')) at++;gsize name_size=strlen(variable->name);
 if(at+name_size>source->length || memcmp(mask+at,variable->name,name_size) || (at+name_size<source->length && ident(mask[at+name_size]))) goto done;
 at+=name_size;while(at<source->length && (mask[at]==' ' || mask[at]=='\t')) at++;
 if(at==source->length || mask[at++]!='=' || (at<source->length && mask[at]=='=')) goto done;
 python_space(mask,source->length,&at,FALSE);constructor=python_import_name(mask,source->length,&at,FALSE,FALSE);if(!constructor) goto done;
 python_space(mask,source->length,&at,FALSE);if(at==source->length || mask[at]!='(') goto done;
 gsize open=0,after=0;if(insight_select_match(mask,source->length,at,&open,&after)!=1 || open!=at) goto done;
 at=after;while(at<source->length && (mask[at]==' ' || mask[at]=='\t')) at++;
 if(at<source->length && mask[at]!='\n' && mask[at]!='\r') goto done;
 lookup=insight_native_python_binding_scope(index,path,scope,constructor,source);if(!lookup) goto done;
 /* A source import has its own module path semantics; do not reinterpret it
  * as a same-name class in this file or elsewhere in the project. */
 GHashTable *imports=g_hash_table_lookup(bindings->imports,lookup);if(imports && g_hash_table_contains(imports,constructor)) goto done;
 const InsightSymbol *klass=NULL;gint rank=-1;gboolean ambiguous=FALSE;
 for(gsize i=0;i<insight_index_count(index);i++) { const InsightSymbol *tag=insight_index_at(index,i);if(strcmp(tag->name,constructor) || (eligible && !eligible(tag,data))) continue;gint candidate=insight_native_python_rank(index,tag,path,lookup,variable->line);
  if(candidate>rank) { klass=tag;rank=candidate;ambiguous=FALSE; }else if(candidate>=0 && candidate==rank) ambiguous=TRUE;
 }
 if(klass && !ambiguous && !strcmp(klass->kind,"c")) owner=python_symbol_scope(klass);
done:
 g_hash_table_insert(bindings->constructors,key,g_strdup(owner ? owner : ""));g_free(scope);g_free(lookup);g_free(constructor);return owner;
}
gchar *insight_native_python_owner(const InsightIndex *index,const gchar *path,const gchar *scope,gulong line,const InsightNativeSource *source,gsize position,gboolean *member,InsightTypeEligible eligible,void *data) {
 *member=FALSE;if(!source || !source->text || !source->mask || position>source->length) return NULL;
 const gchar *mask=source->mask;gsize at=position;while(at && ident(mask[at-1])) at--;while(at && g_ascii_isspace(mask[at-1])) at--;if(!at || mask[at-1]!='.') return NULL;
 *member=TRUE;at--;while(at && g_ascii_isspace(mask[at-1])) at--;gsize end=at;while(at && ident(mask[at-1])) at--;if(at==end) return NULL;
 /* Chained/indexed/call expressions require a richer resolver. */
 gsize prefix=at;while(prefix && (mask[prefix-1]==' ' || mask[prefix-1]=='\t')) prefix--;if(prefix && (mask[prefix-1]=='.' || mask[prefix-1]==')' || mask[prefix-1]==']')) return NULL;
 gchar *receiver=g_strndup(mask+at,end-at),*owner=NULL;
 /* Select the visible binding before interpreting a conventional receiver. A
  * closure may capture self, but a nearer parameter/local must hide it. Python
  * local assignments bind the whole function, even before their source line. */
 InsightNativePythonBindings *owned=NULL,*bindings=source->python;if(!bindings) bindings=owned=insight_native_python_bindings_new(index,path,source);InsightNativeSource cached=*source;cached.python=bindings;
 gchar *lookup=insight_native_python_binding_scope(index,path,scope,receiver,&cached);if(!lookup) { insight_native_python_bindings_free(owned);g_free(receiver);return NULL; }
 const InsightSymbol *best=NULL;gint best_rank=-1;
 for(gsize i=0;i<insight_index_count(index);i++) { const InsightSymbol *tag=insight_index_at(index,i);if(strcmp(tag->name,receiver) || (eligible && !eligible(tag,data))) continue;gint rank=insight_native_python_rank(index,tag,path,lookup,line);if(rank>best_rank) { best=tag;best_rank=rank; } }
 if(best && !strcmp(best->kind,"z") && (!strcmp(receiver,"self") || !strcmp(receiver,"cls"))) {
  gchar *binding=python_scope_name(best->scope);
  for(gsize i=0;i<insight_index_count(index);i++) {
   const InsightSymbol *tag=insight_index_at(index,i);if(g_strcmp0(tag->path,path) || (strcmp(tag->kind,"m") && strcmp(tag->kind,"f")) || !*tag->scope || (eligible && !eligible(tag,data))) continue;
   gchar *name=python_symbol_scope(tag);gboolean same=!strcmp(name,binding);g_free(name);if(!same) continue;
   const gchar *first=tag->signature;while(*first && (*first=='(' || g_ascii_isspace(*first))) first++;gsize n=strlen(receiver);
   if(!g_hash_table_contains(bindings->static_methods,binding) && !strncmp(first,receiver,n) && !ident(first[n])) {
    /* A nested ordinary function with a parameter named self is not a method. */
    for(gsize j=0;j<insight_index_count(index);j++) { const InsightSymbol *klass=insight_index_at(index,j);if(strcmp(klass->kind,"c") || g_strcmp0(klass->path,path) || (eligible && !eligible(klass,data))) continue;gchar *class_name=python_symbol_scope(klass),*parent=python_scope_name(tag->scope);if(!strcmp(class_name,parent)) owner=g_strdup(parent);g_free(parent);g_free(class_name);if(owner) break; }
   }
   break;
  }
  g_free(binding);
 }
 /* A visible annotated variable or a directly named class is nominal. */
 if(!owner && best && !strcmp(best->kind,"c")) owner=python_symbol_scope(best);
 else if(!owner && best && *best->type) { InsightTypeResult resolved=insight_type_resolve(index,best->type,best->scope,path,eligible,data);owner=resolved.owner;resolved.owner=NULL;insight_type_result_clear(&resolved); }
 if(!owner && best && !*best->type) owner=python_constructor_owner(index,path,best,&cached,bindings,eligible,data);
 insight_native_python_bindings_free(owned);g_free(lookup);g_free(receiver);return owner;
}

/* Explicit imports never authorize searching every file for a bare name. */
static gchar *python_module_path(const InsightIndex *index,const gchar *path,const gchar *module) {
 guint dots=0;while(module[dots]=='.') dots++;const gchar *tail=module+dots;
 gchar *relative=g_strdup(tail);for(gchar *p=relative;*p;p++) if(*p=='.') *p=G_DIR_SEPARATOR;
 gchar *stem=NULL;if(dots) { gchar *base=g_path_get_dirname(path);for(guint n=1;n<dots;n++) { gchar *parent=g_path_get_dirname(base);if(!strcmp(parent,base)) { g_free(parent);g_free(base);g_free(relative);return NULL; }g_free(base);base=parent; }stem=*relative ? g_build_filename(base,relative,NULL) : g_strdup(base);g_free(base); }
 else stem=g_strdup(relative);
 gchar *file=g_strconcat(stem,".py",NULL),*package=g_build_filename(stem,"__init__.py",NULL),*chosen=NULL;gboolean ambiguous=FALSE;
 for(gsize i=0;i<insight_index_count(index);i++) {
  const InsightSymbol *tag=insight_index_at(index,i);
  const gchar *candidate=tag->path;gboolean matches=dots ? ((*relative && !strcmp(candidate,file)) || !strcmp(candidate,package)) :
   ((g_str_has_suffix(candidate,file) && strlen(candidate)>strlen(file) && G_IS_DIR_SEPARATOR(candidate[strlen(candidate)-strlen(file)-1])) ||
    (g_str_has_suffix(candidate,package) && strlen(candidate)>strlen(package) && G_IS_DIR_SEPARATOR(candidate[strlen(candidate)-strlen(package)-1])));
  if(!matches) continue;
  if(chosen && strcmp(chosen,candidate)) { ambiguous=TRUE;break; }if(!chosen) chosen=g_strdup(candidate);
 }
 if(ambiguous) g_clear_pointer(&chosen,g_free);
 g_free(file);g_free(package);g_free(stem);g_free(relative);return chosen;
}
typedef struct { const gchar *path;InsightTypeEligible eligible;void *data; } PythonImportFilter;
static int python_import_eligible(const InsightSymbol *tag,void *data) {
 PythonImportFilter *filter=data;return !g_strcmp0(tag->path,filter->path) && (!filter->eligible || filter->eligible(tag,filter->data));
}
gsize insight_native_python_import_query(const InsightIndex *index,const gchar *path,const gchar *scope,const InsightNativeSource *source,gsize position,const gchar *word,InsightTypeEligible eligible,void *data,const InsightSymbol **out,gsize capacity,gboolean *imported) {
 *imported=FALSE;if(!index || !source || !source->mask || position>source->length || !word || !*word || (capacity && !out)) return 0;
 InsightNativePythonBindings *owned=NULL,*bindings=source->python;if(!bindings) bindings=owned=insight_native_python_bindings_new(index,path,source);
 if(!bindings->valid) { *imported=TRUE;insight_native_python_bindings_free(owned);return 0; }
 const gchar *mask=source->mask;gsize at=position;while(at && ident(mask[at-1])) at--;while(at && (mask[at-1]==' ' || mask[at-1]=='\t')) at--;
 gchar *receiver=NULL;gboolean member=at && mask[at-1]=='.';
 if(member) {
  at--;while(at && (mask[at-1]==' ' || mask[at-1]=='\t')) at--;gsize end=at;while(at && ident(mask[at-1])) at--;
  if(at==end) { insight_native_python_bindings_free(owned);return 0; }
  gsize prefix=at;while(prefix && (mask[prefix-1]==' ' || mask[prefix-1]=='\t')) prefix--;
  if(prefix && (mask[prefix-1]=='.' || mask[prefix-1]==')' || mask[prefix-1]==']')) { insight_native_python_bindings_free(owned);return 0; }
  receiver=g_strndup(mask+at,end-at);
 }
 const gchar *local=receiver ? receiver : word;if(!g_hash_table_contains(bindings->import_names,local)) { g_free(receiver);insight_native_python_bindings_free(owned);return 0; }
 InsightNativeSource cached=*source;cached.python=bindings;
 gchar *binding=insight_native_python_binding_scope(index,path,scope,local,&cached),*target=NULL;gsize count=0;
 GHashTable *imports=binding ? g_hash_table_lookup(bindings->imports,binding) : NULL;PythonImport *item=imports ? g_hash_table_lookup(imports,local) : NULL;
 /* A global/nonlocal declaration may redirect an import written in the
  * current suite. It must not accidentally reuse a different module alias. */
 if(binding) {
  gchar *walk=python_scope_name(scope);guint depth=0;
  while(*walk && strcmp(walk,binding)) {
   if(++depth>128) { *imported=TRUE;g_free(walk);goto done; }
   GHashTable *local_imports=g_hash_table_lookup(bindings->imports,walk);PythonImport *candidate=local_imports ? g_hash_table_lookup(local_imports,local) : NULL;
   if(candidate) { gchar *redirect=insight_native_python_binding_scope(index,path,walk,local,&cached);gboolean same=!g_strcmp0(redirect,binding);g_free(redirect);if(same) { item=candidate;break; } }
   gchar *parent=g_strrstr(walk,"::");if(parent) *parent=0;else *walk=0;
  }
  g_free(walk);
 }
 if(!item) goto done;
 /* Assignments/parameters in the same lexical scope override an import. No
  * execution order or dynamic type propagation is inferred. */
 for(gsize i=0;i<insight_index_count(index);i++) { const InsightSymbol *tag=insight_index_at(index,i);if(g_strcmp0(tag->path,path) || strcmp(tag->name,local) || !strcmp(tag->kind,"I") || !strcmp(tag->kind,"i") || !strcmp(tag->kind,"Y")) continue;gchar *owner=python_scope_name(tag->scope);gboolean shadow=!strcmp(owner,binding);g_free(owner);if(shadow) goto done; }
 *imported=TRUE;if(item->ambiguous) goto done;
 const gchar *cached_module=g_hash_table_lookup(bindings->modules,item->module);
 if(!cached_module) { gchar *resolved=python_module_path(index,path,item->module);g_hash_table_insert(bindings->modules,g_strdup(item->module),resolved ? resolved : g_strdup(""));cached_module=g_hash_table_lookup(bindings->modules,item->module); }
 if(!*cached_module) goto done;
 target=g_strdup(cached_module);
 const gchar *name=member && !*item->name ? word : item->name;if(!*name) goto done;
 if(member && *item->name) {
  /* Only an explicitly imported class is a nominal class receiver. */
  gboolean klass=FALSE;for(gsize i=0;i<insight_index_count(index);i++) { const InsightSymbol *tag=insight_index_at(index,i);if(!strcmp(tag->path,target) && !*tag->scope && !strcmp(tag->name,item->name) && !strcmp(tag->kind,"c")) { klass=TRUE;break; } }
  if(klass) { PythonImportFilter filter={target,eligible,data};count=insight_member_query(index,item->name,word,target,python_import_eligible,&filter,out,capacity,NULL,NULL); }
 }else for(gsize i=0;i<insight_index_count(index);i++) { const InsightSymbol *tag=insight_index_at(index,i);if(strcmp(tag->path,target) || *tag->scope || strcmp(tag->name,name) || (eligible && !eligible(tag,data)) || !strcmp(tag->kind,"I") || !strcmp(tag->kind,"i") || !strcmp(tag->kind,"Y")) continue;if(count<capacity) out[count]=tag;count++; }
done:
 g_free(target);g_free(binding);g_free(receiver);insight_native_python_bindings_free(owned);return count;
}
