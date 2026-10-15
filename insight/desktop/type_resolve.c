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
 const InsightSymbol *only=NULL,*local=NULL;guint total=0,local_count=0;gboolean all_namespaces=TRUE,any_namespace=FALSE;
 for(gsize i=0;i<count;i++) {
  const InsightSymbol *symbol=candidates[i];if(!type_symbol(symbol) || (eligible && !eligible(symbol,data))) continue;
  gchar *full=symbol_name(symbol);gboolean match=!strcmp(full,name);g_free(full);if(!match) continue;
  gboolean namespace_symbol=!strcmp(symbol->kind,"n") || !strcmp(symbol->kind,"namespace");all_namespaces&=namespace_symbol;any_namespace|=namespace_symbol;
  only=symbol;total++;if(path && !strcmp(path,symbol->path)) { local=symbol;local_count++; }
 }
 *exists=total!=0;
 if(total>1 && any_namespace && !all_namespaces) { *ambiguous=TRUE;return NULL; }
 if(total && all_namespaces) return local ? local : only;
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
typedef struct { const InsightIndex *index;InsightTypeEligible eligible;void *data;GHashTable *active,*owners;guint aliases,alias_work,visits; } Resolve;
static InsightTypeResult resolve_name(Resolve *,const gchar *,const gchar *,const gchar *,gboolean,guint);
static GPtrArray *inheritance_names(const gchar *text,InsightTypeStatus *status) {
 GPtrArray *names=g_ptr_array_new_with_free_func(g_free);if(!text || !*text) return names;
 if(strlen(text)>65536 || !g_utf8_validate(text,-1,NULL)) { *status=INSIGHT_TYPE_INVALID;goto fail; }
 const gchar *start=text;gchar stack[64];guint depth=0;
 for(const gchar *p=text;;p++) {
  if(*p=='<' || *p=='(' || *p=='[') { if(depth==sizeof stack) { *status=INSIGHT_TYPE_LIMIT;goto fail; }stack[depth++]=*p; }
  else if(*p=='>' || *p==')' || *p==']') { gchar expected=*p=='>' ? '<' : (*p==')' ? '(' : '[');if(!depth || stack[--depth]!=expected) { *status=INSIGHT_TYPE_INVALID;goto fail; } }
  if(!*p || (*p==',' && !depth)) {
   if(!*p && depth) { *status=INSIGHT_TYPE_INVALID;goto fail; }
   gchar *part=g_strndup(start,p-start);g_strstrip(part);const gchar *base=part;
   for(;;) { while(g_ascii_isspace(*base)) base++;const gchar *end=base;while(g_ascii_isalpha(*end)) end++;gsize size=end-base;
    if(g_ascii_isspace(*end) && ((size==6 && !memcmp(base,"public",6)) || (size==7 && !memcmp(base,"private",7)) || (size==9 && !memcmp(base,"protected",9)) || (size==7 && !memcmp(base,"virtual",7)))) base=end;else break;
   }
   gboolean global=FALSE;gchar *name=insight_type_name(base,&global);g_free(part);
   if(!name) { *status=INSIGHT_TYPE_INVALID;goto fail; }
   if(global) { gchar *absolute=g_strconcat("::",name,NULL);g_free(name);name=absolute; }g_ptr_array_add(names,name);
   if(names->len>256) { *status=INSIGHT_TYPE_LIMIT;goto fail; }
   if(!*p) break;
   start=p+1;
  }
 }
 return names;
 fail:g_ptr_array_unref(names);return NULL;
}
static gboolean record_type(const InsightSymbol *symbol) {
 const gchar *kind=symbol->kind;return !strcmp(kind,"c") || !strcmp(kind,"s") || !strcmp(kind,"i") || !strcmp(kind,"class") || !strcmp(kind,"struct") || !strcmp(kind,"interface");
}
static gboolean hidden_nested(Resolve *job,const gchar *qualified,const gchar *name) {
 gsize count=insight_index_find(job->index,name,TRUE,NULL,0);if(count>CANDIDATE_LIMIT) return TRUE;
 const InsightSymbol **rows=g_new(const InsightSymbol *,count);insight_index_find(job->index,name,TRUE,rows,count);gboolean hidden=FALSE;
 for(gsize i=0;i<count && !hidden;i++) { const InsightSymbol *row=rows[i];if(type_symbol(row) || (job->eligible && !job->eligible(row,job->data))) continue;gchar *full=symbol_name(row);hidden=!strcmp(full,qualified);g_free(full); }
 g_free(rows);return hidden;
}
static const InsightSymbol *nested_type(Resolve *job,const InsightSymbol *owner,const gchar *name,guint depth,InsightTypeStatus *status,gboolean *uncertain,const gchar *preferred_path) {
 *status=INSIGHT_TYPE_UNKNOWN;*uncertain=FALSE;
 if(depth>64 || ++job->visits>4096) { *status=INSIGHT_TYPE_LIMIT;*uncertain=TRUE;return NULL; }
 gchar *scope=symbol_name(owner),*qualified=g_strconcat(scope,"::",name,NULL);g_free(scope);
 gboolean namespace_owner=!strcmp(owner->kind,"n") || !strcmp(owner->kind,"namespace");
 const InsightSymbol *direct=lookup(job->index,qualified,"",namespace_owner && preferred_path ? preferred_path : owner->path,TRUE,job->eligible,job->data,status);
 gboolean hidden=!direct && *status==INSIGHT_TYPE_UNKNOWN && hidden_nested(job,qualified,name);g_free(qualified);
 if(direct) return direct;
 if(hidden || *status!=INSIGHT_TYPE_UNKNOWN) { *uncertain=TRUE;return NULL; }
 if(!record_type(owner) || !owner->inheritance || !*owner->inheritance) return NULL;
 if(g_hash_table_contains(job->owners,owner)) { *status=INSIGHT_TYPE_CYCLE;*uncertain=TRUE;return NULL; }
 GPtrArray *bases=inheritance_names(owner->inheritance,status);if(!bases) { *uncertain=TRUE;return NULL; }
 g_hash_table_add(job->owners,(gpointer)owner);const InsightSymbol *found=NULL;
 for(guint i=0;i<bases->len;i++) {
  guint before_aliases=job->aliases;const gchar *base=g_ptr_array_index(bases,i);gboolean global=g_str_has_prefix(base,"::");
  InsightTypeResult resolved=resolve_name(job,global ? base+2 : base,owner->scope,owner->path,global,depth+1);
  if(resolved.status!=INSIGHT_TYPE_FOUND || !record_type(resolved.symbol)) { *status=resolved.status==INSIGHT_TYPE_FOUND ? INSIGHT_TYPE_INVALID : resolved.status;*uncertain=TRUE;insight_type_result_clear(&resolved);found=NULL;break; }
  InsightTypeStatus branch_status;gboolean branch_uncertain=FALSE;const InsightSymbol *hit=nested_type(job,resolved.symbol,name,depth+1,&branch_status,&branch_uncertain,NULL);insight_type_result_clear(&resolved);
  if(branch_uncertain) { *status=branch_status;*uncertain=TRUE;found=NULL;break; }
  if(hit && found && hit!=found) { *status=INSIGHT_TYPE_AMBIGUOUS;*uncertain=TRUE;found=NULL;break; }
  if(hit) found=hit;else job->aliases=before_aliases;
 }
 g_hash_table_remove(job->owners,owner);g_ptr_array_unref(bases);return found;
}
static const InsightSymbol *lexical_type(Resolve *job,const gchar *name,const gchar *scope,const gchar *path,gboolean global,guint depth,InsightTypeStatus *status,gboolean *blocked) {
 gchar *context=global ? g_strdup("") : canonical_scope(scope);const InsightSymbol *symbol=NULL;*blocked=FALSE;
 for(guint level=0;;level++) {
  if(depth+level>64) { *status=INSIGHT_TYPE_LIMIT;*blocked=TRUE;break; }
  gchar *qualified=*context ? g_strconcat(context,"::",name,NULL) : g_strdup(name);
  symbol=lookup(job->index,qualified,"",path,TRUE,job->eligible,job->data,status);
  gboolean hidden=!symbol && *status==INSIGHT_TYPE_UNKNOWN && hidden_nested(job,qualified,name);g_free(qualified);
  if(symbol) break;
  if(hidden || *status!=INSIGHT_TYPE_UNKNOWN) { *blocked=TRUE;break; }
  if(*context) {
   InsightTypeStatus owner_status=INSIGHT_TYPE_UNKNOWN;const InsightSymbol *owner=lookup(job->index,context,"",path,TRUE,job->eligible,job->data,&owner_status);
   if(owner_status!=INSIGHT_TYPE_UNKNOWN) { *status=owner_status;*blocked=TRUE;break; }
   if(owner && !alias_symbol(owner)) { guint before_aliases=job->aliases;gboolean uncertain=FALSE;symbol=nested_type(job,owner,name,depth+level+1,status,&uncertain,path);if(!symbol) job->aliases=before_aliases;if(symbol || uncertain) { *blocked=uncertain;break; } }
  }
  gchar *last=g_strrstr(context,"::");if(last) *last=0;else if(*context) *context=0;else break;
 }
 g_free(context);return symbol;
}
static InsightTypeResult resolve_symbol(Resolve *job,const InsightSymbol *symbol,guint depth) {
 InsightTypeResult result={.status=INSIGHT_TYPE_UNKNOWN};
 if(depth>64) { result.status=INSIGHT_TYPE_LIMIT;return result; }
 if(!alias_symbol(symbol)) { result.status=INSIGHT_TYPE_FOUND;result.owner=symbol_name(symbol);result.symbol=symbol;return result; }
 if(g_hash_table_contains(job->active,symbol)) { result.status=INSIGHT_TYPE_CYCLE;return result; }
 if(job->alias_work>=ALIAS_LIMIT) { result.status=INSIGHT_TYPE_LIMIT;return result; }
 job->aliases++;job->alias_work++;g_hash_table_add(job->active,(gpointer)symbol);
 gboolean absolute=FALSE;gchar *target=insight_type_name(symbol->type,&absolute);
 if(target) {
  /* Alias-template parameters belong to the alias itself, while ordinary
   * target names still resolve in its enclosing declaration context. */
  gboolean dependent=FALSE;
  if(!absolute) { const gchar *end=strstr(target,"::");gchar *head=end ? g_strndup(target,end-target) : g_strdup(target),*owner=symbol_name(symbol),*qualified=g_strconcat(owner,"::",head,NULL);dependent=hidden_nested(job,qualified,head);g_free(qualified);g_free(owner);g_free(head); }
  if(!dependent) result=resolve_name(job,target,symbol->scope,symbol->path,absolute,depth+1);
 }
 else result.status=*symbol->type ? INSIGHT_TYPE_INVALID : INSIGHT_TYPE_UNKNOWN;
 g_free(target);g_hash_table_remove(job->active,symbol);return result;
}
static InsightTypeResult resolve_nested_name(Resolve *job,const InsightSymbol *owner,const gchar *name,const gchar *path,guint depth) {
 InsightTypeResult result={.status=INSIGHT_TYPE_UNKNOWN};if(depth>64) { result.status=INSIGHT_TYPE_LIMIT;return result; }
 const gchar *separator=strstr(name,"::");gchar *part=separator ? g_strndup(name,separator-name) : g_strdup(name);gboolean uncertain=FALSE;
 const InsightSymbol *symbol=nested_type(job,owner,part,depth,&result.status,&uncertain,path);g_free(part);if(!symbol) return result;
 result=resolve_symbol(job,symbol,depth);
 if(separator && result.status==INSIGHT_TYPE_FOUND) { InsightTypeResult next=resolve_nested_name(job,result.symbol,separator+2,path,depth+1);insight_type_result_clear(&result);result=next; }
 return result;
}
static InsightTypeResult resolve_name(Resolve *job,const gchar *name,const gchar *scope,const gchar *path,gboolean global,guint depth) {
 InsightTypeResult result={.status=INSIGHT_TYPE_UNKNOWN};if(depth>64) { result.status=INSIGHT_TYPE_LIMIT;return result; }
 const gchar *first=strstr(name,"::");gboolean blocked=FALSE;
 if(first) {
  /* Select the nearest first component before looking for its members. A
   * missing member cannot reopen lookup in a different outer owner. */
  gchar *head=g_strndup(name,first-name);const InsightSymbol *prefix=lexical_type(job,head,scope,path,global,depth,&result.status,&blocked);g_free(head);
  if(prefix) { InsightTypeResult owner=resolve_symbol(job,prefix,depth+1);if(owner.status==INSIGHT_TYPE_FOUND) { result=resolve_nested_name(job,owner.symbol,first+2,path,depth+1);insight_type_result_clear(&owner);return result; }return owner; }
  if(blocked || result.status!=INSIGHT_TYPE_UNKNOWN) return result;
 }
 const InsightSymbol *symbol=first ? lookup(job->index,name,scope,path,global,job->eligible,job->data,&result.status) : lexical_type(job,name,scope,path,global,depth,&result.status,&blocked);
 if(!symbol && first && result.status==INSIGHT_TYPE_UNKNOWN) {
  /* Partial snapshots may record qualified scopes without separate namespace
   * rows. Retain exact nominal lookup only when no visible head bound it. */
  const gchar *separator=g_strrstr(name,"::");gchar *prefix=g_strndup(name,separator-name);InsightTypeResult owner=resolve_name(job,prefix,scope,path,global,depth+1);g_free(prefix);
  if(owner.status==INSIGHT_TYPE_FOUND) { result=resolve_nested_name(job,owner.symbol,separator+2,path,depth+1);insight_type_result_clear(&owner);return result; }
  result.status=owner.status;insight_type_result_clear(&owner);
 }
 return symbol ? resolve_symbol(job,symbol,depth) : result;
}
InsightTypeResult insight_type_resolve(const InsightIndex *index,const gchar *type,const gchar *scope,const gchar *path,InsightTypeEligible eligible,void *data) {
 InsightTypeResult result={.status=INSIGHT_TYPE_INVALID};gboolean global=FALSE;gchar *name=insight_type_name(type,&global);
 if(!index || !name || (scope && (!g_utf8_validate(scope,-1,NULL) || strlen(scope)>65536))) { g_free(name);return result; }
 Resolve job={.index=index,.eligible=eligible,.data=data,.active=g_hash_table_new(g_direct_hash,g_direct_equal),.owners=g_hash_table_new(g_direct_hash,g_direct_equal)};
 result=resolve_name(&job,name,scope,path,global,0);result.aliases=job.aliases;
 g_hash_table_unref(job.active);g_hash_table_unref(job.owners);g_free(name);return result;
}
InsightTypeResult insight_type_resolve_return(const InsightIndex *index,const InsightSymbol *declaration,InsightTypeEligible eligible,void *data) {
 InsightTypeResult result={.status=INSIGHT_TYPE_INVALID};
 if(!index || !declaration || !declaration->name || !declaration->scope || !declaration->path) return result;
 gboolean global=FALSE;gchar *name=insight_type_name(declaration->type,&global);if(!name) return result;
 if(eligible && !eligible(declaration,data)) { g_free(name);result.status=INSIGHT_TYPE_UNKNOWN;return result; }
 if(!global) {
  const gchar *end=strstr(name,"::");gchar *head=end ? g_strndup(name,end-name) : g_strdup(name);gsize count=insight_index_find(index,head,TRUE,NULL,0);
  if(count>CANDIDATE_LIMIT) { g_free(head);g_free(name);result.status=INSIGHT_TYPE_LIMIT;return result; }
  const InsightSymbol **rows=g_new(const InsightSymbol *,count);insight_index_find(index,head,TRUE,rows,count);g_free(head);gchar *owner=symbol_name(declaration);gboolean dependent=FALSE;
  for(gsize i=0;i<count && !dependent;i++) { const InsightSymbol *row=rows[i];if(strcmp(row->kind,"Z") || strcmp(row->path,declaration->path) || (eligible && !eligible(row,data))) continue;gchar *scope=canonical_scope(row->scope);dependent=!strcmp(scope,owner);g_free(scope); }
  g_free(owner);g_free(rows);
  if(dependent) { g_free(name);result.status=INSIGHT_TYPE_UNKNOWN;return result; }
 }
 g_free(name);return insight_type_resolve(index,declaration->type,declaration->scope,declaration->path,eligible,data);
}
void insight_type_result_clear(InsightTypeResult *result) { if(!result) return;g_free(result->owner);memset(result,0,sizeof *result);result->status=INSIGHT_TYPE_UNKNOWN; }
