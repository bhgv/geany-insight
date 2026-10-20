/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "text_transfer.h"
#include <string.h>
#define ATTRS G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_ETAG_VALUE
static gboolean regular(GFileInfo *info,GError **error) {
 if(info && g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR) return TRUE;
 g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_REGULAR_FILE,"Choose a regular file, not a directory or symbolic link.");return FALSE;
}
static gboolean text_valid(const gchar *text,gsize size,GError **error) {
 if(size>INSIGHT_TEXT_TRANSFER_LIMIT) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Text transfer is limited to 8 MiB.");return FALSE; }
 if(!text || memchr(text,0,size) || !g_utf8_validate(text,size,NULL)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Text transfer requires UTF-8 without NUL bytes.");return FALSE; }return TRUE;
}
gchar *insight_text_file_read(const gchar *path,gsize *size,GError **error) {
 GFile *file=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(file,ATTRS,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);GFileInputStream *input=NULL;gchar *text=NULL;
 if(!info || !regular(info,error)) goto done;
 goffset expected=g_file_info_get_size(info);
 if(expected<0 || expected>INSIGHT_TEXT_TRANSFER_LIMIT) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Text transfer is limited to 8 MiB.");goto done; }
 input=g_file_read(file,NULL,error);if(!input) goto done;
 GFileInfo *opened=g_file_input_stream_query_info(input,ATTRS,NULL,error);if(!opened) goto done;
 gboolean same=g_file_info_get_file_type(opened)==G_FILE_TYPE_REGULAR && g_file_info_get_size(opened)==expected && !g_strcmp0(g_file_info_get_etag(info),g_file_info_get_etag(opened));g_object_unref(opened);
 if(!same) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"The selected file changed; choose it again.");goto done; }
 text=g_malloc((gsize)expected+2);gsize length=0;
 if(!g_input_stream_read_all(G_INPUT_STREAM(input),text,(gsize)expected+1,&length,NULL,error)) { g_clear_pointer(&text,g_free);goto done; }
 GFileInfo *after=g_file_input_stream_query_info(input,ATTRS,NULL,error);if(!after) { g_clear_pointer(&text,g_free);goto done; }
 same=length==(gsize)expected && !g_strcmp0(g_file_info_get_etag(info),g_file_info_get_etag(after));g_object_unref(after);
 if(!same) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"The selected file changed; choose it again.");g_clear_pointer(&text,g_free);goto done; }
 if(!text_valid(text,length,error)) { g_clear_pointer(&text,g_free);goto done; }
 if(length>=3 && !memcmp(text,"\xef\xbb\xbf",3)) { memmove(text,text+3,length-3);length-=3; }
 text[length]=0;if(size) *size=length;
done:g_clear_object(&input);g_clear_object(&info);g_object_unref(file);return text;
}
gboolean insight_text_file_write(const gchar *path,const gchar *text,gsize size,GError **error) {
 if(!text_valid(text,size,error)) return FALSE;
 GFile *file=g_file_new_for_path(path),*temporary=NULL;GFileOutputStream *output=NULL;gboolean ok=FALSE,created=FALSE;
 GError *query_error=NULL;GFileInfo *info=g_file_query_info(file,ATTRS,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,&query_error);
 if(!info && !g_error_matches(query_error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND)) { g_propagate_error(error,query_error);goto done; }g_clear_error(&query_error);
 if(info) {
  if(!regular(info,error)) goto done;
  const gchar *etag=g_file_info_get_etag(info);
  if(!etag || !*etag) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"The destination does not provide an etag for conditional replacement.");goto done; }
  ok=g_file_replace_contents(file,text,size,etag,FALSE,G_FILE_CREATE_PRIVATE|G_FILE_CREATE_REPLACE_DESTINATION,NULL,NULL,error);
 } else {
  GFile *parent=g_file_get_parent(file);if(!parent) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Choose a destination filename.");goto done; }
  gchar *uuid=g_uuid_string_random(),*name=g_strconcat(".insight-export-",uuid,".tmp",NULL);temporary=g_file_get_child(parent,name);g_free(uuid);g_free(name);g_object_unref(parent);
  output=g_file_create(temporary,G_FILE_CREATE_PRIVATE,NULL,error);if(!output) goto done;created=TRUE;
  if(!g_output_stream_write_all(G_OUTPUT_STREAM(output),text,size,NULL,NULL,error) || !g_output_stream_close(G_OUTPUT_STREAM(output),NULL,error)) goto done;
  ok=g_file_move(temporary,file,G_FILE_COPY_NOFOLLOW_SYMLINKS|G_FILE_COPY_NO_FALLBACK_FOR_MOVE,NULL,NULL,NULL,error);
 }
done:g_clear_object(&output);if(temporary && created && !ok) g_file_delete(temporary,NULL,NULL);g_clear_object(&temporary);g_clear_object(&info);g_object_unref(file);return ok;
}
