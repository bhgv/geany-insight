/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relation_query.h"
#include "type_resolve.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
static int owns_scope(const char *scope,const char *owner) {
 if(!scope || !owner) return 0;
 size_t n=strlen(scope),length=strlen(owner);
 if(n<length || strcmp(scope+n-length,owner)) return 0;
 if(n==length) return 1;
 size_t before=n-length;
 return scope[before-1]=='.' || (before>=2 && scope[before-1]==':' && scope[before-2]==':');
}
static gboolean class_symbol(const InsightSymbol *symbol) {
 const char *kind=symbol->kind;
 return !strcmp(kind,"c") || !strcmp(kind,"s") || !strcmp(kind,"i") || !strcmp(kind,"t") || !strcmp(kind,"class") || !strcmp(kind,"struct") || !strcmp(kind,"interface") || !strcmp(kind,"typedef") || !strcmp(kind,"alias") || !strcmp(kind,"u") || !strcmp(kind,"g") || !strcmp(kind,"n") || !strcmp(kind,"union") || !strcmp(kind,"enum") || !strcmp(kind,"namespace");
}
static gboolean qualified_name(const InsightSymbol *symbol,const char *name) {
 while(g_str_has_prefix(name,"::")) name+=2;
 if(!strcmp(symbol->name,name)) return TRUE;
 gchar *cpp=g_strconcat(symbol->scope,"::",symbol->name,NULL),*dot=g_strconcat(symbol->scope,".",symbol->name,NULL);
 gboolean match=!strcmp(cpp,name) || !strcmp(dot,name);g_free(cpp);g_free(dot);return match;
}
static void add_base(GHashTable *names,const char *start,gsize length,gboolean global) {
 gchar *part=g_strndup(start,length);g_strstrip(part);
 /* Access specifiers precede nominal C++ bases; keep whitespace around ::
  * and balanced template arguments intact for the common type normalizer. */
 const gchar *base=part;
 for(;;) {
  while(g_ascii_isspace(*base)) base++;
  const gchar *end=base;while(g_ascii_isalpha(*end)) end++;
  gsize size=end-base;
  if(g_ascii_isspace(*end) && ((size==6 && !memcmp(base,"public",6)) || (size==7 && !memcmp(base,"private",7)) || (size==9 && !memcmp(base,"protected",9)) || (size==7 && !memcmp(base,"virtual",7)))) base=end;
  else break;
 }
 gboolean absolute=FALSE;gchar *name=insight_type_name(base,&absolute);
 if(name) { if(global && absolute) { gchar *full=g_strconcat("::",name,NULL);g_free(name);name=full; }g_hash_table_add(names,name); }
 g_free(part);
}
static GHashTable *base_names_full(const char *inheritance,gboolean global) {
 GHashTable *names=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 if(!inheritance || strlen(inheritance)>65536) return names;
 const char *start=inheritance;guint depth=0;
 for(const char *p=start;;p++) {
  if(*p=='<' || *p=='(' || *p=='[') depth++;
  else if((*p=='>' || *p==')' || *p==']') && depth) depth--;
  if(!*p || (*p==',' && !depth)) { add_base(names,start,(gsize)(p-start),global);start=p+1;if(!*p) break; }
 }
 return names;
}
static GHashTable *base_names(const char *inheritance) { return base_names_full(inheritance,FALSE); }
static gboolean known_base(const InsightSymbol *symbol,GHashTable *names) {
 if(g_hash_table_contains(names,symbol->name)) return TRUE;
 gchar *cpp=g_strconcat(symbol->scope,"::",symbol->name,NULL),*dot=g_strconcat(symbol->scope,".",symbol->name,NULL);
 gboolean found=g_hash_table_contains(names,cpp) || g_hash_table_contains(names,dot);g_free(cpp);g_free(dot);return found;
}
static gboolean inherits_owner(const InsightSymbol *symbol,const char *owner) {
 if(!symbol->inheritance || !*symbol->inheritance) return FALSE;
 GHashTable *names=base_names(symbol->inheritance); GHashTableIter iter;gpointer key;gboolean found=FALSE;
 g_hash_table_iter_init(&iter,names);
 while(!found && g_hash_table_iter_next(&iter,&key,NULL)) {
  const char *base=key,*tail=base;
  for(const char *p=base;*p;p++) if(*p==':' || *p=='.') tail=p+1;
  if(!strchr(owner,':') && !strchr(owner,'.')) found=!strcmp(tail,owner);
  else { gchar *cpp=g_strconcat(symbol->scope,"::",base,NULL),*dot=g_strconcat(symbol->scope,".",base,NULL);found=!strcmp(base,owner) || !strcmp(cpp,owner) || !strcmp(dot,owner);g_free(cpp);g_free(dot); }
 }
 g_hash_table_unref(names);return found;
}
size_t insight_relation_query(const InsightIndex *index,const char *owner,InsightRelationMode mode,size_t offset,const InsightSymbol **out,size_t capacity) {
 if(!index || !owner || !*owner || strlen(owner)>1024 || (mode<INSIGHT_RELATION_DEFINITIONS || mode>INSIGHT_RELATION_DERIVED) || (capacity && !out)) return SIZE_MAX;
 while(g_str_has_prefix(owner,"::")) owner+=2;
 if(!*owner) return SIZE_MAX;
 if(mode==INSIGHT_RELATION_DEFINITIONS) return insight_index_query(index,owner,1,NULL,NULL,NULL,offset,out,capacity);
 size_t total=0,count=insight_index_count(index);
 if(!count) return 0;
 if(count>SIZE_MAX/sizeof(const InsightSymbol *)) return SIZE_MAX;
 const InsightSymbol **candidates=malloc(count*sizeof *candidates);
 if(!candidates) return SIZE_MAX;
 /* Query once: paginating index_query repeatedly would rescan the whole
  * project to compute its total, causing quadratic work on large projects. */
 size_t found=insight_index_query(index,"",0,mode==INSIGHT_RELATION_MEMBERS ? owner : NULL,NULL,NULL,0,candidates,count);
 if(found==SIZE_MAX || found>count) { free(candidates); return SIZE_MAX; }
 GHashTable *bases=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 if(mode==INSIGHT_RELATION_BASES) for(size_t i=0;i<found;i++) if(class_symbol(candidates[i]) && qualified_name(candidates[i],owner)) {
  GHashTable *names=base_names(candidates[i]->inheritance);GHashTableIter iter;gpointer key;g_hash_table_iter_init(&iter,names);while(g_hash_table_iter_next(&iter,&key,NULL)) g_hash_table_add(bases,g_strdup(key));g_hash_table_unref(names);
 }
 for(size_t i=0;i<found;i++) {
  const InsightSymbol *symbol=candidates[i];
  gboolean match=mode==INSIGHT_RELATION_MEMBERS ? owns_scope(symbol->scope,owner) : (class_symbol(symbol) && (mode==INSIGHT_RELATION_BASES ? known_base(symbol,bases) : inherits_owner(symbol,owner)));
  if(!match) continue;
  if(total>=offset && total-offset<capacity) out[total-offset]=symbol;
  total++;
 }
 g_hash_table_unref(bases); free(candidates);
 return total;
}

