#include "notebook_store.h"
#include <glib.h>
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
 g_remove(file); g_free(file); insight_notes_free(n); g_print("Notes checks passed\n"); return 0;
}
