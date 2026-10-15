/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*view,*query,*page;static gchar *source,*store,*output;static guint phase,picks;static gboolean change_selection;static const char *destination;
static GtkWidget *named(GtkWidget *w,const char *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=list;p && !f;p=p->next) f=named(p->data,name);g_list_free(list);return f; }return NULL; }
static GtkWidget *button(GtkWidget *w,const char *label) { if(GTK_IS_BUTTON(w) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(w)),label)) return w;if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=list;p && !f;p=p->next) f=button(p->data,label);g_list_free(list);return f; }return NULL; }
static GtkTreeSelection *selection(void) { return gtk_tree_view_get_selection(GTK_TREE_VIEW(view)); }
static void select_name(const char *name) { GtkTreeModel *m=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter i;for(gboolean v=gtk_tree_model_get_iter_first(m,&i);v;v=gtk_tree_model_iter_next(m,&i)) { gchar *n=NULL;gtk_tree_model_get(m,&i,0,&n,-1);gboolean match=!g_strcmp0(n,name);g_free(n);if(match) { gtk_tree_selection_select_iter(selection(),&i);return; } }g_error("Missing %s",name); }
static void disk(const char *path,const char *names,guint count) { GKeyFile *k=g_key_file_new();g_assert_true(g_key_file_load_from_file(k,path,G_KEY_FILE_NONE,NULL));g_assert_cmpuint(g_key_file_get_integer(k,"notes","count",NULL),==,count);for(guint i=0;i<count;i++) { gchar *group=g_strdup_printf("note-%u",i),*name=g_key_file_get_string(k,group,"name",NULL);g_assert_nonnull(strchr(names,*name));g_assert_cmpint(g_key_file_get_integer(k,group,"kind",NULL),==,1);g_assert_true(g_key_file_has_key(k,group,"anchor-name",NULL));g_free(group);g_free(name); }g_key_file_unref(k); }
/* Exercise the panel integration through its real menu and modal dialog. */
static guint appearance_action;
static gboolean appearance_response(gpointer data) {
 (void)data;
 GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p && !dialog;p=p->next)
  if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-list-style-dialog")) dialog=p->data;
 g_list_free(windows);g_assert_nonnull(dialog);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-list-style-enabled")),appearance_action!=2);
 gtk_font_chooser_set_font(GTK_FONT_CHOOSER(named(dialog,"insight-list-style-font")),"Monospace 14");
 GdkRGBA color;g_assert_true(gdk_rgba_parse(&color,"#123456"));
 gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(named(dialog,"insight-list-style-foreground")),&color);
 g_assert_true(gdk_rgba_parse(&color,"#f4e3d2"));
 gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(named(dialog,"insight-list-style-background")),&color);
 gtk_dialog_response(GTK_DIALOG(dialog),appearance_action==0 ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_APPLY);
 return G_SOURCE_REMOVE;
}
static void appearance_check(GtkWidget *view,gboolean enabled,gint points,const char *foreground,const char *background) {
 if(enabled) {
  GdkRGBA got,wanted;gtk_style_context_get_color(gtk_widget_get_style_context(view),GTK_STATE_FLAG_NORMAL,&got);g_assert_true(gdk_rgba_parse(&wanted,foreground));g_assert_true(gdk_rgba_equal(&got,&wanted));
  gtk_style_context_get_background_color(gtk_widget_get_style_context(view),GTK_STATE_FLAG_NORMAL,&got);g_assert_true(gdk_rgba_parse(&wanted,background));g_assert_true(gdk_rgba_equal(&got,&wanted));
 }
 /* All four columns must receive the font, rather than just the name cell. */
 for(guint i=0;i<gtk_tree_view_get_n_columns(GTK_TREE_VIEW(view));i++) {
  GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(gtk_tree_view_get_column(GTK_TREE_VIEW(view),i)));
  for(GList *p=cells;p;p=p->next) { gboolean set=FALSE;g_object_get(p->data,"size-set",&set,NULL);g_assert_cmpint(set,==,enabled);if(enabled) { gint size=0;g_object_get(p->data,"size",&size,NULL);g_assert_cmpint(size,==,points*PANGO_SCALE); } }
  g_list_free(cells);
 }
}
static GtkWidget *menu(void);
static void appearance_checks(GeanyDocument *doc) {
 GtkTreeSelection *selected=selection();guint count=gtk_tree_selection_count_selected_rows(selected);
 gint caret=sci_get_current_position(doc->editor->sci);
 GtkWidget *folders=named(window,"insight-project-folders");
 GdkRGBA folder_color;gtk_style_context_get_color(gtk_widget_get_style_context(folders),GTK_STATE_FLAG_NORMAL,&folder_color);
 appearance_check(view,TRUE,11,"#234567","#f1e2d3");
 for(guint action=0;action<4;action++) {
  appearance_action=action;GtkWidget *m=menu(),*item=named(m,"insight-bookmark-appearance");g_assert_nonnull(item);g_assert_true(gtk_widget_get_sensitive(item));
  g_timeout_add(50,appearance_response,NULL);gtk_menu_item_activate(GTK_MENU_ITEM(item));
  appearance_check(view,action!=2,action==0 ? 11 : 14,action==0 ? "#234567" : "#123456",action==0 ? "#f1e2d3" : "#f4e3d2");
  g_assert_cmpuint(gtk_tree_selection_count_selected_rows(selected),==,count);
  g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);
 }
 GdkRGBA after;gtk_style_context_get_color(gtk_widget_get_style_context(folders),GTK_STATE_FLAG_NORMAL,&after);g_assert_true(gdk_rgba_equal(&folder_color,&after));
}
static gboolean pick(gpointer data) { (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=windows;p;p=p->next) if(GTK_IS_FILE_CHOOSER(p->data) && gtk_widget_get_visible(p->data)) dialog=p->data;g_list_free(windows);if(!dialog) return G_SOURCE_CONTINUE;
 if(!picks++) { g_print("Prepare export: %s\n",destination); if(change_selection) { gtk_tree_selection_unselect_all(selection());select_name("B"); }gchar *folder=g_path_get_dirname(destination),*base=g_path_get_basename(destination);gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),folder);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),base);g_free(folder);g_free(base);return G_SOURCE_CONTINUE; }
 gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));gboolean ready=!g_strcmp0(path,destination);g_free(path);if(!ready) return G_SOURCE_CONTINUE;g_print("Accept export: %s\n",destination);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE; }
