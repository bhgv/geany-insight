/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static gchar *path;static GtkWidget *window;static GeanyDocument *source;static guint backup_mode;
static const gchar *original="header\nα old\r\nkeep\nend old\n",*backup="header\nα backup\r\nkeep\nend backup\n",*current="header\nα current\r\nkeep\nend current\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *top(const gchar *name) { GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data) && !g_strcmp0(gtk_widget_get_name(p->data),name)) found=p->data;g_list_free(list);return found; }
static void action(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void click(GtkWidget *view,const gchar *name) { GtkWidget *button=named(view,name);g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button)); }
static void flush(void) { while(gtk_events_pending()) gtk_main_iteration(); }
static GtkWidget *ready(void) { GtkWidget *view=top("insight-snapshot-compare-window");g_assert_nonnull(view);gint64 until=g_get_monotonic_time()+8*G_TIME_SPAN_SECOND;while(g_object_get_data(G_OBJECT(view),"insight-snapshot-busy") && g_get_monotonic_time()<until) { flush();g_usleep(1000); }g_assert_null(g_object_get_data(G_OBJECT(view),"insight-snapshot-busy"));flush();GtkWidget *tabs=named(view,"insight-snapshot-toolbar-tabs");g_assert_true(GTK_IS_NOTEBOOK(tabs));g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(tabs)),==,4);g_assert_nonnull(named(tabs,"insight-ai-options-collapse"));g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(gtk_widget_get_parent(named(view,"insight-snapshot-previous")))),==,GTK_ORIENTATION_HORIZONTAL);g_assert_true(gtk_style_context_has_class(gtk_widget_get_style_context(tabs),"insight-compare-toolbar"));return view; }
static void pane(GtkWidget *view,const gchar *name,const gchar *expected) { GtkWidget *widget=named(view,name);g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(widget)));GtkTextIter a,z;GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));gtk_text_buffer_get_bounds(buffer,&a,&z);gchar *text=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);g_assert_cmpstr(text,==,expected);g_free(text); }
static guint rows(GtkWidget *view) { return gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(named(view,"insight-snapshot-differences"))),NULL); }
static guint selected_row(GtkWidget *view) { GtkTreeModel *model=NULL;GtkTreeIter iter;guint index=99;g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(named(view,"insight-snapshot-differences"))),&model,&iter));gtk_tree_model_get(model,&iter,0,&index,-1);return index; }
static void source_is(const gchar *expected) { gchar *text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void reset(const gchar *text) { SEND(source->editor->sci,SCI_SETREADONLY,0,0);sci_set_text(source->editor->sci,text);SEND(source->editor->sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(source->editor->sci,SCI_SETSAVEPOINT,0,0);SEND(source->editor->sci,SCI_SETSEL,12,7); }
static gboolean backup_respond(gpointer data) {
 (void)data;GtkWidget *dialog=top("insight-backup-dialog");if(!dialog) return G_SOURCE_CONTINUE;
 if(backup_mode==1) { gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(backup_mode==2) { document_set_text_changed(source,FALSE);g_assert_true(document_close(source));gtk_dialog_response(GTK_DIALOG(dialog),204);backup_mode=3;return G_SOURCE_CONTINUE; }
 if(backup_mode==3) { g_assert_null(top("insight-snapshot-compare-window"));gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 gtk_dialog_response(GTK_DIALOG(dialog),backup_mode==4 ? 204:GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static void compare_backup(guint mode,gboolean key) { backup_mode=mode;g_timeout_add(120,backup_respond,NULL);if(key) keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_COMPAREBACKUP);else action(mode==4 ? "insight-open-backup":"insight-compare-backup"); }
static void unchanged(gint undo,gint anchor,gint caret) { source_is(current);g_assert_cmpint(SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0),==,undo);g_assert_cmpint(SEND(source->editor->sci,SCI_GETANCHOR,0,0),==,anchor);g_assert_cmpint(SEND(source->editor->sci,SCI_GETCURRENTPOS,0,0),==,caret);gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,original);g_free(disk); }
static gboolean step(gpointer data) {
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));
 sci_set_text(source->editor->sci,backup);action("insight-save-backup");sci_set_text(source->editor->sci,current);SEND(source->editor->sci,SCI_SETSEL,12,7);gint undo=SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0);
 compare_backup(1,FALSE);g_assert_null(top("insight-snapshot-compare-window"));unchanged(undo,12,7);
 compare_backup(0,TRUE);GtkWidget *view=ready();g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(view,"insight-snapshot-right-name"))),path));g_assert_cmpuint(rows(view),==,2);pane(view,"insight-snapshot-left",backup);pane(view,"insight-snapshot-right",current);unchanged(undo,12,7);
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(named(view,"insight-snapshot-differences")));GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter_first(model,&iter));gchar *range=NULL;gtk_tree_model_get(model,&iter,2,&range,-1);g_assert_cmpstr(range,==,"2–2");g_free(range);
 g_assert_cmpuint(selected_row(view),==,0);click(view,"insight-snapshot-next");g_assert_cmpuint(selected_row(view),==,1);click(view,"insight-snapshot-next");g_assert_cmpuint(selected_row(view),==,1);click(view,"insight-snapshot-previous");g_assert_cmpuint(selected_row(view),==,0);
 click(view,"insight-snapshot-copy");gchar *report=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(strstr(report,"Kind\tLeft lines\tRight lines"));g_assert_nonnull(strstr(report,"2–2\t2–2"));g_free(report);
 if(g_getenv("GEANY_SNAPSHOT_COMPARE_CAPTURE")) { GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(view),0,0,gtk_widget_get_allocated_width(view),gtk_widget_get_allocated_height(view));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_SNAPSHOT_COMPARE_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
 sci_set_text(source->editor->sci,"later source edit\n");click(view,"insight-snapshot-run");ready();pane(view,"insight-snapshot-right",current);source_is("later source edit\n");click(view,"insight-snapshot-close");
 reset(current);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_COMPAREDISK);view=ready();pane(view,"insight-snapshot-left",original);pane(view,"insight-snapshot-right",current);click(view,"insight-snapshot-close");
 SEND(source->editor->sci,SCI_SETREADONLY,1,0);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_COMPAREORIGINAL);view=ready();pane(view,"insight-snapshot-left",original);g_assert_cmpuint(rows(view),==,2);click(view,"insight-snapshot-close");SEND(source->editor->sci,SCI_SETREADONLY,0,0);
 /* Source saving changes the disk, but preserves the opening baseline. */
 reset(current);g_assert_true(document_save_file(source,TRUE));action("insight-compare-disk");view=ready();g_assert_cmpuint(rows(view),==,0);g_assert_false(gtk_widget_get_sensitive(named(view,"insight-snapshot-copy")));click(view,"insight-snapshot-close");
 action("insight-compare-original");view=ready();g_assert_cmpuint(rows(view),==,2);pane(view,"insight-snapshot-left",original);click(view,"insight-snapshot-close");
 /* Exact mixed-EOL/space differences are optional; dirty options clear rows. */
 g_assert_true(g_file_set_contents(path,"a b\r\n",-1,NULL));reset("ab\n");action("insight-compare-disk");view=ready();g_assert_cmpuint(rows(view),==,1);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(view,"insight-snapshot-ignore-space")),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(view,"insight-snapshot-ignore-eol")),TRUE);g_assert_cmpuint(rows(view),==,0);g_assert_false(gtk_widget_get_sensitive(named(view,"insight-snapshot-copy")));click(view,"insight-snapshot-run");ready();g_assert_cmpuint(rows(view),==,0);click(view,"insight-snapshot-close");
 reset("AB\n");action("insight-compare-disk");view=ready();gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(view,"insight-snapshot-ignore-space")),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(view,"insight-snapshot-ignore-eol")),TRUE);click(view,"insight-snapshot-run");ready();g_assert_cmpuint(rows(view),==,1);click(view,"insight-snapshot-close");
 /* Insertion/deletion anchors are one-based, including CRLF and EOF. */
 g_assert_true(g_file_set_contents(path,"prefix\n",-1,NULL));reset("prefix\nadded\r\n");action("insight-compare-disk");view=ready();g_assert_cmpuint(rows(view),==,1);model=gtk_tree_view_get_model(GTK_TREE_VIEW(named(view,"insight-snapshot-differences")));g_assert_true(gtk_tree_model_get_iter_first(model,&iter));gchar *kind=NULL;gtk_tree_model_get(model,&iter,1,&kind,2,&range,-1);g_assert_cmpstr(kind,==,"Inserted");g_assert_cmpstr(range,==,"2 (anchor)");g_free(kind);g_free(range);click(view,"insight-snapshot-close");
 g_assert_true(g_file_set_contents(path,"prefix\nremoved\n",-1,NULL));reset("prefix\n");action("insight-compare-disk");view=ready();model=gtk_tree_view_get_model(GTK_TREE_VIEW(named(view,"insight-snapshot-differences")));g_assert_true(gtk_tree_model_get_iter_first(model,&iter));gtk_tree_model_get(model,&iter,1,&kind,3,&range,-1);g_assert_cmpstr(kind,==,"Deleted");g_assert_cmpstr(range,==,"2 (anchor)");g_free(kind);g_free(range);click(view,"insight-snapshot-close");
 GeanyDocument *untitled=document_new_file(NULL,NULL,NULL);action("insight-compare-disk");g_assert_null(top("insight-snapshot-compare-window"));action("insight-compare-original");view=ready();g_assert_cmpuint(rows(view),==,0);pane(view,"insight-snapshot-left","");pane(view,"insight-snapshot-right","");click(view,"insight-snapshot-close");g_assert_true(document_close(untitled));
 /* Pending/cancelled/destroyed worker windows must not publish stale rows. */
 GString *a=g_string_new(NULL),*b=g_string_new(NULL);for(guint i=0;i<2100;i++) { g_string_append(a,"a\n");g_string_append(b,"b\n"); }
 g_assert_true(g_file_set_contents(path,a->str,a->len,NULL));reset(b->str);action("insight-compare-disk");view=ready();g_assert_cmpuint(rows(view),==,0);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(view,"insight-snapshot-status"))),"limit"));click(view,"insight-snapshot-close");
 action("insight-compare-disk");view=top("insight-snapshot-compare-window");g_assert_nonnull(view);click(view,"insight-snapshot-cancel");ready();g_assert_cmpuint(rows(view),==,0);click(view,"insight-snapshot-close");
 action("insight-compare-disk");view=top("insight-snapshot-compare-window");g_assert_nonnull(view);click(view,"insight-snapshot-close");flush();g_assert_null(top("insight-snapshot-compare-window"));g_string_free(a,TRUE);g_string_free(b,TRUE);
 gchar *large=g_malloc(1024*1024+2);memset(large,'x',1024*1024+1);large[1024*1024+1]=0;reset(large);g_free(large);action("insight-compare-original");g_assert_null(top("insight-snapshot-compare-window"));
 reset("valid");SEND(source->editor->sci,SCI_CLEARALL,0,0);SEND(source->editor->sci,SCI_ADDTEXT,3,(sptr_t)"a\0b");action("insight-compare-original");g_assert_null(top("insight-snapshot-compare-window"));
 reset(current);compare_backup(4,FALSE);view=ready();pane(view,"insight-snapshot-left",backup);click(view,"insight-snapshot-close");
 compare_backup(2,FALSE);g_assert_null(top("insight-snapshot-compare-window"));
 g_print("Snapshot comparison GUI: backup/disk/original commands and keys, exact immutable text, mixed-EOL ranges, navigation/report, source Undo/selection/disk, read-only, save baseline, ignore options, work/size/NUL guards, cancel/close and closed-source backup rejection passed\n");
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Snapshot comparison GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *profile=g_dir_make_tmp("snapshot-compare-XXXXXX",NULL),*config=g_build_filename(profile,"geany.conf",NULL);path=g_build_filename(profile,"source.c",NULL);g_assert_true(g_file_set_contents(path,original,-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *args[]={"snapshot-compare","--new-instance","--no-session","--config",profile,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(45,timeout,NULL);return main_lib(6,args);
}
