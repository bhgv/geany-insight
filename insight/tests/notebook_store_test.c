#include "notebook_store.h"
#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
int main(void) {
 InsightNotes *n=insight_notes_new(); GError *e=NULL;
 g_assert_true(insight_notes_set(n,INSIGHT_BOOKMARK,"entry","/tmp/source.c",2,"call();",&e));
 g_assert_true(insight_notes_set(n,INSIGHT_CLIP,"entry",NULL,0,"void f() {\n}\n",&e)); g_assert_cmpuint(n->items->len,==,2);
 g_assert_true(insight_notes_set(n,INSIGHT_CLIP,"entry",NULL,0,"updated",&e)); g_assert_cmpstr(insight_notes_get(n,INSIGHT_CLIP,"entry")->text,==,"updated");
 const InsightNote *mark=insight_notes_get(n,INSIGHT_BOOKMARK,"entry"); guint64 line; gboolean ambiguous;
 const char *moved="header\nextra\ncall();\n"; g_assert_true(insight_bookmark_resolve(mark,moved,strlen(moved),&line,&ambiguous)); g_assert_cmpuint(line,==,3);
 const char *dup="call();\nother\ncall();"; g_assert_false(insight_bookmark_resolve(mark,dup,strlen(dup),&line,&ambiguous)); g_assert_true(ambiguous);
 gchar *file=g_strdup_printf("notes-test-%u.conf",g_random_int()); g_assert_true(insight_notes_save(n,file,&e)); InsightNotes *copy=insight_notes_load(file,&e); g_assert_no_error(e); g_assert_nonnull(copy); g_assert_cmpuint(copy->items->len,==,2);
 g_assert_true(insight_notes_remove(copy,INSIGHT_CLIP,"entry")); g_assert_false(insight_notes_remove(copy,INSIGHT_CLIP,"missing")); insight_notes_free(copy);
 g_assert_true(g_file_set_contents(file,"[notes]\nversion=1\ncount=garbage\n",-1,&e)); copy=insight_notes_load(file,&e); g_assert_null(copy); g_assert_nonnull(e); g_clear_error(&e);
 g_assert_false(insight_notes_set(n,INSIGHT_BOOKMARK,"bad","relative.c",1,"text",NULL));
 /* Export only the selected panel, including Unicode and multiline text. */
 g_assert_true(insight_notes_set(n,INSIGHT_CLIP,"пример",NULL,0,"строка\n[section]\\value\t",&e));
 InsightNotes *clips=insight_notes_select(n,INSIGHT_CLIP,&e); g_assert_no_error(e); g_assert_cmpuint(clips->items->len,==,2);
 g_assert_null(insight_notes_get(clips,INSIGHT_BOOKMARK,"entry"));
 g_assert_true(insight_notes_save(clips,file,&e)); copy=insight_notes_load(file,&e); g_assert_no_error(e);
 g_assert_cmpstr(insight_notes_get(copy,INSIGHT_CLIP,"пример")->text,==,"строка\n[section]\\value\t"); insight_notes_free(copy);
 gchar *directory=g_strdup_printf("notes-directory-%u",g_random_int()); g_assert_cmpint(g_mkdir(directory,0700),==,0);
 g_assert_false(insight_notes_save(n,directory,&e)); g_assert_nonnull(e); g_clear_error(&e); g_rmdir(directory); g_free(directory);
 copy=insight_notes_load(file,&e); g_assert_no_error(e); g_assert_cmpuint(copy->items->len,==,2); insight_notes_free(copy);
 InsightNotes *destination=insight_notes_new(); g_assert_true(insight_notes_set(destination,INSIGHT_BOOKMARK,"entry","/tmp/other.c",1,"other",&e));
 copy=insight_notes_merge(destination,clips,INSIGHT_BOOKMARK,&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&e);
 copy=insight_notes_merge(destination,n,INSIGHT_CLIP,&e); g_assert_no_error(e); g_assert_cmpuint(copy->items->len,==,3);
 g_assert_cmpstr(insight_notes_get(copy,INSIGHT_BOOKMARK,"entry")->path,==,"/tmp/other.c"); g_assert_cmpuint(destination->items->len,==,1); insight_notes_free(copy);
 /* A late collision must not import preceding records or overwrite data. */
 g_assert_true(insight_notes_set(destination,INSIGHT_CLIP,"пример",NULL,0,"keep",&e));
 copy=insight_notes_merge(destination,n,INSIGHT_CLIP,&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_EXISTS); g_clear_error(&e);
 g_assert_cmpuint(destination->items->len,==,2); g_assert_null(insight_notes_get(destination,INSIGHT_CLIP,"entry")); g_assert_cmpstr(insight_notes_get(destination,INSIGHT_CLIP,"пример")->text,==,"keep");
 for(guint i=0;i<62;i++) { gchar *name=g_strdup_printf("note-%u",i); g_assert_true(insight_notes_set(destination,INSIGHT_CLIP,name,NULL,0,"x",&e)); g_free(name); }
 InsightNotes *incoming=insight_notes_new(); g_assert_true(insight_notes_set(incoming,INSIGHT_CLIP,"new",NULL,0,"x",&e));
 copy=insight_notes_merge(destination,incoming,INSIGHT_CLIP,&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_NO_SPACE); g_clear_error(&e); g_assert_cmpuint(destination->items->len,==,64);
 copy=insight_notes_select(n,(InsightNoteKind)0,&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT); g_clear_error(&e);
 /* Rename still works at the shared limit and does not alter source notes. */
 copy=insight_notes_rename(destination,INSIGHT_CLIP,"пример","новое имя",&e); g_assert_no_error(e); g_assert_cmpuint(copy->items->len,==,64);
 g_assert_null(insight_notes_get(copy,INSIGHT_CLIP,"пример")); g_assert_cmpstr(insight_notes_get(copy,INSIGHT_CLIP,"новое имя")->text,==,"keep"); insight_notes_free(copy);
 copy=insight_notes_rename(destination,INSIGHT_CLIP,"пример","note-0",&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_EXISTS); g_clear_error(&e);
 copy=insight_notes_rename(destination,INSIGHT_CLIP,"пример","",&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&e);
 g_assert_nonnull(insight_notes_get(destination,INSIGHT_CLIP,"пример"));
 copy=insight_notes_rename(n,INSIGHT_BOOKMARK,"entry","переход",&e); g_assert_no_error(e);
 g_assert_cmpstr(insight_notes_get(copy,INSIGHT_BOOKMARK,"переход")->path,==,"/tmp/source.c"); g_assert_cmpuint(insight_notes_get(copy,INSIGHT_BOOKMARK,"переход")->line,==,2);
 g_assert_cmpstr(insight_notes_get(copy,INSIGHT_BOOKMARK,"переход")->text,==,"call();"); g_assert_nonnull(insight_notes_get(copy,INSIGHT_CLIP,"entry")); insight_notes_free(copy);
 const char invalid[]="[notes]\nversion=1\ncount=0\n\0unexpected";
 g_assert_true(g_file_set_contents(file,invalid,sizeof invalid-1,&e)); copy=insight_notes_load(file,&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&e);
 insight_notes_free(incoming); insight_notes_free(destination); insight_notes_free(clips);
 g_remove(file); g_free(file); insight_notes_free(n); g_print("Notes checks passed\n"); return 0;
}
