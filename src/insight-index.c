/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-index.h"
#include "tagmanager/tm_ctags.h"
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
  const char *signature=tag->arglist && g_utf8_validate(tag->arglist,-1,NULL) ? tag->arglist : "";
  const char *type=tag->var_type && g_utf8_validate(tag->var_type,-1,NULL) ? tag->var_type : "";
  const char *inheritance=tag->inheritance && g_utf8_validate(tag->inheritance,-1,NULL) ? tag->inheritance : "";
  symbols[used]=(InsightSymbol){tag->name,path,scope,kinds[used],tag->line,signature,type,inheritance}; used++;
 }
 size_t old=insight_index_query(index,"",FALSE,NULL,NULL,path,0,NULL,0);
 gboolean ok=insight_index_count(index)-old+used<=250000 && insight_index_update(index,path,symbols,used);
 g_free(kinds); g_free(symbols); return ok;
}
guint insight_native_definition_rank(const InsightSymbol *symbol,const char *path) {
 return (!g_strcmp0(symbol->path,path) ? 4u : 0u) + (!strcmp(symbol->kind,"f") ? 8u : 0u);
}

gint insight_native_definition_rank_at(const InsightSymbol *symbol,const char *path,const char *scope,gulong line) {
 if(!strcmp(symbol->kind,"l") || !strcmp(symbol->kind,"z")) {
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
static gchar *type_owner(const gchar *type) {
 if(!type || !*type) return NULL;
 gchar **words=g_strsplit_set(type," *&\t\r\n",-1); gchar *owner=NULL;
 for(guint n=0;words[n];n++) if(*words[n] && strcmp(words[n],"const") && strcmp(words[n],"volatile") && strcmp(words[n],"struct") && strcmp(words[n],"class") && strcmp(words[n],"union")) { g_free(owner); owner=g_strdup(words[n]); }
 g_strfreev(words); return owner;
}
gchar *insight_native_member_owner(const InsightIndex *index,const char *path,const char *scope,gulong line,const char *text,gsize position,gboolean *member) {
 *member=FALSE; if(!text || position>strlen(text)) return NULL;
 gsize at=position; while(at && identifier_byte(text[at-1])) at--; while(at && g_ascii_isspace(text[at-1])) at--;
 if(at>=2 && text[at-2]=='-' && text[at-1]=='>') at-=2;
 else if(at && text[at-1]=='.') at--;
 else return NULL;
 *member=TRUE; while(at && g_ascii_isspace(text[at-1])) at--; gsize end=at;
 while(at && identifier_byte(text[at-1])) at--;
 if(end==at) return NULL;
 gchar *receiver=g_strndup(text+at,end-at); const InsightSymbol *vars[512],*best=NULL;
 gsize count=MIN(insight_index_find(index,receiver,TRUE,vars,G_N_ELEMENTS(vars)),G_N_ELEMENTS(vars)); g_free(receiver);
 for(gsize n=0;n<count;n++) {
  const InsightSymbol *tag=vars[n]; if(strcmp(tag->kind,"l") && strcmp(tag->kind,"z") && strcmp(tag->kind,"v")) continue;
  gint rank=insight_native_definition_rank_at(tag,path,scope,line); if(rank<0 || !tag->type || !*tag->type) continue;
  if(!best || rank>insight_native_definition_rank_at(best,path,scope,line)) best=tag;
 }
 gchar *owner=best ? type_owner(best->type) : NULL;
 /* Resolve simple typedef chains, including aliases of named structs. */
 for(guint depth=0;owner && depth<8;depth++) {
  const InsightSymbol *aliases[64],*alias=NULL; gsize count=MIN(insight_index_find(index,owner,TRUE,aliases,G_N_ELEMENTS(aliases)),G_N_ELEMENTS(aliases));
  for(gsize n=0;n<count;n++) if(!strcmp(aliases[n]->kind,"t") && aliases[n]->type && *aliases[n]->type) { if(!alias || !g_strcmp0(aliases[n]->path,path)) alias=aliases[n]; }
  gchar *next=alias ? type_owner(alias->type) : NULL;
  if(!next || !strcmp(next,owner)) { g_free(next); break; } g_free(owner); owner=next;
 }
 return owner;
}
gboolean insight_native_member_matches(const InsightSymbol *symbol,const char *owner) {
 if(!owner || !*owner || !symbol->scope || !*symbol->scope || !strcmp(symbol->kind,"l") || !strcmp(symbol->kind,"z")) return FALSE;
 return !strcmp(symbol->scope,owner);
}

gboolean insight_native_parse(InsightIndex *index,TMSourceFile *file,const char *path,const char *text,gsize length) {
 if(!index || !file || !file->file_name || file->lang<0 || (guint)file->lang>=tm_ctags_get_lang_count() || !path || !*path || !g_utf8_validate(path,-1,NULL) || length>2u*1024u*1024u || (length && (!text || memchr(text,0,length) || !g_utf8_validate(text,(gssize)length,NULL)))) return FALSE;
 /* tm_source_file_parse currently fills tags but returns FALSE for nonempty
  * sources. Use the actual tag array, not that obsolete retry result. */
 tm_source_file_parse(file,(guchar *)text,length,TRUE);
 return insight_native_import(index,file,path);
}
