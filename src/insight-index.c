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

gboolean insight_native_parse(InsightIndex *index,TMSourceFile *file,const char *path,const char *text,gsize length) {
 if(!index || !file || !file->file_name || file->lang<0 || (guint)file->lang>=tm_ctags_get_lang_count() || !path || !*path || !g_utf8_validate(path,-1,NULL) || length>2u*1024u*1024u || (length && (!text || memchr(text,0,length) || !g_utf8_validate(text,(gssize)length,NULL)))) return FALSE;
 /* tm_source_file_parse currently fills tags but returns FALSE for nonempty
  * sources. Use the actual tag array, not that obsolete retry result. */
 tm_source_file_parse(file,(guchar *)text,length,TRUE);
 return insight_native_import(index,file,path);
}