static void export_selected(const char *path,gboolean change) { destination=path;change_selection=change;picks=0;g_timeout_add(100,pick,NULL);gtk_button_clicked(GTK_BUTTON(button(page,"Export selected…"))); }
static GtkWidget *menu(void) { gboolean handled=FALSE;g_signal_emit_by_name(view,"popup-menu",&handled);g_assert_true(handled);GtkWidget *m=g_object_get_data(G_OBJECT(view),"insight-bookmark-popup");g_assert_nonnull(m);gtk_menu_popdown(GTK_MENU(m));return m; }
static GtkWidget *command(GtkWidget *m,guint index) { gchar *name=g_strdup_printf("insight-bookmark-command-%u",index);GtkWidget *item=g_object_get_data(G_OBJECT(m),name);g_free(name);return item; }
static guint total_notes(void) { GKeyFile *k=g_key_file_new();g_assert_true(g_key_file_load_from_file(k,store,G_KEY_FILE_NONE,NULL));guint count=g_key_file_get_integer(k,"notes","count",NULL);g_key_file_unref(k);return count; }
static gboolean cancel_unsaved_close(gpointer data) {
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p && !dialog;p=p->next) if(GTK_IS_DIALOG(p->data) && gtk_widget_get_visible(p->data) && gtk_dialog_get_widget_for_response(GTK_DIALOG(p->data),GTK_RESPONSE_CANCEL)) dialog=p->data;
 g_list_free(windows);g_assert_nonnull(dialog);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
