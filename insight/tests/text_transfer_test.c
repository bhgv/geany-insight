/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "text_transfer.h"
#include <glib/gstdio.h>
#include <string.h>
int main(void) {
 gchar *dir=g_dir_make_tmp("text-transfer-XXXXXX",NULL),*file=g_build_filename(dir,"text.txt",NULL),*bad=g_build_filename(dir,"bad.txt",NULL);g_assert_nonnull(dir);GError *error=NULL;gsize size;
 const gchar *sample="α\r\nbeta\nlast";g_assert_true(insight_text_file_write(file,sample,strlen(sample),&error));g_assert_no_error(error);gchar *text=insight_text_file_read(file,&size,&error);g_assert_no_error(error);g_assert_cmpuint(size,==,strlen(sample));g_assert_cmpstr(text,==,sample);g_free(text);
 g_assert_true(insight_text_file_write(file,"",0,&error));g_assert_no_error(error);text=insight_text_file_read(file,&size,&error);g_assert_cmpuint(size,==,0);g_assert_cmpstr(text,==,"");g_free(text);
 g_assert_true(g_file_set_contents(file,"\xef\xbb\xbf" "α\r\n",-1,&error));text=insight_text_file_read(file,&size,&error);g_assert_no_error(error);g_assert_cmpstr(text,==,"α\r\n");g_assert_cmpuint(size,==,4);g_free(text);
 g_assert_false(insight_text_file_write(file,"a\0b",3,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA);g_clear_error(&error);
 g_assert_false(insight_text_file_write(file,"\xff",1,&error));g_clear_error(&error);text=insight_text_file_read(file,&size,&error);g_assert_cmpstr(text,==,"α\r\n");g_free(text);
 g_assert_true(g_file_set_contents(bad,"a\0b",3,&error));g_assert_null(insight_text_file_read(bad,&size,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA);g_clear_error(&error);
 g_assert_true(g_file_set_contents(bad,"\xff",1,&error));g_assert_null(insight_text_file_read(bad,&size,&error));g_clear_error(&error);
 g_assert_null(insight_text_file_read(dir,&size,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NOT_REGULAR_FILE);g_clear_error(&error);
 GFile *link=g_file_new_for_path(bad);g_assert_true(g_file_delete(link,NULL,&error));
 if(g_file_make_symbolic_link(link,file,NULL,&error)) { g_assert_null(insight_text_file_read(bad,&size,&error));g_clear_error(&error);g_assert_false(insight_text_file_write(bad,"bad",3,&error));g_clear_error(&error);g_file_delete(link,NULL,NULL); } else g_clear_error(&error);g_object_unref(link);
 gchar *large=g_malloc0(INSIGHT_TEXT_TRANSFER_LIMIT+1);memset(large,'x',INSIGHT_TEXT_TRANSFER_LIMIT+1);g_assert_true(g_file_set_contents(bad,large,INSIGHT_TEXT_TRANSFER_LIMIT+1,&error));g_assert_null(insight_text_file_read(bad,&size,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE);g_clear_error(&error);g_assert_false(insight_text_file_write(file,large,INSIGHT_TEXT_TRANSFER_LIMIT+1,&error));g_clear_error(&error);g_free(large);
 GDir *listing=g_dir_open(dir,0,NULL);const gchar *name;while((name=g_dir_read_name(listing))) g_assert_false(g_str_has_prefix(name,".insight-export-"));g_dir_close(listing);
 g_remove(file);g_remove(bad);g_rmdir(dir);g_free(file);g_free(bad);g_free(dir);g_print("Text transfer: exact UTF-8/EOL, BOM, empty, binary/size/type guards and temporary cleanup passed\n");return 0;
}
