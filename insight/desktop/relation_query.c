/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relation_query.h"
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
 return !strcmp(kind,"c") || !strcmp(kind,"s") || !strcmp(kind,"i") || !strcmp(kind,"t") || !strcmp(kind,"class") || !strcmp(kind,"struct") || !strcmp(kind,"interface") || !strcmp(kind,"typedef");
}
static gboolean qualified_name(const InsightSymbol *symbol,const char *name) {
 while(g_str_has_prefix(name,"::")) name+=2;
 if(!strcmp(symbol->name,name)) return TRUE;
 gchar *cpp=g_strconcat(symbol->scope,"::",symbol->name,NULL),*dot=g_strconcat(symbol->scope,".",symbol->name,NULL);
 gboolean match=!strcmp(cpp,name) || !strcmp(dot,name);g_free(cpp);g_free(dot);return match;
}
static void add_base(GHashTable *names,const char *start,gsize length) {
 gchar *part=g_strndup(start,length);g_strstrip(part);
 GString *plain=g_string_new(NULL);guint depth=0;
 for(const char *p=part;*p;p++) {
  if(*p=='<' || *p=='[' || *p=='(') depth++;
  else if((*p=='>' || *p==']' || *p==')') && depth) depth--;
  else if(!depth) g_string_append_c(plain,*p);
 }
 g_free(part);part=g_string_free(plain,FALSE);
 gchar **words=g_strsplit_set(part," \t\r\n",-1);const char *name=NULL;
 for(guint i=0;words[i];i++) if(*words[i]) name=words[i];
 if(name && !strchr(name,'=')) { while(g_str_has_prefix(name,"::")) name+=2; if(*name) g_hash_table_add(names,g_strdup(name)); }
 g_strfreev(words);g_free(part);
}
static GHashTable *base_names(const char *inheritance) {
 GHashTable *names=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 if(!inheritance || strlen(inheritance)>65536) return names;
 const char *start=inheritance;guint depth=0;
 for(const char *p=start;;p++) {
  if(*p=='<' || *p=='(' || *p=='[') depth++;
  else if((*p=='>' || *p==')' || *p==']') && depth) depth--;
  if(!*p || (*p==',' && !depth)) { add_base(names,start,(gsize)(p-start));start=p+1;if(!*p) break; }
 }
 return names;
}
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
