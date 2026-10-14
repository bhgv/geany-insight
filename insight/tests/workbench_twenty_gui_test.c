/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*local,*project,*files,*entry;static gchar *profile,*source,*other,*settings;static guint phase;static gint caret,preview_line;static guint doc_id;
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!strcmp(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_ITEM(root)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
static gboolean row(GtkTreeModel *model,const gchar *name,GtkTreeIter *result) { GtkTreeIter it;for(gboolean v=gtk_tree_model_get_iter_first(model,&it);v;v=gtk_tree_model_iter_next(model,&it)) { gchar *n=NULL;gtk_tree_model_get(model,&it,0,&n,-1);gboolean match=!g_strcmp0(n,name);g_free(n);if(match) { *result=it;return TRUE; }GtkTreeIter child;if(gtk_tree_model_iter_children(model,&child,&it)) do { gchar *n=NULL;gtk_tree_model_get(model,&child,0,&n,-1);gboolean match=!g_strcmp0(n,name);g_free(n);if(match) { *result=child;return TRUE; } }while(gtk_tree_model_iter_next(model,&child)); }return FALSE; }
static void select_row(GtkWidget *view,const gchar *name) { GtkTreeModel *m=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter it;g_assert_true(row(m,name,&it));GtkTreePath *path=gtk_tree_model_get_path(m,&it);gtk_tree_view_expand_to_path(GTK_TREE_VIEW(view),path);gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE);gtk_tree_path_free(path); }
static void key(guint keyval,guint modifiers) { GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(window));event->key.keyval=keyval;event->key.state=modifiers;gdk_event_set_device(event,gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default())));gtk_main_do_event(event);gdk_event_free(event); }
static GtkWidget *popup(GtkWidget *view,gboolean file) { gtk_widget_grab_focus(view);key(GDK_KEY_Menu,0);GtkWidget *menu=g_object_get_data(G_OBJECT(view),file ? "insight-file-popup" : "insight-symbol-popup");g_assert_nonnull(menu);gtk_menu_popdown(GTK_MENU(menu));return menu; }
static GtkWidget *command(GtkWidget *menu,guint action,gboolean file) { gchar *name=g_strdup_printf(file ? "insight-file-command-%u" : "insight-symbol-command-%u",action);GtkWidget *item=g_object_get_data(G_OBJECT(menu),name);g_free(name);g_assert_nonnull(item);return item; }
static void act(GtkWidget *view,guint action,gboolean file) { GtkWidget *item=command(popup(view,file),action,file);g_assert_true(gtk_widget_get_sensitive(item));gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void clipboard(const gchar *expected) { gchar *text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,expected);g_free(text); }
static void unchanged(GeanyDocument *doc) { g_assert_cmpuint(doc->id,==,doc_id);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);g_assert_cmpint(sci_get_current_line(SCINTILLA(named(window,"insight-context-code"))),==,preview_line); }
static GtkTreeViewColumn *column(GtkWidget *view,gint field) { GList *list=gtk_tree_view_get_columns(GTK_TREE_VIEW(view));GtkTreeViewColumn *result=NULL;for(GList *p=list;p;p=p->next) if(gtk_tree_view_column_get_sort_column_id(p->data)==field) result=p->data;g_list_free(list);g_assert_nonnull(result);return result; }
static gboolean timeout(gpointer data) { (void)data;g_error("Twenty-stage GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(other,FALSE,NULL,NULL);document_open_file(source,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!local) { local=named(window,"insight-local-symbols");project=named(window,"insight-project-symbols");files=named(window,"insight-project-files");entry=named(window,"insight-local-query"); }
 if(!gtk_widget_get_sensitive(local) || !gtk_widget_get_sensitive(files)) return G_SOURCE_CONTINUE;
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(local));GtkTreeIter it;

 if(phase==1) {
  if(!row(model,"run",&it) || gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(files)),NULL)!=2) return G_SOURCE_CONTINUE;
  g_assert_true(gtk_tree_view_column_get_visible(column(local,0)));select_row(local,"run");phase++;
 } else if(phase==2) {
  doc_id=doc->id;caret=sci_get_current_position(doc->editor->sci);preview_line=sci_get_current_line(SCINTILLA(named(window,"insight-context-code")));act(local,15,FALSE);clipboard("Demo::Widget::run");act(local,16,FALSE);clipboard(source);act(local,17,FALSE);clipboard("4");unchanged(doc);act(local,18,FALSE);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)),==,"\"run\"");phase++;
 } else if(phase==3) {
  if(!row(model,"run",&it) || row(model,"extra",&it)) return G_SOURCE_CONTINUE;
  select_row(local,"run");act(local,19,FALSE);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)),==,"scope:\"Demo::Widget\"");phase++;
 } else if(phase==4) {
  if(!row(model,"run",&it)) return G_SOURCE_CONTINUE;
  select_row(local,"run");act(local,20,FALSE);gchar *query=g_strdup_printf("file:\"%s\"",source);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)),==,query);g_free(query);phase++;
 } else if(phase==5) {
  if(!row(model,"extra",&it)) return G_SOURCE_CONTINUE;
  act(local,21,FALSE);phase++;
 } else if(phase==6) {
  select_row(local,"run");g_assert_true(row(model,"run",&it));GtkTreePath *path=gtk_tree_model_get_path(model,&it);gtk_tree_path_up(path);act(local,23,FALSE);g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(local),path));act(local,22,FALSE);g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(local),path));gtk_tree_path_free(path);act(local,24,FALSE);phase++;
 } else if(phase==7) {
  if(row(model,"run",&it)) return G_SOURCE_CONTINUE;
  g_assert_true(row(model,"Widget",&it));act(local,24,FALSE);phase++;
 } else if(phase==8) {
  if(!row(model,"run",&it)) return G_SOURCE_CONTINUE;
  GtkWidget *menu=popup(local,FALSE),*columns=gtk_menu_item_get_submenu(GTK_MENU_ITEM(command(menu,25,FALSE)));GtkWidget *name=named(columns,"insight-column-0"),*kind=named(columns,"insight-column-4");g_assert_false(gtk_widget_get_sensitive(name));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(kind),FALSE);g_assert_false(gtk_tree_view_column_get_visible(column(local,4)));unchanged(doc);
  GtkWidget *sort=named(window,"insight-files-sort");g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(named(sort,"insight-symbol-sort-field"))),==,1);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(sort,"insight-symbol-sort-descending"))));g_assert_cmpint(gtk_tree_view_column_get_fixed_width(column(files,1)),==,333);
  select_row(files,"a source.cpp");phase++;
 } else if(phase==9) {
  preview_line=sci_get_current_line(SCINTILLA(named(window,"insight-context-code")));gtk_widget_grab_focus(files);key(GDK_KEY_c,GEANY_PRIMARY_MOD_MASK);clipboard("a source.cpp");key(GDK_KEY_C,GEANY_PRIMARY_MOD_MASK|GDK_SHIFT_MASK);clipboard(source);key(GDK_KEY_f,GEANY_PRIMARY_MOD_MASK);g_assert_true(gtk_widget_has_focus(named(window,"insight-file-query")));act(files,3,TRUE);phase++;
 } else if(phase==10) {
  if(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(files)),NULL)!=1) return G_SOURCE_CONTINUE;
  unchanged(doc);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-files-count"))),==,"Files: 1");act(files,4,TRUE);phase++;
 } else if(phase==11) {
  if(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(files)),NULL)!=2) return G_SOURCE_CONTINUE;
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-files-count"))),==,"Files: 2");GtkWidget *sort=named(window,"insight-files-sort"),*field=named(sort,"insight-symbol-sort-field"),*direction=named(sort,"insight-symbol-sort-descending");gtk_combo_box_set_active(GTK_COMBO_BOX(field),0);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(direction),FALSE);gtk_tree_view_column_clicked(column(files,1));g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(field)),==,1);
  GtkWidget *menu=popup(files,TRUE),*columns=gtk_menu_item_get_submenu(GTK_MENU_ITEM(command(menu,6,TRUE)));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(columns,"insight-column-1")),FALSE);g_assert_false(gtk_tree_view_column_get_visible(column(files,1)));act(files,5,TRUE);g_assert_true(gtk_tree_view_column_get_visible(column(files,1)));g_assert_cmpint(gtk_tree_view_column_get_fixed_width(column(files,1)),==,300);gtk_tree_view_column_set_fixed_width(column(files,1),345);gtk_tree_view_move_column_after(GTK_TREE_VIEW(files),column(files,1),NULL);
  GtkWidget *history_button=named(window,"insight-files-query-history"),*history=GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(history_button)));gtk_entry_set_text(GTK_ENTRY(named(window,"insight-file-query")),"*.cpp");gtk_widget_hide(history);gtk_widget_show(history);GtkWidget *remember=named(history,"insight-symbol-history-remember");g_assert_true(gtk_widget_get_sensitive(remember));gtk_menu_item_activate(GTK_MENU_ITEM(remember));gtk_widget_hide(history);phase++;
 } else if(phase==12) {
  GtkWidget *history=GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(named(window,"insight-files-query-history"))));gtk_widget_show(history);GtkWidget *first=named(history,"insight-symbol-history-0");g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(first)),==,"*.cpp");gtk_widget_hide(history);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);phase++;
 } else if(phase==13) {
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-files-count"))),==,"Файлы: 2");GtkWidget *menu=popup(files,TRUE);g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(command(menu,2,TRUE))),==,"Копировать полный путь файла");select_row(files,"z.cpp");gtk_widget_grab_focus(files);key(GDK_KEY_Return,GEANY_PRIMARY_MOD_MASK);phase++;
 } else {
  if(!doc->file_name || strcmp(doc->file_name,other)) return G_SOURCE_CONTINUE;
  g_print("Twenty-stage GUI passed: native C++ qualification, clipboard path/line, exact name/scope/file actions, group controls, inverted categories, persistent column visibility, independent file layout/sort/history, native keyboard routes, file counts and RU menus, centered file activation\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("twenty-gui-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL);g_free(cwd);g_free(name);profile=g_build_filename(directory,"profile",NULL);source=g_build_filename(directory,"a source.cpp",NULL);other=g_build_filename(directory,"z.cpp",NULL);gchar *sd=g_build_filename(profile,"insight",NULL);g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);settings=g_build_filename(sd,"workbench.conf",NULL);g_free(sd);
 g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nfollow=false\nproject-visible=true\nfile-symbols-grouped=true\nfile-symbol-types=1\nfile-symbols-column-order=0;3;4;\nfile-symbols-column-widths=220;70;70;\nfile-symbols-column-visible=0;1;1;\nproject-files-column-order=1;0;\nproject-files-column-widths=333;160;\nproject-files-sort-column=1\nproject-files-sort-descending=true\nproject-files-query-history=*.cpp;\n",-1,NULL));g_assert_true(g_file_set_contents(source,"namespace Demo {\nclass Widget {\npublic:\n int run() { return 1; }\n};\n}\nint extra() { return 0; }\n",-1,NULL));g_assert_true(g_file_set_contents(other,"int secondary() { return 2; }\n",-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"twenty-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(50,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL));gsize count=0;gint *visible=g_key_file_get_integer_list(key,"workbench","file-symbols-column-visible",&count,NULL);g_assert_cmpuint(count,==,3);g_assert_cmpint(visible[2],==,0);g_free(visible);gint *widths=g_key_file_get_integer_list(key,"workbench","project-files-column-widths",&count,NULL);g_assert_cmpuint(count,==,2);g_assert_cmpint(widths[0],==,345);g_free(widths);g_assert_cmpint(g_key_file_get_integer(key,"workbench","project-files-sort-column",NULL),==,1);gchar **history=g_key_file_get_string_list(key,"workbench","project-files-query-history",&count,NULL);g_assert_cmpuint(count,==,1);g_assert_cmpstr(history[0],==,"*.cpp");g_strfreev(history);g_key_file_unref(key);g_free(directory);g_free(profile);g_free(source);g_free(other);g_free(settings);return result;
}
