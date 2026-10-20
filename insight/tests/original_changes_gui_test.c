/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "navqueue.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static const gchar *original="head\r\nα original\r\nkeep\r\nremoved\r\nanchor\r\nlast\r\n";
static const gchar *edited="head\r\nβ edited\r\nkeep\r\nanchor\r\nLAST\r\nextra\r\n";
static GeanyDocument *source,*other;
static GtkWidget *window,*popup;
static gchar *path;
static guint mode,tick;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *dialog(const gchar *name) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),name)) { found=p->data;break; }g_list_free(windows);return found;
}
static void action(const gchar *name) { GtkWidget *item=named(window,name);if(!item) item=named(popup,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void text_is(const gchar *expected) { gchar *text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void reset(const gchar *text,gint caret) {
 ScintillaObject *sci=source->editor->sci;source->readonly=FALSE;SEND(sci,SCI_SETREADONLY,0,0);SEND(sci,SCI_CLEARSELECTIONS,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,0);SEND(sci,SCI_SETSELECTIONNANCHORVIRTUALSPACE,0,0);sci_set_text(sci,text);SEND(sci,SCI_SETSEL,caret,caret);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);
}
static void position(gint expected) { g_assert_cmpint(sci_get_current_position(source->editor->sci),==,expected); }
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog("insight-restore-file-dialog");
 if(d) {
  if(mode==10 && tick++==0) return G_SOURCE_CONTINUE;
  if(mode==10 && g_getenv("GEANY_RESTORE_FILE_CAPTURE")) { GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_RESTORE_FILE_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  if(mode==11) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
  if(mode==12) SEND(source->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"prefix\n");
  if(mode==13) { g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT))); }
  if(mode==14) SEND(source->editor->sci,SCI_SETSEL,0,0);
  if(mode==15) other=document_new_file("other.c",NULL,"other");
  if(mode==16) { document_set_text_changed(source,FALSE);g_assert_true(document_close(source)); }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
 }
 d=dialog("insight-original-changes-dialog");if(!d) return G_SOURCE_CONTINUE;
 GtkWidget *tree=named(d,"insight-changes-list");g_assert_nonnull(tree);GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
 if(mode==0) {
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,3);
  if(!tick++) return G_SOURCE_CONTINUE;
  if(g_getenv("GEANY_ORIGINAL_CHANGES_CAPTURE")) { GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_ORIGINAL_CHANGES_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  GtkTreePath *p=gtk_tree_path_new_from_indices(1,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),p,NULL,FALSE);gtk_tree_path_free(p);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
 }
 if(mode==1) {
  if(tick==0) { SEND(source->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"prefix\n");tick++;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
  if(tick==1) { tick++;gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,4);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
 }
 if(mode==2) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(mode==3) {
  if(!tick++) { other=document_new_file("other.c",NULL,"other");gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
  gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_REMOVE;
 }
 if(mode==4) { document_set_text_changed(source,FALSE);g_assert_true(document_close(source));gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_REMOVE; }
 if(mode==5) {
  GtkTreePath *p=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),p,NULL,FALSE);gtk_tree_path_free(p);gtk_dialog_response(GTK_DIALOG(d),202);return G_SOURCE_CONTINUE;
 }
 if(mode==6) { g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT)));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static gboolean lines_respond(gpointer data) { (void)data;GtkWidget *d=dialog("insight-restore-lines-dialog");if(!d) return G_SOURCE_CONTINUE;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE; }