/* Canonical tag names, shared by C++ (::) and dot-scoped parsers. */
static gchar *canonical(const gchar *name) {
 GString *s=g_string_new(NULL);
 while(g_str_has_prefix(name,"::")) name+=2;
 for(;*name;name++) { if(name[0]==':' && name[1]==':') { g_string_append_c(s,'.');name++; } else g_string_append_c(s,*name); }
 return g_string_free(s,FALSE);
}
static gchar *class_name(const InsightSymbol *s) {
 gchar *raw=*s->scope ? g_strconcat(s->scope,".",s->name,NULL) : g_strdup(s->name);
 gchar *name=canonical(raw);g_free(raw);return name;
}
typedef struct { const InsightSymbol *symbol; gchar *name; GPtrArray *bases,*derived; guint distance; gboolean ambiguous,limited; } RelationClass;
static void relation_class_free(gpointer data) {
 RelationClass *c=data;g_free(c->name);g_ptr_array_unref(c->bases);g_ptr_array_unref(c->derived);g_free(c);
}
static void map_class(GHashTable *map,const gchar *key,RelationClass *c) {
 GPtrArray *values=g_hash_table_lookup(map,key);
 if(!values) { values=g_ptr_array_new();g_hash_table_insert(map,g_strdup(key),values); }
 g_ptr_array_add(values,c);
}
static RelationClass *unique_class(GPtrArray *values,gboolean *ambiguous) {
 if(!values) return NULL;
 if(values->len==1) return g_ptr_array_index(values,0);
 *ambiguous=TRUE;return NULL;
}
static RelationClass *resolve_type(const InsightIndex *index,GHashTable *symbols,const gchar *name,const gchar *scope,const gchar *path,InsightRelationEligible eligible,void *data,gboolean *ambiguous,gboolean *limited) {
 InsightTypeResult type=insight_type_resolve(index,name,scope,path,eligible,data);
 RelationClass *result=type.status==INSIGHT_TYPE_FOUND ? g_hash_table_lookup(symbols,type.symbol) : NULL;
 *ambiguous|=type.status==INSIGHT_TYPE_AMBIGUOUS || type.status==INSIGHT_TYPE_CYCLE;
 *limited|=type.status==INSIGHT_TYPE_LIMIT;
 insight_type_result_clear(&type);return result;
}
size_t insight_relation_query_transitive(const InsightIndex *index,const char *owner,InsightRelationMode mode,
 unsigned max_depth,InsightRelationEligible eligible,void *data,size_t offset,const InsightSymbol **out,size_t capacity,
 int *limited,int *ambiguous) {
 if(limited) *limited=0;
 if(ambiguous) *ambiguous=0;
 if(!index || !owner || !*owner || strlen(owner)>1024 || max_depth>32 || mode<INSIGHT_RELATION_MEMBERS || mode>INSIGHT_RELATION_DERIVED || (capacity && !out)) return SIZE_MAX;
 GPtrArray *classes=g_ptr_array_new_with_free_func(relation_class_free);
 GHashTable *names=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_ptr_array_unref);
 GHashTable *short_names=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_ptr_array_unref);
 GHashTable *symbols=g_hash_table_new(g_direct_hash,g_direct_equal);
 gboolean unclear=FALSE,truncated=FALSE;size_t count=insight_index_count(index),total=0;
 for(size_t i=0;i<count;i++) {
  const InsightSymbol *s=insight_index_at(index,i);
  if(!class_symbol(s) || (eligible && !eligible(s,data))) continue;
  RelationClass *c=g_new0(RelationClass,1);c->symbol=s;c->name=class_name(s);c->distance=G_MAXUINT;
  c->bases=g_ptr_array_new();c->derived=g_ptr_array_new();g_ptr_array_add(classes,c);map_class(names,c->name,c);map_class(short_names,s->name,c);g_hash_table_insert(symbols,(gpointer)s,c);
 }
 for(guint i=0;i<classes->len;i++) {
  RelationClass *c=g_ptr_array_index(classes,i);GHashTable *bases=base_names_full(c->symbol->inheritance,TRUE);
  GHashTableIter iter;gpointer key;g_hash_table_iter_init(&iter,bases);
  while(g_hash_table_iter_next(&iter,&key,NULL)) {
   RelationClass *base=resolve_type(index,symbols,key,c->symbol->scope,c->symbol->path,eligible,data,&c->ambiguous,&c->limited);
   if(base && !g_ptr_array_find(c->bases,base,NULL)) { g_ptr_array_add(c->bases,base);g_ptr_array_add(base->derived,c); }
  }
  g_hash_table_unref(bases);
 }
 gchar *root_name=canonical(owner);
 GPtrArray *roots=g_hash_table_lookup(names,root_name);
 if(!roots && !strchr(root_name,'.')) roots=g_hash_table_lookup(short_names,root_name);
 RelationClass *root=unique_class(roots,&unclear);
 if(root) {
  gchar *full=*root->symbol->scope ? g_strconcat(root->symbol->scope,"::",root->symbol->name,NULL) : g_strdup(root->symbol->name);
  root=resolve_type(index,symbols,full,NULL,NULL,eligible,data,&unclear,&truncated);g_free(full);
 }
 GQueue queue=G_QUEUE_INIT;
 if(root) { root->distance=0;g_queue_push_tail(&queue,root); }
 while(!g_queue_is_empty(&queue)) {
  RelationClass *c=g_queue_pop_head(&queue);unclear|=c->ambiguous;truncated|=c->limited;GPtrArray *edges=mode==INSIGHT_RELATION_DERIVED ? c->derived : c->bases;
  for(guint i=0;i<edges->len;i++) {
   RelationClass *next=g_ptr_array_index(edges,i);if(next->distance!=G_MAXUINT) continue;
   if(c->distance==max_depth) continue;
   next->distance=c->distance+1;g_queue_push_tail(&queue,next);
  }
 }
 for(guint i=0;i<classes->len;i++) {
  RelationClass *c=g_ptr_array_index(classes,i);if(c->distance!=max_depth) continue;
  GPtrArray *edges=mode==INSIGHT_RELATION_DERIVED ? c->derived : c->bases;
  for(guint k=0;k<edges->len;k++) if(((RelationClass *)g_ptr_array_index(edges,k))->distance==G_MAXUINT) truncated=TRUE;
 }
 GHashTable *scopes=g_hash_table_new(g_str_hash,g_str_equal);
 for(guint i=0;i<classes->len;i++) { RelationClass *c=g_ptr_array_index(classes,i);if(c->distance!=G_MAXUINT) g_hash_table_add(scopes,c->name); }
 /* Ordinal output is deterministic, and pagination never changes the total. */
 for(size_t i=0;i<count;i++) {
  const InsightSymbol *s=insight_index_at(index,i);gboolean match=FALSE;
  if(eligible && !eligible(s,data)) continue;
  if(mode==INSIGHT_RELATION_MEMBERS) { gchar *scope=canonical(s->scope);match=g_hash_table_contains(scopes,scope);g_free(scope); }
  else if(class_symbol(s)) { gchar *name=class_name(s);gboolean ignored=FALSE;RelationClass *c=unique_class(g_hash_table_lookup(names,name),&ignored);match=c && c->symbol==s && c->distance!=G_MAXUINT && c!=root;g_free(name); }
  if(match) { if(total>=offset && total-offset<capacity) out[total-offset]=s;total++; }
 }
 if(limited) *limited=truncated;
 if(ambiguous) *ambiguous=unclear;
 g_hash_table_unref(symbols);g_hash_table_unref(scopes);g_free(root_name);g_hash_table_unref(names);g_hash_table_unref(short_names);g_ptr_array_unref(classes);return total;
}
