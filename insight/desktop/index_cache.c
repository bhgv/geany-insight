/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "index_cache.h"
#include <stdlib.h>
static void format_error(GError **error,const char *message) {
 g_set_error_literal(error,g_quark_from_static_string("insight-index-cache"),1,message);
}
gboolean insight_cache_save_cancellable(const char *path,const InsightIndex *index,const char *root,GCancellable *cancel,GError **error) {
 if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) return FALSE;
 unsigned char *data=NULL; size_t length=0;
 if(!path || !insight_index_serialize(index,root,&data,&length)) {
  format_error(error,"Unable to serialize project index (invalid data or size limit)"); return FALSE;
 }
 if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) { free(data); return FALSE; }
 GFile *file=g_file_new_for_path(path);
 gboolean ok=g_file_replace_contents(file,(const char *)data,length,NULL,FALSE,
  G_FILE_CREATE_PRIVATE,NULL,cancel,error);
 g_object_unref(file); free(data); return ok;
}
gboolean insight_cache_save(const char *path,const InsightIndex *index,const char *root,GError **error) {
 return insight_cache_save_cancellable(path,index,root,NULL,error);
}
InsightIndex *insight_cache_load(const char *path,char **root,GError **error) {
 if(root) *root=NULL;
 if(!path || !root) { format_error(error,"Invalid cache arguments"); return NULL; }
 GFile *file=g_file_new_for_path(path);
 GFileInputStream *stream=g_file_read(file,NULL,error); g_object_unref(file);
 if(!stream) return NULL;
 GByteArray *bytes=g_byte_array_new(); unsigned char buffer[8192];
 gssize count;
 while((count=g_input_stream_read(G_INPUT_STREAM(stream),buffer,sizeof buffer,NULL,error))>0) {
  if((size_t)count>INSIGHT_INDEX_SNAPSHOT_LIMIT-bytes->len) {
   format_error(error,"Project index exceeds snapshot size limit"); count=-1; break;
  }
  g_byte_array_append(bytes,buffer,(guint)count);
 }
 g_object_unref(stream);
 InsightIndex *index=NULL;
 if(count==0) {
  index=insight_index_deserialize(bytes->data,bytes->len,root);
  if(!index) format_error(error,"Invalid, damaged or unsupported project index");
 }
 g_byte_array_free(bytes,TRUE); return index;
}
