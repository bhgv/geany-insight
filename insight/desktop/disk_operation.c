/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "disk_operation.h"
#include <string.h>
static gboolean valid_path(const gchar *path) {
 return path && *path && strlen(path)<=16384 && g_path_is_absolute(path) &&
  g_utf8_validate(path,-1,NULL) && !strpbrk(path,"\r\n");
}
gboolean insight_disk_operation(const gchar *source,const gchar *destination,
 const gchar *file_id,const gchar *etag,gboolean trash,GCancellable *cancel,GError **error) {
 if(!valid_path(source) || (!trash && !valid_path(destination)) || !file_id || !*file_id) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Refresh the directory before changing this entry");return FALSE;
 }
 if(g_cancellable_set_error_if_cancelled(cancel,error)) return FALSE;
 GFile *from=g_file_new_for_path(source),*parent=g_file_get_parent(from);
 if(!parent) { g_object_unref(from);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Cannot change a filesystem root");return FALSE; }
 g_object_unref(parent);
 GFile *to=trash ? NULL : g_file_new_for_path(destination);
 if(to && (g_file_equal(from,to) || g_file_has_prefix(to,from))) {
  g_object_unref(to);g_object_unref(from);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Choose a different destination outside the source");return FALSE;
 }
 GFileInfo *info=g_file_query_info(from,INSIGHT_DISK_IDENTITY_ATTRIBUTES,
  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,error);
 gboolean ok=FALSE;
 if(info) {
  if(g_strcmp0(file_id,g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE)) ||
     (etag && g_strcmp0(etag,g_file_info_get_etag(info))))
   g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"The selected entry changed; refresh the directory");
  else if(trash) ok=g_file_trash(from,cancel,error);
  else ok=g_file_move(from,to,G_FILE_COPY_NOFOLLOW_SYMLINKS|G_FILE_COPY_NO_FALLBACK_FOR_MOVE,cancel,NULL,NULL,error);
  g_object_unref(info);
 }
 g_clear_object(&to);g_object_unref(from);return ok;
}
