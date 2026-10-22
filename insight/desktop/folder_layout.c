/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "folder_layout.h"
#include "glib_compat.h"
#include <string.h>
gboolean insight_folder_layout_vertical(gboolean adaptive,gboolean manual_vertical,
 gint width,gint height,gboolean current_vertical) {
 if(!adaptive) return manual_vertical;
 if(width<=0 || height<=0) return current_vertical;
 if(width<320) return TRUE;
 /* Subtraction in 64 bits also accepts malformed/huge geometry safely. */
 gint64 difference=(gint64)width-height;
 return current_vertical ? difference<=24 : difference< -24;
}
GPtrArray *insight_folder_ancestry(const gchar *path,guint limit) {
 if(!path || !g_path_is_absolute(path) || strlen(path)>16384 ||
    !g_utf8_validate(path,-1,NULL) || strpbrk(path,"\r\n") || !limit) return NULL;
 GPtrArray *paths=g_ptr_array_new_with_free_func(g_free);gchar *current=insight_canonicalize_filename(path,NULL);
 limit=MIN(limit,64u);
 for(guint i=0;i<limit;i++) {
  g_ptr_array_add(paths,current);gchar *parent=g_path_get_dirname(current);
  if(!strcmp(parent,current)) { g_free(parent);break; }current=parent;
  if(i+1==limit) g_free(current);
 }
 for(guint i=0;i<paths->len/2;i++) { gpointer tmp=paths->pdata[i];paths->pdata[i]=paths->pdata[paths->len-i-1];paths->pdata[paths->len-i-1]=tmp; }
 return paths;
}