static void close_file_checks(void) {
 GtkWidget *m=menu(),*option=command(m,10);
 g_assert_nonnull(option);
 g_assert_cmpint(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(option)),==,g_getenv("GEANY_TEST_BOOKMARK_CLOSE_RESTORE")!=NULL);
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option),FALSE);
 gchar *other=g_strconcat(source,".other.c",NULL);g_assert_true(g_file_set_contents(other,"int other(void) {\nreturn 2;\n}\n",-1,NULL));
 GeanyDocument *doc=document_open_file(other,FALSE,NULL,NULL);g_assert_nonnull(doc);
 gtk_entry_set_text(GTK_ENTRY(query),"KeepOnClose");gtk_button_clicked(GTK_BUTTON(button(page,"Add/update")));
 g_assert_cmpuint(total_notes(),==,3);g_assert_true(document_close(doc));g_assert_cmpuint(total_notes(),==,3);
 doc=document_open_file(other,FALSE,NULL,NULL);g_assert_nonnull(doc);
 gtk_entry_set_text(GTK_ENTRY(query),"SecondInFile");sci_set_current_position(doc->editor->sci,sci_get_position_from_line(doc->editor->sci,1),FALSE);gtk_button_clicked(GTK_BUTTON(button(page,"Add/update")));
 g_assert_cmpuint(total_notes(),==,4);
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option),TRUE);
 document_set_text_changed(doc,TRUE);g_timeout_add(50,cancel_unsaved_close,NULL);g_assert_false(document_close(doc));g_assert_true(doc->is_valid);g_assert_cmpuint(total_notes(),==,4);document_set_text_changed(doc,FALSE);
 /* A hidden row must be removed too; this setting does not use manual-delete confirmation. */
 gtk_entry_set_text(GTK_ENTRY(query),"no-visible-marks");g_assert_true(document_close(doc));g_assert_cmpuint(total_notes(),==,2);
 doc=document_open_file(other,FALSE,NULL,NULL);g_assert_nonnull(doc);g_assert_cmpint(scintilla_send_message(doc->editor->sci,SCI_MARKERNEXT,0,2),==,-1);
 sci_set_current_position(doc->editor->sci,sci_get_position_from_line(doc->editor->sci,1),FALSE);gtk_entry_set_text(GTK_ENTRY(query),"FailedClose");gtk_button_clicked(GTK_BUTTON(button(page,"Add/update")));g_assert_cmpuint(total_notes(),==,3);
 gchar *backup=g_strconcat(store,".close-backup",NULL);g_assert_cmpint(g_rename(store,backup),==,0);g_assert_cmpint(g_mkdir(store,0700),==,0);
 g_assert_true(document_close(doc));g_assert_cmpint(g_rmdir(store),==,0);g_assert_cmpint(g_rename(backup,store),==,0);g_assert_cmpuint(total_notes(),==,3);
 doc=document_open_file(other,FALSE,NULL,NULL);g_assert_nonnull(doc);gtk_entry_set_text(GTK_ENTRY(query),"FailedClose");g_signal_emit_by_name(query,"activate");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,1);
 g_assert_true(document_close(doc));g_assert_cmpuint(total_notes(),==,2);
 gtk_entry_set_text(GTK_ENTRY(query),"");g_free(backup);g_free(other);
}
static void enter(const char *text) { gtk_entry_set_text(GTK_ENTRY(query),text);g_signal_emit_by_name(query,"activate"); }
static gboolean confirm(gpointer data) { GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-confirm-bookmark-delete")) dialog=p->data;g_list_free(windows);g_assert_nonnull(dialog);gint action=GPOINTER_TO_INT(data);g_assert_true(gtk_widget_has_default(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL)));if(action==2) gtk_button_clicked(GTK_BUTTON(named(window,"insight-reload-project-bookmarks")));gtk_dialog_response(GTK_DIALOG(dialog),action ? GTK_RESPONSE_ACCEPT : GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
static void delete_all(gint response) { GtkWidget *m=menu();g_idle_add(confirm,GINT_TO_POINTER(response));gtk_menu_item_activate(GTK_MENU_ITEM(command(m,8))); }
static void command_checks(GeanyDocument *doc) {
 enter("3");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,2);enter("0");enter("999999999999999999999999999999999");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,2);g_assert_cmpuint(total_notes(),==,2);
 gtk_entry_set_text(GTK_ENTRY(query),"CoreOne");sci_set_current_position(doc->editor->sci,0,FALSE);gtk_button_clicked(GTK_BUTTON(button(page,"Add/update")));gtk_entry_set_text(GTK_ENTRY(query),"CoreTwo");gtk_button_clicked(GTK_BUTTON(button(page,"Add/update")));enter("3");enter("Core");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,2);g_assert_cmpuint(total_notes(),==,4);enter("one");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,0);
 gtk_entry_set_text(GTK_ENTRY(query),"2");gtk_button_clicked(GTK_BUTTON(button(page,"Add/update")));enter("3");enter("2");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,0);g_assert_cmpuint(total_notes(),==,5);
 gtk_entry_set_text(GTK_ENTRY(query),"absent");delete_all(0);g_assert_cmpuint(total_notes(),==,5);delete_all(2);g_assert_cmpuint(total_notes(),==,5);
 gchar *backup=g_strconcat(store,".backup",NULL);g_assert_cmpint(g_rename(store,backup),==,0);g_assert_cmpint(g_mkdir(store,0700),==,0);delete_all(1);g_assert_cmpint(g_rmdir(store),==,0);g_assert_cmpint(g_rename(backup,store),==,0);g_free(backup);g_assert_cmpuint(total_notes(),==,5);
 gtk_entry_set_text(GTK_ENTRY(query),"");select_name("CoreOne");GtkWidget *m=menu();gtk_menu_item_activate(GTK_MENU_ITEM(command(m,9)));g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(command(m,9))));g_idle_add(confirm,GINT_TO_POINTER(0));gtk_button_clicked(GTK_BUTTON(button(page,"Delete")));g_assert_cmpuint(total_notes(),==,5);
 g_idle_add(confirm,GINT_TO_POINTER(1));gtk_button_clicked(GTK_BUTTON(button(page,"Delete")));g_assert_cmpuint(total_notes(),==,4);gtk_entry_set_text(GTK_ENTRY(query),"absent");delete_all(1);g_assert_cmpuint(total_notes(),==,1);gtk_entry_set_text(GTK_ENTRY(query),"");g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,0);
}
static gboolean options_confirm,options_close,options_apply,options_fail,options_font;
static GtkWidget *options_dialog(void) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p && !dialog;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-bookmark-options")) dialog=p->data;
 g_list_free(windows);g_assert_nonnull(dialog);return dialog;
}
static gboolean options_failed(gpointer data) {
 (void)data;GtkWidget *dialog=options_dialog();g_assert_true(*gtk_label_get_text(GTK_LABEL(named(dialog,"insight-bookmark-options-status")))!=0);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
static gboolean options_response(gpointer data) {
 (void)data;GtkWidget *dialog=options_dialog();g_assert_cmpstr(gtk_window_get_title(GTK_WINDOW(dialog)),==,gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru"))) ? "Параметры закладок" : "Bookmark Options");g_assert_nonnull(named(dialog,"insight-bookmark-options-font"));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-bookmark-options-confirm")),options_confirm);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-bookmark-options-close")),options_close);
 if(options_font) { appearance_action=0;g_timeout_add(50,appearance_response,NULL);gtk_button_clicked(GTK_BUTTON(named(dialog,"insight-bookmark-options-font"))); }
 if(options_fail) g_timeout_add(50,options_failed,NULL);
 gtk_dialog_response(GTK_DIALOG(dialog),options_apply ? GTK_RESPONSE_APPLY : GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
static void options_open(gboolean keyboard) {
 g_print("Options open keyboard=%d apply=%d fail=%d\n",keyboard,options_apply,options_fail);g_timeout_add(50,options_response,NULL);
 if(keyboard) { gtk_widget_grab_focus(query);GdkEventKey event={0};event.type=GDK_KEY_PRESS;event.window=gtk_widget_get_window(window);event.keyval=GDK_KEY_q;event.state=GDK_CONTROL_MASK;gboolean handled=FALSE;g_signal_emit_by_name(query,"key-press-event",&event,&handled);g_assert_true(handled); }
 else gtk_menu_item_activate(GTK_MENU_ITEM(command(menu(),11)));
}
static void options_check_state(gboolean a,gboolean b) {
 GtkWidget *m=menu();g_assert_cmpint(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(command(m,9))),==,a);g_assert_cmpint(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(command(m,10))),==,b);
}
static void options_checks(void) {
 gboolean a=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(command(menu(),9))),b=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(command(menu(),10)));
 gchar *folder=g_path_get_dirname(store),*prefs=g_build_filename(folder,"workbench.conf",NULL),*backup=g_strconcat(prefs,".backup",NULL);g_free(folder);
 guint notes=total_notes();gint cursor=sci_get_current_position(document_get_current()->editor->sci);
 options_confirm=!a;options_close=!b;options_font=TRUE;options_apply=FALSE;options_open(TRUE);options_check_state(a,b);options_font=FALSE;
 options_apply=TRUE;options_open(FALSE);options_check_state(!a,!b);
 GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,prefs,G_KEY_FILE_NONE,NULL));g_assert_cmpint(g_key_file_get_boolean(key,"workbench","confirm-bookmark-delete",NULL),==,!a);g_assert_cmpint(g_key_file_get_boolean(key,"workbench","delete-bookmarks-on-close",NULL),==,!b);gchar *font=g_key_file_get_string(key,"bookmarks-style","font",NULL);g_assert_cmpstr(font,==,"Monospace 11");g_free(font);g_key_file_unref(key);
 g_assert_cmpint(g_rename(prefs,backup),==,0);g_assert_cmpint(g_mkdir(prefs,0700),==,0);
 options_confirm=a;options_close=b;options_fail=TRUE;options_open(FALSE);options_check_state(!a,!b);options_fail=FALSE;
 g_assert_cmpint(g_rmdir(prefs),==,0);g_assert_cmpint(g_rename(backup,prefs),==,0);options_open(FALSE);options_check_state(a,b);
 g_assert_cmpuint(total_notes(),==,notes);g_assert_cmpint(sci_get_current_position(document_get_current()->editor->sci),==,cursor);
 g_free(backup);g_free(prefs);
}
static gboolean tick(gpointer data) { (void)data;if(phase++) return G_SOURCE_REMOVE;GeanyDocument *doc=document_open_file(source,FALSE,NULL,NULL);g_assert_nonnull(doc);window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));view=named(window,"insight-bookmarks");query=named(window,"insight-bookmarks-query");page=gtk_widget_get_parent(query);GtkWidget *child=page,*notebook=gtk_widget_get_parent(child);while(notebook && !GTK_IS_NOTEBOOK(notebook)) { child=notebook;notebook=gtk_widget_get_parent(child); }g_assert_nonnull(notebook);gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook),gtk_notebook_page_num(GTK_NOTEBOOK(notebook),child));
 GtkWidget *initial=menu();g_assert_cmpint(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(command(initial,9))),==,g_getenv("GEANY_TEST_BOOKMARK_CONFIRM_RESTORE")!=NULL);if(g_getenv("GEANY_TEST_BOOKMARK_CONFIRM_RESTORE")) gtk_menu_item_activate(GTK_MENU_ITEM(command(initial,9)));
 options_checks();
 g_assert_cmpint(gtk_tree_selection_get_mode(selection()),==,GTK_SELECTION_MULTIPLE);select_name("A");select_name("C");gtk_entry_set_text(GTK_ENTRY(query),"function");g_assert_cmpint(gtk_tree_selection_count_selected_rows(selection()),==,2);gtk_entry_set_text(GTK_ENTRY(query),"");g_assert_cmpint(gtk_tree_selection_count_selected_rows(selection()),==,2);GtkWidget *m=menu();g_assert_false(gtk_widget_get_sensitive(command(m,1)));g_assert_true(gtk_widget_get_sensitive(command(m,3)));
 export_selected(output,TRUE);disk(output,"AC",2);g_assert_cmpint(gtk_tree_selection_count_selected_rows(selection()),==,1);
 gtk_tree_selection_unselect_all(selection());select_name("A");select_name("B");gchar *backup=g_strconcat(store,".backup",NULL);g_assert_cmpint(g_rename(store,backup),==,0);g_assert_cmpint(g_mkdir(store,0700),==,0);gtk_button_clicked(GTK_BUTTON(button(page,"Delete")));g_assert_cmpint(gtk_tree_selection_count_selected_rows(selection()),==,2);g_assert_cmpint(g_rmdir(store),==,0);g_assert_cmpint(g_rename(backup,store),==,0);g_free(backup);
 gtk_button_clicked(GTK_BUTTON(button(page,"Delete")));g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,1);
 GKeyFile *k=g_key_file_new();g_assert_true(g_key_file_load_from_file(k,store,G_KEY_FILE_NONE,NULL));g_assert_cmpint(g_key_file_get_integer(k,"notes","count",NULL),==,2);g_key_file_unref(k);
 gtk_entry_set_text(GTK_ENTRY(query),"absent");m=menu();g_assert_false(gtk_widget_get_sensitive(command(m,3)));destination=source;change_selection=FALSE;picks=0;g_timeout_add(100,pick,NULL);gchar *all=g_strconcat(output,".all.conf",NULL);destination=all;gtk_menu_item_activate(GTK_MENU_ITEM(command(m,4)));disk(all,"C",1);g_free(all);
 gtk_entry_set_text(GTK_ENTRY(query),"");m=menu();gtk_menu_item_activate(GTK_MENU_ITEM(command(m,6)));g_assert_cmpint(gtk_tree_selection_count_selected_rows(selection()),==,1);gtk_menu_item_activate(GTK_MENU_ITEM(command(m,7)));g_assert_cmpint(gtk_tree_selection_count_selected_rows(selection()),==,0);gtk_button_clicked(GTK_BUTTON(button(page,"Export selected…")));
 select_name("C");appearance_checks(doc);gtk_tree_selection_unselect_all(selection());close_file_checks();command_checks(doc);gtk_entry_set_text(GTK_ENTRY(query),"ExitKeep");gtk_button_clicked(GTK_BUTTON(button(page,"Add/update")));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);m=menu();g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(command(m,3))),==,"Экспортировать выбранные…");g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(command(m,8))),==,"Удалить все закладки…");g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(named(m,"insight-bookmark-appearance"))),==,"Шрифт и цвет…");g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(command(m,10))),==,"Удалять закладки при закрытии файла");
 g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(command(m,11))),==,"Параметры закладок…");
 options_apply=FALSE;options_open(FALSE);
 g_print("Bookmark selection GUI passed: transactional options, Ctrl+Q, multi-selection, frozen selected export with anchors, export all ignores filters, atomic batch delete rollback, global clips retained, menu sensitivity, selection commands and persistent bookmark appearance\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
static gboolean timeout(gpointer data) { (void)data;g_error("Bookmark selection timeout");return G_SOURCE_REMOVE; }
int main(void) { if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;gchar *dir=g_dir_make_tmp("bookmark-selection-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL),*folder=g_build_filename(profile,"insight",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_mkdir_with_parents(folder,0700);source=g_build_filename(dir,"source.c",NULL);store=g_build_filename(folder,"notes.conf",NULL);output=g_build_filename(dir,"selected.conf",NULL);g_file_set_contents(source,"int function(void) {\nreturn 1;\n}\n",-1,NULL);g_file_set_contents(config,"[geany]\nconfirm_exit=false\nuse_native_dialogs=false\n",-1,NULL);GKeyFile *k=g_key_file_new();g_key_file_set_integer(k,"notes","version",1);g_key_file_set_integer(k,"notes","count",4);
 for(guint i=0;i<4;i++) { gchar *group=g_strdup_printf("note-%u",i),name[2]={(char)('A'+i),0};g_key_file_set_integer(k,group,"kind",i==3 ? 2 : 1);g_key_file_set_string(k,group,"name",name);g_key_file_set_string(k,group,"path",i==3 ? "" : source);g_key_file_set_integer(k,group,"line",i==3 ? 0 : 2);g_key_file_set_string(k,group,"text",i==3 ? "clip" : "return 1;");if(i!=3) { g_key_file_set_string(k,group,"anchor-name","function");g_key_file_set_string(k,group,"anchor-scope","");g_key_file_set_integer(k,group,"anchor-kind",1);g_key_file_set_uint64(k,group,"anchor-offset",1); }g_free(group); }
 gchar *bytes=g_key_file_to_data(k,NULL,NULL);g_file_set_contents(store,bytes,-1,NULL);g_free(bytes);g_key_file_unref(k);{ gchar *prefs=g_build_filename(folder,"workbench.conf",NULL);gchar *initial=g_strdup_printf("[workbench]\nversion=1\nconfirm-bookmark-delete=%s\ndelete-bookmarks-on-close=%s\n[bookmarks-style]\nversion=1\nenabled=true\nfont=Monospace 11\nforeground=#234567\nbackground=#f1e2d3\n",g_getenv("GEANY_TEST_BOOKMARK_CONFIRM_RESTORE") ? "true" : "false",g_getenv("GEANY_TEST_BOOKMARK_CLOSE_RESTORE") ? "true" : "false");g_file_set_contents(prefs,initial,-1,NULL);g_free(initial);g_free(prefs); }gchar *argv[]={"bookmark-selection","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(300,tick,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_assert_cmpuint(total_notes(),==,2);gchar *prefs=g_build_filename(folder,"workbench.conf",NULL);k=g_key_file_new();g_assert_true(g_key_file_load_from_file(k,prefs,G_KEY_FILE_NONE,NULL));g_assert_true(g_key_file_get_boolean(k,"workbench","confirm-bookmark-delete",NULL));g_assert_true(g_key_file_get_boolean(k,"workbench","delete-bookmarks-on-close",NULL));g_assert_true(g_key_file_get_boolean(k,"bookmarks-style","enabled",NULL));gchar *saved_font=g_key_file_get_string(k,"bookmarks-style","font",NULL);g_assert_cmpstr(saved_font,==,"Monospace 14");g_free(saved_font);g_key_file_unref(k);g_free(prefs);return result; }