static void run(guint which,const gchar *name) { mode=which;tick=0;guint timer=g_timeout_add(120,respond,NULL);action(name);if(which==5) { g_source_remove(timer); } }
static gboolean step(gpointer data) {
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));popup=g_object_get_data(G_OBJECT(window),"insight-editor-menu");g_assert_nonnull(popup);
 ScintillaObject *sci=source->editor->sci;gint first=strstr(edited,"β")-edited,deleted=strstr(edited,"anchor")-edited,last=strstr(edited,"LAST")-edited;
 reset(edited,0);g_assert_true(document_save_file(source,TRUE));
 action("insight-next-original-change");position(first);action("insight-next-original-change");position(deleted);action("insight-next-original-change");position(last);action("insight-next-original-change");position(last);action("insight-prev-original-change");position(deleted);action("insight-prev-original-change");position(first);action("insight-prev-original-change");position(first);text_is(edited);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 SEND(sci,SCI_SETSEL,0,strlen(edited));action("insight-prev-original-change");position(last);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,last);
 SEND(sci,SCI_SETREADONLY,1,0);action("insight-popup-prev-original-change");position(deleted);text_is(edited);SEND(sci,SCI_SETREADONLY,0,0);
 reset(edited,0);gtk_widget_grab_focus(GTK_WIDGET(sci));keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_INSIGHT_NEXTCHANGE);position(first);keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_INSIGHT_PREVCHANGE);position(first);
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-extend-mode")),TRUE);action("insight-next-original-change");position(deleted);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,first);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-extend-mode")),FALSE);
 reset(edited,0);run(0,"insight-original-changes");position(deleted);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,deleted);
 reset(edited,0);run(1,"insight-popup-original-changes");position(7);gchar *prefixed=g_strconcat("prefix\n",edited,NULL);text_is(prefixed);g_free(prefixed);
 reset(edited,0);run(2,"insight-original-changes");position(0);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 run(3,"insight-original-changes");g_assert_true(document_get_current()==other);g_assert_true(document_close(other));text_is(edited);
 reset(original,0);run(6,"insight-original-changes");text_is(original);
 reset(edited,8);run(11,"insight-restore-file");text_is(edited);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 run(10,"insight-popup-restore-file");text_is(original);SEND(sci,SCI_UNDO,0,0);text_is(edited);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);text_is(original);
 reset(edited,8);run(12,"insight-restore-file");prefixed=g_strconcat("prefix\n",edited,NULL);text_is(prefixed);g_free(prefixed);
 reset(edited,8);SEND(sci,SCI_SETREADONLY,1,0);run(13,"insight-restore-file");text_is(edited);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(edited,8);run(14,"insight-restore-file");text_is(edited);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(edited,8);run(15,"insight-restore-file");g_assert_true(document_close(other));text_is(edited);
 reset(edited,8);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);action("insight-next-original-change");action("insight-original-changes");action("insight-restore-file");g_assert_null(dialog("insight-original-changes-dialog"));g_assert_null(dialog("insight-restore-file-dialog"));position(8);
 reset(edited,8);SEND(sci,SCI_ADDSELECTION,0,0);action("insight-restore-file");g_assert_null(dialog("insight-restore-file-dialog"));
 reset(edited,8);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,1);action("insight-next-original-change");position(8);
 reset("α",1);action("insight-next-original-change");position(1);action("insight-original-changes");g_assert_null(dialog("insight-original-changes-dialog"));
 reset(edited,8);g_timeout_add(150,lines_respond,NULL);run(5,"insight-original-changes");gchar *partial=g_strdup(edited);gchar *expected=g_strconcat("head\r\nα original\r\n",strstr(partial,"keep"),NULL);text_is(expected);g_free(partial);g_free(expected);SEND(sci,SCI_UNDO,0,0);text_is(edited);
 reset(edited,8);GtkWidget *entry=gtk_entry_new();gtk_box_pack_start(GTK_BOX(ui_lookup_widget(window,"vbox1")),entry,FALSE,FALSE,0);gtk_widget_show(entry);gtk_widget_grab_focus(entry);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_RESTOREFILE);g_assert_null(dialog("insight-restore-file-dialog"));gtk_widget_destroy(entry);gtk_widget_grab_focus(GTK_WIDGET(sci));
 reset(edited,8);run(4,"insight-original-changes");source=document_open_file(path,FALSE,NULL,NULL);g_assert_nonnull(source);sci=source->editor->sci;reset(original,8);run(16,"insight-restore-file");source=document_open_file(path,FALSE,NULL,NULL);g_assert_nonnull(source);
 GeanyDocument *old=source;GString *large_original=g_string_new(NULL),*large_current=g_string_new(NULL);for(guint i=0;i<2100;i++) { g_string_append(large_original,"a\n");g_string_append(large_current,"b\n"); }
 source=document_new_file("work-limit.txt",NULL,large_original->str);reset(large_current->str,0);action("insight-original-changes");g_assert_null(dialog("insight-original-changes-dialog"));action("insight-next-original-change");position(0);run(17,"insight-restore-file");text_is(large_original->str);SEND(source->editor->sci,SCI_UNDO,0,0);text_is(large_current->str);document_set_text_changed(source,FALSE);g_assert_true(document_close(source));source=old;g_string_free(large_original,TRUE);g_string_free(large_current,TRUE);
 gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,edited);g_free(disk);
 g_print("Original Changes GUI: exact blocks, deletion anchors, navigation boundaries/read-only, list selection/stale refresh/closed/switched guards, whole-file restore Cancel/Undo/Redo/stale/readonly/selection/focus guards and unchanged saved disk passed\n");document_set_text_changed(source,FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Original Changes GUI timeout mode %u tick %u",mode,tick);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("original-changes-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL);path=g_build_filename(dir,"source.c",NULL);g_assert_true(g_file_set_contents(path,original,-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nshow_keep_edit_history_on_reload_msg=false\n",-1,NULL));gchar *args[]={"original-changes","--new-instance","--no-session","--config",dir,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(40,timeout,NULL);return main_lib(6,args);
}
