/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-index.h"
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
  symbols[used]=(InsightSymbol){tag->name,path,scope,kinds[used],tag->line}; used++;
 }
 size_t old=insight_index_query(index,"",FALSE,NULL,NULL,path,0,NULL,0);
 gboolean ok=insight_index_count(index)-old+used<=250000 && insight_index_update(index,path,symbols,used);
 g_free(kinds); g_free(symbols); return ok;
}
guint insight_native_definition_rank(const InsightSymbol *symbol,const char *path) {
 return (!g_strcmp0(symbol->path,path) ? 4u : 0u) + (!strcmp(symbol->kind,"f") ? 2u : 0u);
}
