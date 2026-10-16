/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "directory_operation.h"
#include "disk_operation.h"
#include <string.h>
#define ATTRS "standard::type,standard::is-symlink,standard::size,time::modified,time::modified-usec,id::file,etag::value"
static gboolean regular(GFileInfo *info) {
 return info && (!g_file_info_has_attribute(info,G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK) || !g_file_info_get_is_symlink(info)) && g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR;
}
static gboolean identity(GFileInfo *info) {
 const gchar *id=info ? g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE) : NULL;
 return id && *id && g_file_info_get_etag(info) && *g_file_info_get_etag(info);
}
static gboolean same(GFileInfo *a,GFileInfo *b) {
 return regular(b) && identity(a) && identity(b) &&
  !g_strcmp0(g_file_info_get_attribute_string(a,G_FILE_ATTRIBUTE_ID_FILE),g_file_info_get_attribute_string(b,G_FILE_ATTRIBUTE_ID_FILE)) &&
  !g_strcmp0(g_file_info_get_etag(a),g_file_info_get_etag(b)) && g_file_info_get_size(a)==g_file_info_get_size(b) &&
  g_file_info_get_attribute_uint64(a,G_FILE_ATTRIBUTE_TIME_MODIFIED)==g_file_info_get_attribute_uint64(b,G_FILE_ATTRIBUTE_TIME_MODIFIED) &&
  g_file_info_get_attribute_uint32(a,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC)==g_file_info_get_attribute_uint32(b,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
}
static gboolean verify(GFile *file,GFileInfo *expected,GCancellable *cancel,GError **error) {
 GError *failure=NULL;GFileInfo *current=g_file_query_info(file,ATTRS,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,&failure);
 if(!current) {
  if(!expected && g_error_matches(failure,G_IO_ERROR,G_IO_ERROR_NOT_FOUND)) { g_clear_error(&failure);return TRUE; }
  g_propagate_error(error,failure);return FALSE;
 }
 gboolean ok=expected && same(expected,current);g_object_unref(current);
 if(!ok) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"The selected file changed; refresh the directory comparison.");
 return ok;
}
static gboolean folder(GFile *file,GCancellable *cancel,GError **error) {
 GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,error);if(!info) return FALSE;
 gboolean ok=!g_file_info_get_is_symlink(info) && g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY;g_object_unref(info);
 if(!ok) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY,"File operations require existing parent directories without symbolic links.");
 return ok;
}
static gboolean parents(const gchar *root,const gchar *relative,GCancellable *cancel,GError **error) {
 GFile *base=g_file_new_for_path(root);gboolean ok=folder(base,cancel,error);gchar **parts=g_strsplit_set(relative,"/\\",-1);
 for(guint i=0;ok && parts[i] && parts[i+1];i++) { GFile *child=g_file_get_child(base,parts[i]);g_object_unref(base);base=child;ok=folder(base,cancel,error); }
 g_strfreev(parts);g_object_unref(base);return ok;
}
static gboolean valid(const gchar *const roots[2],const gchar *relative) {
 if(!roots || !relative || !*relative || strlen(relative)>16384 || g_path_is_absolute(relative) || !g_utf8_validate(relative,-1,NULL) || strpbrk(relative,"\r\n")) return FALSE;
 /* Reject either slash convention rather than interpreting Windows separators
  * as a harmless filename on one host and a traversal on another. */
 gchar **parts=g_strsplit_set(relative,"/\\",-1);gboolean ok=TRUE;
 for(guint i=0;parts[i];i++) if(!*parts[i] || !strcmp(parts[i],".") || !strcmp(parts[i],"..") || strchr(parts[i],':')) ok=FALSE;
 g_strfreev(parts);
#ifndef G_OS_WIN32
 if(strchr(relative,'\\')) ok=FALSE;
#endif
 for(gint i=0;i<2;i++) if(!roots[i] || !g_path_is_absolute(roots[i]) || strlen(roots[i])>16384 || !g_utf8_validate(roots[i],-1,NULL) || strpbrk(roots[i],"\r\n")) ok=FALSE;
 return ok;
}
gboolean insight_directory_operation(const gchar *const roots[2],const gchar *relative,GFileInfo *const info[2],guint action,GCancellable *cancel,GError **error) {
 guint side=action%2;gboolean trash=action>=INSIGHT_DIR_TRASH_L;
 if(action>INSIGHT_DIR_TRASH_R || !valid(roots,relative) || !info || !regular(info[side]) || !identity(info[side]) || (!trash && info[1-side] && (!regular(info[1-side]) || !identity(info[1-side])))) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Choose an identifiable regular file from a fresh directory comparison.");return FALSE;
 }
 if(g_cancellable_set_error_if_cancelled(cancel,error)) return FALSE;
 gchar *paths[2]={g_build_filename(roots[side],relative,NULL),g_build_filename(roots[1-side],relative,NULL)};
 GFile *from=g_file_new_for_path(paths[0]),*to=g_file_new_for_path(paths[1]),*temporary=NULL;GFileInputStream *input=NULL;GFileOutputStream *output=NULL;gchar *bytes=NULL;gboolean ok=FALSE,created=FALSE;
 if(!parents(roots[side],relative,cancel,error) || !verify(from,info[side],cancel,error)) goto done;
 if(trash) { ok=insight_disk_operation(paths[0],NULL,g_file_info_get_attribute_string(info[side],G_FILE_ATTRIBUTE_ID_FILE),g_file_info_get_etag(info[side]),TRUE,cancel,error);goto done; }
 if(g_file_equal(from,to) || (info[1-side] && !g_strcmp0(g_file_info_get_attribute_string(info[side],G_FILE_ATTRIBUTE_ID_FILE),g_file_info_get_attribute_string(info[1-side],G_FILE_ATTRIBUTE_ID_FILE)))) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"The comparison sides refer to the same file.");goto done;
 }
 goffset size=g_file_info_get_size(info[side]);
 if(size<0 || size>64*1024*1024) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Directory copy is limited to 64 MiB per file.");goto done; }
 if(!parents(roots[1-side],relative,cancel,error) || !verify(to,info[1-side],cancel,error)) goto done;
 input=g_file_read(from,cancel,error);if(!input) goto done;
 GFileInfo *opened=g_file_input_stream_query_info(input,ATTRS,cancel,error);if(!opened) goto done;
 gboolean unchanged=same(info[side],opened);g_object_unref(opened);
 if(!unchanged) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"The selected file changed; refresh the directory comparison.");goto done; }
 bytes=g_malloc((gsize)size+1);gsize length=0;
 if(!g_input_stream_read_all(G_INPUT_STREAM(input),bytes,(gsize)size+1,&length,cancel,error)) goto done;
 if(length!=(gsize)size) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"The selected file changed; refresh the directory comparison.");goto done; }
 if(!parents(roots[side],relative,cancel,error) || !parents(roots[1-side],relative,cancel,error) || !verify(from,info[side],cancel,error) || !verify(to,info[1-side],cancel,error)) goto done;
 if(info[1-side]) {
  ok=g_file_replace_contents(to,bytes,length,g_file_info_get_etag(info[1-side]),FALSE,G_FILE_CREATE_PRIVATE|G_FILE_CREATE_REPLACE_DESTINATION,NULL,cancel,error);
 } else {
  GFile *parent=g_file_get_parent(to);gchar *uuid=g_uuid_string_random(),*name=g_strconcat(".insight-copy-",uuid,".tmp",NULL);temporary=g_file_get_child(parent,name);g_free(name);g_free(uuid);g_object_unref(parent);
  output=g_file_create(temporary,G_FILE_CREATE_PRIVATE,cancel,error);if(!output) goto done;
  created=TRUE;
  if(!g_output_stream_write_all(G_OUTPUT_STREAM(output),bytes,length,NULL,cancel,error) || !g_output_stream_close(G_OUTPUT_STREAM(output),cancel,error)) goto done;
  if(!parents(roots[side],relative,cancel,error) || !verify(from,info[side],cancel,error) || !parents(roots[1-side],relative,cancel,error) || !verify(to,NULL,cancel,error)) goto done;
  ok=g_file_move(temporary,to,G_FILE_COPY_NOFOLLOW_SYMLINKS|G_FILE_COPY_NO_FALLBACK_FOR_MOVE,cancel,NULL,NULL,error);
 }
done:
 g_clear_object(&input);g_clear_object(&output);
 if(temporary) { if(created && !ok) g_file_delete(temporary,NULL,NULL);g_object_unref(temporary); }
 g_free(bytes);g_object_unref(from);g_object_unref(to);g_free(paths[0]);g_free(paths[1]);return ok;
}
