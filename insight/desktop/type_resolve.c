/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "type_resolve.h"
#include <string.h>
#define ALIAS_LIMIT 32
#define CANDIDATE_LIMIT 4096
static gboolean identifier_start(guchar c) { return g_ascii_isalpha(c) || c=='_' || c=='$' || c>=128; }
static gboolean identifier(guchar c) { return identifier_start(c) || g_ascii_isdigit(c); }
static gchar *canonical_scope(const gchar *scope) {
 GString *out=g_string_new(NULL);if(!scope) return g_string_free(out,FALSE);
 while(g_str_has_prefix(scope,"::")) scope+=2;
 for(;*scope;scope++) { if(*scope=='.') g_string_append(out,"::");else g_string_append_c(out,*scope); }return g_string_free(out,FALSE);
}
static gboolean qualifier(const gchar *word) {
 static const char *words[]={"const","volatile","struct","class","union","enum","typename","restrict","__restrict","__restrict__"};
 for(guint i=0;i<G_N_ELEMENTS(words);i++) if(!strcmp(word,words[i])) return TRUE;
 return FALSE;
}
gchar *insight_type_name(const gchar *type,gboolean *global) {
 if(global) *global=FALSE;
 if(!type || !*type || strlen(type)>65536 || !g_utf8_validate(type,-1,NULL)) return NULL;
 GString *plain=g_string_new(NULL);guint angles=0,brackets=0;gboolean valid=TRUE;
 for(const gchar *p=type;*p;p++) {
  if(*p=='<' && !brackets) { if(++angles>64) { valid=FALSE;break; } }
  else if(*p=='>' && !brackets) { if(!angles) { valid=FALSE;break; }angles--; }
  else if(*p=='[') brackets++;
  else if(*p==']') { if(!brackets) { valid=FALSE;break; }brackets--; }
  else if(!angles && !brackets) { if(*p=='(' || *p==')' || *p=='=' || *p==';' || *p==',') { valid=FALSE;break; }g_string_append_c(plain,*p); }
 }
 if(angles || brackets) valid=FALSE;
 gchar **words=g_strsplit_set(plain->str," \t\r\n*&",-1);GString *name=g_string_new(NULL);
 for(guint i=0;valid && words[i];i++) {
  const gchar *word=words[i];if(!*word || qualifier(word)) continue;
  if(name->len && name->str[name->len-1]!=':' && name->str[name->len-1]!='.' && word[0]!=':' && word[0]!='.') { valid=FALSE;break; }
  g_string_append(name,word);
 }
 g_strfreev(words);g_string_free(plain,TRUE);gboolean absolute=g_str_has_prefix(name->str,"::");gchar *normalized=canonical_scope(name->str);g_string_free(name,TRUE);
 const gchar *p=normalized;if(!*p) valid=FALSE;
 while(valid && *p) {
  if(!identifier_start((guchar)*p)) { valid=FALSE;break; }p++;while(identifier((guchar)*p)) p++;
  if(!*p) break;
  if(p[0]!=':' || p[1]!=':' || !p[2]) { valid=FALSE;break; }p+=2;
 }
 if(!valid) { g_free(normalized);return NULL; }
 if(global) *global=absolute;
 return normalized;
}
static gboolean alias_symbol(const InsightSymbol *s) { return !strcmp(s->kind,"t") || !strcmp(s->kind,"typedef") || !strcmp(s->kind,"alias"); }
static gboolean type_symbol(const InsightSymbol *s) {
 static const char *kinds[]={"c","s","u","i","g","n","class","struct","union","interface","enum","namespace"};
 if(alias_symbol(s)) return TRUE;
 for(guint i=0;i<G_N_ELEMENTS(kinds);i++) if(!strcmp(s->kind,kinds[i])) return TRUE;
 return FALSE;
}
static gchar *symbol_name(const InsightSymbol *symbol) { gchar *scope=canonical_scope(symbol->scope),*name=*scope ? g_strconcat(scope,"::",symbol->name,NULL) : g_strdup(symbol->name);g_free(scope);return name; }
static const InsightSymbol *pick(const InsightSymbol **candidates,gsize count,const gchar *name,const gchar *path,InsightTypeEligible eligible,void *data,gboolean *exists,gboolean *ambiguous) {
 const InsightSymbol *only=NULL,*local=NULL;guint total=0,local_count=0;
 for(gsize i=0;i<count;i++) {
  const InsightSymbol *symbol=candidates[i];if(!type_symbol(symbol) || (eligible && !eligible(symbol,data))) continue;
  gchar *full=symbol_name(symbol);gboolean match=!strcmp(full,name);g_free(full);if(!match) continue;
  only=symbol;total++;if(path && !strcmp(path,symbol->path)) { local=symbol;local_count++; }
 }
 *exists=total!=0;
 if(local_count==1) return local;
 if(total==1) return only;
 if(total) *ambiguous=TRUE;
 return NULL;
}
static const InsightSymbol *lookup(const InsightIndex *index,const gchar *name,const gchar *scope,const gchar *path,gboolean global,InsightTypeEligible eligible,void *data,InsightTypeStatus *status) {
 const gchar *tail=g_strrstr(name,"::");tail=tail ? tail+2 : name;gsize count=insight_index_find(index,tail,TRUE,NULL,0);
 if(count>CANDIDATE_LIMIT) { *status=INSIGHT_TYPE_LIMIT;return NULL; }
 const InsightSymbol **candidates=g_new(const InsightSymbol *,count);insight_index_find(index,tail,TRUE,candidates,count);gchar *context=global ? g_strdup("") : canonical_scope(scope);const InsightSymbol *result=NULL;
 for(;;) {
  gchar *qualified=*context ? g_strconcat(context,"::",name,NULL) : g_strdup(name);gboolean exists=FALSE,ambiguous=FALSE;
  result=pick(candidates,count,qualified,path,eligible,data,&exists,&ambiguous);g_free(qualified);
  if(exists) { if(ambiguous) *status=INSIGHT_TYPE_AMBIGUOUS;break; }
  gchar *last=g_strrstr(context,"::");if(last) *last=0;else if(*context) *context=0;else break;
 }
 g_free(context);g_free(candidates);return result;
}
InsightTypeResult insight_type_resolve(const InsightIndex *index,const gchar *type,const gchar *scope,const gchar *path,InsightTypeEligible eligible,void *data) {
 InsightTypeResult result={.status=INSIGHT_TYPE_INVALID};gboolean global=FALSE;gchar *name=insight_type_name(type,&global);
 if(!index || !name || (scope && (!g_utf8_validate(scope,-1,NULL) || strlen(scope)>65536))) { g_free(name);return result; }
 gchar *context=g_strdup(scope ? scope : ""),*preferred=g_strdup(path);GHashTable *visited=g_hash_table_new(g_direct_hash,g_direct_equal);
 for(;;) {
  result.status=INSIGHT_TYPE_UNKNOWN;const InsightSymbol *symbol=lookup(index,name,context,preferred,global,eligible,data,&result.status);if(!symbol) break;
  if(!alias_symbol(symbol)) { result.status=INSIGHT_TYPE_FOUND;result.owner=symbol_name(symbol);result.symbol=symbol;break; }
  if(g_hash_table_contains(visited,symbol)) { result.status=INSIGHT_TYPE_CYCLE;break; }
  if(result.aliases>=ALIAS_LIMIT) { result.status=INSIGHT_TYPE_LIMIT;break; }
  g_hash_table_add(visited,(gpointer)symbol);result.aliases++;
  gchar *next=insight_type_name(symbol->type,&global);if(!next) { result.status=*symbol->type ? INSIGHT_TYPE_INVALID : INSIGHT_TYPE_UNKNOWN;break; }
  g_free(name);name=next;g_free(context);context=g_strdup(symbol->scope);g_free(preferred);preferred=g_strdup(symbol->path);
 }
 g_hash_table_unref(visited);g_free(name);g_free(context);g_free(preferred);return result;
}
void insight_type_result_clear(InsightTypeResult *result) { if(!result) return;g_free(result->owner);memset(result,0,sizeof *result);result->status=INSIGHT_TYPE_UNKNOWN; }
