/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>

#undef SSM
#define SSM(sci,msg,w,l) scintilla_send_message((sci),(msg),(w),(l))
static guint phase,original_id; static gint initial_size; static gchar *test_directory,*test_project,*bad_project,*second_project;
static GtkWidget *window,*overview_menu;
static guint preview_heartbeats; static gint large_last; static guint heartbeat_source;
static GtkWidget *find_named(GtkWidget *root,const char *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root)); GtkWidget *found=NULL; for(GList *p=children;p && !found;p=p->next) found=find_named(p->data,name); g_list_free(children); if(found) return found; }
 return NULL;
}
static GtkWidget *find_button(GtkWidget *root,const char *label) {
 if(GTK_IS_BUTTON(root) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(root)),label)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root)); GtkWidget *found=NULL; for(GList *p=children;p && !found;p=p->next) found=find_button(p->data,label); g_list_free(children); if(found) return found; } return NULL;
}
static gboolean edit_membership(gpointer data) {
 gboolean cancel=GPOINTER_TO_INT(data); GList *windows=gtk_window_list_toplevels(); GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(GTK_IS_DIALOG(p->data) && !g_strcmp0(gtk_window_get_title(GTK_WINDOW(p->data)),"Add and Remove Project Files")) dialog=p->data;
 g_list_free(windows); g_assert_nonnull(dialog);
 if(cancel) { GtkWidget *button=find_button(dialog,"Remove all"); g_assert_nonnull(button); gtk_button_clicked(GTK_BUTTON(button)); gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL); }
 else { GtkWidget *view=find_named(dialog,"insight-project-membership-files"); g_assert_nonnull(view); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,1); gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(view))); GtkWidget *button=find_button(dialog,"Remove selected"); g_assert_nonnull(button); gtk_button_clicked(GTK_BUTTON(button)); gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_APPLY); }
 return G_SOURCE_REMOVE;
}
static gboolean pick_history(gpointer data) {
 GtkWidget *button=data; GList *menus=gtk_menu_get_for_attach_widget(button); g_assert_nonnull(menus); GtkWidget *item=find_named(menus->data,"insight-search-history-0"); g_assert_nonnull(item); gtk_menu_shell_activate_item(GTK_MENU_SHELL(menus->data),item,TRUE); return G_SOURCE_REMOVE;
}
static gboolean select_project(gpointer data) {
 const gchar *target=GPOINTER_TO_INT(data)==2 ? second_project : (data ? bad_project : test_project); GList *windows=gtk_window_list_toplevels(); GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(GTK_IS_FILE_CHOOSER(p->data) && !g_strcmp0(gtk_window_get_title(GTK_WINDOW(p->data)),"Open Project")) dialog=p->data;
 g_list_free(windows); g_assert_nonnull(dialog);
 gchar *filename=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
 if(g_strcmp0(filename,target)) { g_assert_true(gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog),target)); g_free(filename); return G_SOURCE_CONTINUE; }
 g_free(filename); gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); return G_SOURCE_REMOVE;
}
static gboolean dismiss_invalid(gpointer data) {
 static gboolean rejected; (void)data; GList *windows=gtk_window_list_toplevels(); GtkWidget *message=NULL,*chooser=NULL;
 for(GList *p=windows;p;p=p->next) { if(GTK_IS_MESSAGE_DIALOG(p->data)) message=p->data; else if(GTK_IS_FILE_CHOOSER(p->data)) chooser=p->data; }
 g_list_free(windows);
 if(message) { rejected=TRUE; gtk_dialog_response(GTK_DIALOG(message),GTK_RESPONSE_OK); return G_SOURCE_CONTINUE; }
 if(rejected && chooser) { rejected=FALSE; gtk_dialog_response(GTK_DIALOG(chooser),GTK_RESPONSE_CANCEL); return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static void project_empty(void) { GtkWidget *view=find_named(window,"insight-project-files"),*symbols=find_named(window,"insight-project-symbols"); g_assert_nonnull(view); g_assert_nonnull(symbols); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,0); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(symbols)),NULL),==,0); }
static ScintillaObject *map_for(GeanyDocument *doc) {
 GtkWidget *pane=gtk_widget_get_parent(GTK_WIDGET(doc->editor->sci)); g_assert_true(GTK_IS_PANED(pane)); GtkWidget *host=gtk_paned_get_child2(GTK_PANED(pane)); g_assert_true(GTK_IS_OVERLAY(host));
 return SCINTILLA(gtk_bin_get_child(GTK_BIN(host)));
}
static void same_text(GeanyDocument *doc) {
 ScintillaObject *map=map_for(doc); gchar *source=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1),*copy=sci_get_contents(map,sci_get_length(map)+1);
 g_assert_cmpuint(strlen(copy),==,strlen(source)); g_assert_true(!strcmp(copy,source)); g_free(source); g_free(copy); g_assert_false(SSM(doc->editor->sci,SCI_GETREADONLY,0,0)); g_assert_true(SSM(map,SCI_GETREADONLY,0,0));
}
static void screenshot(void) {
 if(!g_getenv("GEANY_GUI_SCREENSHOT")) return;
 GdkWindow *root=gdk_get_default_root_window(); GdkPixbuf *picture=gdk_pixbuf_get_from_window(root,0,0,MIN(1280,gdk_window_get_width(root)),MIN(900,gdk_window_get_height(root))); if(!picture) return;
 gint w=gdk_pixbuf_get_width(picture),h=gdk_pixbuf_get_height(picture),stride=gdk_pixbuf_get_rowstride(picture),channels=gdk_pixbuf_get_n_channels(picture); guchar *pixels=gdk_pixbuf_get_pixels(picture); GString *ppm=g_string_new(NULL); g_string_append_printf(ppm,"P6\n%d %d\n255\n",w,h);
 for(gint y=0;y<h;y++) for(gint x=0;x<w;x++) g_string_append_len(ppm,(const gchar *)pixels+y*stride+x*channels,3);
 g_file_set_contents("overview-gui-smoke.ppm",ppm->str,ppm->len,NULL); g_string_free(ppm,TRUE); g_object_unref(picture);
}
static gboolean preview_heartbeat(gpointer data) { (void)data; preview_heartbeats++; return G_SOURCE_CONTINUE; }
static gboolean timeout_fail(gpointer data) { (void)data; g_error("Overview GUI smoke timed out"); return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data; GeanyDocument *doc=document_get_current(); g_assert_nonnull(doc);
 if(phase==0) {
  const char *icons[]={"classviewer-class","classviewer-macro","classviewer-member","classviewer-method","classviewer-namespace","classviewer-other","classviewer-struct","classviewer-var","geany-save-all","geany-build","geany-close-all"};
  for(guint i=0;i<G_N_ELEMENTS(icons);i++) { GError *error=NULL; GdkPixbuf *icon=gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),icons[i],16,0,&error); g_assert_no_error(error); g_assert_nonnull(icon); g_object_unref(icon); }

  GString *source=g_string_new(NULL); for(guint i=0;i<500;i++) g_string_append_printf(source,"int function_%u(void) { return %u; }\n",i,i);
  gchar *path=g_build_filename(test_directory,"overview-smoke.c",NULL); g_assert_true(g_file_set_contents(path,source->str,-1,NULL)); doc=document_open_file(path,FALSE,filetypes_lookup_by_name("C"),NULL); g_free(path); g_string_free(source,TRUE); original_id=doc->id;
  window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci)); gtk_window_resize(GTK_WINDOW(window),1100,800);
  GtkWidget *view=ui_lookup_widget(window,"menu_view1_menu"); GList *items=gtk_container_get_children(GTK_CONTAINER(view));
  for(GList *p=items;p;p=p->next) if(GTK_IS_CHECK_MENU_ITEM(p->data) && !g_strcmp0(gtk_menu_item_get_label(GTK_MENU_ITEM(p->data)),"Overview")) overview_menu=p->data;
  g_list_free(items); g_assert_nonnull(overview_menu); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview_menu),TRUE);
 } else if(phase==1) {
  same_text(doc); GtkNotebook *sidebar=GTK_NOTEBOOK(ui_lookup_widget(window,"notebook3")); GtkWidget *page=gtk_notebook_get_nth_page(sidebar,gtk_notebook_get_current_page(sidebar)); g_assert_cmpstr(gtk_notebook_get_tab_label_text(sidebar,page),==,"File Symbols"); g_assert_nonnull(doc->tm_file); g_assert_cmpuint(doc->tm_file->tags_array->len,>,0); g_assert_cmpint(SSM(map_for(doc),SCI_GETSTYLEAT,0,0),!=,0);
  SSM(doc->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"/* данные saving writing */\n"); SSM(doc->editor->sci,SCI_SETSEL,0,3); SSM(doc->editor->sci,SCI_SETFIRSTVISIBLELINE,200,0);
 } else if(phase==2) {
  same_text(doc); ScintillaObject *map=map_for(doc); gint first=(gint)SSM(map,SCI_GETFIRSTVISIBLELINE,0,0),main_first=(gint)SSM(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0);
  g_assert_cmpint(first,<=,main_first); g_assert_cmpint(first+SSM(map,SCI_LINESONSCREEN,0,0),>,main_first);
  screenshot(); initial_size=(gint)SSM(map,SCI_STYLEGETSIZEFRACTIONAL,STYLE_DEFAULT,0);
  gint caret=sci_get_current_position(doc->editor->sci); GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS); event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map))); event->button.button=1; event->button.x=10; event->button.y=10; gtk_widget_event(GTK_WIDGET(map),event); gdk_event_free(event);
  event=gdk_event_new(GDK_BUTTON_RELEASE); event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map))); event->button.button=1; event->button.x=10; event->button.y=10; gtk_widget_event(GTK_WIDGET(map),event); gdk_event_free(event);
  g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret); g_assert_cmpint(SSM(doc->editor->sci,SCI_GETANCHOR,0,0),==,0); g_assert_cmpint(SSM(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0),!=,main_first);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview_menu),FALSE); g_assert_false(gtk_widget_get_visible(gtk_widget_get_parent(GTK_WIDGET(map)))); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview_menu),TRUE);
  event=gdk_event_new(GDK_SCROLL); event->scroll.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map))); event->scroll.state=GDK_CONTROL_MASK; event->scroll.direction=GDK_SCROLL_UP; gtk_widget_event(GTK_WIDGET(map),event); gdk_event_free(event);
 } else if(phase==3) {
  same_text(doc); g_assert_cmpint(SSM(map_for(doc),SCI_STYLEGETSIZEFRACTIONAL,STYLE_DEFAULT,0),>,initial_size);
  GtkWidget *keywords=find_named(window,"insight-search-keywords"),*variations=find_named(window,"insight-search-variations"),*scope=find_named(window,"insight-search-scope"),*query=find_named(window,"insight-project-query"); g_assert_nonnull(keywords); g_assert_nonnull(variations); g_assert_nonnull(scope); g_assert_nonnull(query); g_assert_false(gtk_widget_get_sensitive(variations));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(keywords),TRUE); g_assert_true(gtk_widget_get_sensitive(variations)); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(variations),TRUE); gtk_combo_box_set_active(GTK_COMBO_BOX(scope),2); gtk_entry_set_text(GTK_ENTRY(query),"save write"); g_signal_emit_by_name(query,"activate");
 } else if(phase==4) {
  GtkWidget *view=find_named(window,"insight-search-results"),*preview=find_named(window,"insight-context-code"); g_assert_nonnull(view); g_assert_nonnull(preview); GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view)); g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,2);
  GtkTreeIter iter; g_assert_true(gtk_tree_model_get_iter_first(model,&iter)); gulong start,end; gtk_tree_model_get(model,&iter,3,&start,4,&end,-1); g_assert_cmpuint(end-start,==,6); GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1); gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE); gtk_tree_path_free(path); g_assert_cmpint(SSM(SCINTILLA(preview),SCI_INDICATORVALUEAT,28,start),>,0); g_assert_cmpint(SSM(SCINTILLA(preview),SCI_INDICATORVALUEAT,29,start),>,0);
  gint before=sci_get_current_position(doc->editor->sci),anchor=SSM(doc->editor->sci,SCI_GETANCHOR,0,0);
  GdkEvent *browse=gdk_event_new(GDK_KEY_PRESS); browse->key.window=g_object_ref(gtk_widget_get_window(view)); browse->key.state=GDK_MOD1_MASK; browse->key.keyval=GDK_KEY_Down; gtk_widget_event(view,browse); gdk_event_free(browse);
  g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&iter)); GtkTreePath *selected=gtk_tree_model_get_path(model,&iter); g_assert_cmpint(gtk_tree_path_get_indices(selected)[0],==,1); gtk_tree_path_free(selected); gtk_tree_model_get(model,&iter,3,&start,4,&end,-1); g_assert_cmpuint(end-start,==,7); gulong column; gtk_tree_model_get(model,&iter,9,&column,-1); g_assert_cmpuint(column,==,18); g_assert_cmpint(SSM(SCINTILLA(preview),SCI_INDICATORVALUEAT,29,start),>,0); g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,before); g_assert_cmpint(SSM(doc->editor->sci,SCI_GETANCHOR,0,0),==,anchor);
  GdkEvent *activate=gdk_event_new(GDK_KEY_PRESS); activate->key.window=g_object_ref(gtk_widget_get_window(view)); activate->key.state=GDK_CONTROL_MASK; activate->key.keyval=GDK_KEY_l; gtk_widget_event(view,activate); gdk_event_free(activate);
  g_assert_cmpint(SSM(doc->editor->sci,SCI_GETSELECTIONSTART,0,0),==,start); g_assert_cmpint(SSM(doc->editor->sci,SCI_GETSELECTIONEND,0,0),==,end); g_assert_cmpint(SSM(doc->editor->sci,SCI_INDICATORVALUEAT,28,start),>,0); g_assert_cmpint(SSM(doc->editor->sci,SCI_INDICATORVALUEAT,29,start),>,0);
  const char *change="/* edited after search */\n"; SSM(doc->editor->sci,SCI_APPENDTEXT,strlen(change),(sptr_t)change);
 } else if(phase==5) {
  GtkWidget *view=find_named(window,"insight-search-results"),*preview=find_named(window,"insight-context-code"),*query=find_named(window,"insight-project-query"); GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view)); GtkTreeIter iter; g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&iter)); gulong start,end; gtk_tree_model_get(model,&iter,3,&start,4,&end,-1);
  gint before=sci_get_current_position(doc->editor->sci),anchor=SSM(doc->editor->sci,SCI_GETANCHOR,0,0); GtkTreePath *path=gtk_tree_model_get_path(model,&iter); gtk_tree_view_row_activated(GTK_TREE_VIEW(view),path,NULL); gtk_tree_path_free(path);
  g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,before); g_assert_cmpint(SSM(doc->editor->sci,SCI_GETANCHOR,0,0),==,anchor); g_assert_cmpint(SSM(SCINTILLA(preview),SCI_INDICATORVALUEAT,28,start),==,0); g_assert_cmpint(SSM(SCINTILLA(preview),SCI_INDICATORVALUEAT,29,start),==,0);
  gtk_entry_set_text(GTK_ENTRY(query),"("); g_signal_emit_by_name(query,"activate"); g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,2);
  GtkWidget *context_page=find_named(window,"insight-context"); g_assert_nonnull(context_page); GtkWidget *bottom=gtk_widget_get_parent(context_page); g_assert_true(GTK_IS_NOTEBOOK(bottom)); g_assert_cmpstr(gtk_notebook_get_tab_label_text(GTK_NOTEBOOK(bottom),context_page),==,"Context"); gtk_notebook_set_current_page(GTK_NOTEBOOK(bottom),gtk_notebook_page_num(GTK_NOTEBOOK(bottom),context_page));
 } else if(phase==6) {
  GtkWidget *preview=find_named(window,"insight-context-code");
  ScintillaObject *context=SCINTILLA(preview); gchar *text=sci_get_contents(context,sci_get_length(context)+1),*name=strstr(text,"function_0"); g_assert_nonnull(name); gint position=(gint)(name-text); g_free(text); g_assert_cmpint(SSM(context,SCI_INDICATORVALUEAT,31,position),>,0); g_assert_cmpint(SSM(context,SCI_GETMARGINWIDTHN,0,0),>,0);
  gdouble x=(gdouble)SSM(context,SCI_POINTXFROMPOSITION,0,position)+1,y=(gdouble)SSM(context,SCI_POINTYFROMPOSITION,0,position)+1;
  GdkEvent *event=gdk_event_new(GDK_MOTION_NOTIFY); event->motion.window=g_object_ref(gtk_widget_get_window(preview)); event->motion.x=x; event->motion.y=y; gtk_widget_event(preview,event); gdk_event_free(event); g_assert_cmpint(SSM(context,SCI_GETCURSOR,0,0),==,8);
  SSM(doc->editor->sci,SCI_SETSEL,0,0); event=gdk_event_new(GDK_BUTTON_PRESS); event->button.window=g_object_ref(gtk_widget_get_window(preview)); event->button.button=1; event->button.x=x; event->button.y=y; gtk_widget_event(preview,event); gdk_event_free(event); g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,1);
  GtkWidget *history=find_named(window,"insight-search-history"); g_assert_nonnull(history); gtk_button_clicked(GTK_BUTTON(history)); g_timeout_add(150,pick_history,history);
  document_set_text_changed(doc,FALSE); GeanyDocument *second=document_new_file(NULL,filetypes_lookup_by_name("C"),"int second(void) { return 2; }\n"); g_assert_nonnull(second); g_assert_true(document_close(document_find_by_id(original_id)));
 } else if(phase==7) {
  g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(find_named(window,"insight-project-query"))),==,"save write");
  same_text(doc); document_set_text_changed(doc,FALSE); GtkWidget *view=find_named(window,"insight-project-files"); g_assert_nonnull(view); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,1); GtkWidget *button=find_button(window,"Add and Remove Project Files…"); g_assert_nonnull(button); g_timeout_add(150,edit_membership,NULL); gtk_button_clicked(GTK_BUTTON(button));
 } else if(phase==8) {
  project_empty(); gchar *path=g_build_filename(test_directory,"overview-smoke.c",NULL); g_assert_nonnull(document_open_file(path,FALSE,filetypes_lookup_by_name("C"),NULL)); g_free(path);
 } else if(phase==9) {
  project_empty(); GtkWidget *button=find_button(window,"Add and Remove Project Files…"); g_assert_nonnull(button); g_timeout_add(150,edit_membership,GINT_TO_POINTER(1)); gtk_button_clicked(GTK_BUTTON(button));
 } else if(phase==10) {
  project_empty(); GKeyFile *key=g_key_file_new(); g_assert_true(g_key_file_load_from_file(key,test_project,G_KEY_FILE_NONE,NULL)); g_assert_true(g_key_file_get_boolean(key,"insight-project-files","automatic",NULL)); gsize count=0; gchar **excluded=g_key_file_get_string_list(key,"insight-project-files","excluded-files",&count,NULL); g_assert_cmpuint(count,==,1); g_assert_cmpstr(excluded[0],==,"overview-smoke.c"); g_strfreev(excluded); g_key_file_unref(key); document_set_text_changed(doc,FALSE);

  GtkWidget *close=ui_lookup_widget(window,"project_close1"),*open=ui_lookup_widget(window,"project_open1"); g_assert_true(GTK_IS_MENU_ITEM(close)); g_assert_true(GTK_IS_MENU_ITEM(open)); gtk_menu_item_activate(GTK_MENU_ITEM(close));  g_timeout_add(150,select_project,NULL); gtk_menu_item_activate(GTK_MENU_ITEM(open));
 } else if(phase==11) {
  g_assert_nonnull(strstr(gtk_window_get_title(GTK_WINDOW(window)),"GUI smoke")); project_empty(); document_set_text_changed(doc,FALSE);
  g_timeout_add(150,dismiss_invalid,NULL); g_timeout_add(150,select_project,GINT_TO_POINTER(1)); gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));
 } else if(phase==12) {
  g_assert_nonnull(strstr(gtk_window_get_title(GTK_WINDOW(window)),"GUI smoke")); project_empty(); document_set_text_changed(doc,FALSE);
  g_assert_true(g_file_set_contents(bad_project,"[project\nname=Broken\n",-1,NULL)); g_timeout_add(150,dismiss_invalid,NULL); g_timeout_add(150,select_project,GINT_TO_POINTER(1)); gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));
 } else if(phase==13) {
  g_assert_nonnull(strstr(gtk_window_get_title(GTK_WINDOW(window)),"GUI smoke")); project_empty(); document_set_text_changed(doc,FALSE);
  g_timeout_add(150,select_project,GINT_TO_POINTER(2)); gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));
 } else if(phase==14) {
  g_assert_nonnull(strstr(gtk_window_get_title(GTK_WINDOW(window)),"GUI second")); GtkWidget *view=find_named(window,"insight-project-files"); g_assert_nonnull(view); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,1); document_set_text_changed(doc,FALSE);
  g_timeout_add(150,select_project,NULL); gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));
 } else if(phase==15) {
  g_assert_nonnull(strstr(gtk_window_get_title(GTK_WINDOW(window)),"GUI smoke")); project_empty(); document_set_text_changed(doc,FALSE);
  g_timeout_add(150,select_project,GINT_TO_POINTER(2)); gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));
 } else if(phase==16) {
  GString *large=g_string_sized_new(1400000); for(guint i=0;i<100000;i++) g_string_append(large,"function_0();\n"); large_last=(gint)large->len-14; gchar *directory=g_path_get_dirname(test_project),*filename=g_build_filename(directory,"large-preview.c",NULL); g_free(directory); g_assert_true(g_file_set_contents(filename,large->str,large->len,NULL)); g_string_free(large,TRUE);
  GtkWidget *view=find_named(window,"insight-project-files"); GtkListStore *model=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view))); GtkTreeIter iter; gtk_list_store_append(model,&iter); gtk_list_store_set(model,&iter,0,"large-preview.c",1,filename,-1); GtkTreePath *path=gtk_tree_model_get_path(GTK_TREE_MODEL(model),&iter); heartbeat_source=g_timeout_add(5,preview_heartbeat,NULL); gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE); gtk_tree_path_free(path); g_free(filename);
 } else if(phase==17) {
  ScintillaObject *preview=SCINTILLA(find_named(window,"insight-context-code")); g_assert_cmpint(sci_get_length(preview),==,1400000); if(!SSM(preview,SCI_INDICATORVALUEAT,31,large_last)) return G_SOURCE_CONTINUE; g_assert_cmpuint(preview_heartbeats,>,1); g_source_remove(heartbeat_source); heartbeat_source=0;
  GtkWidget *page=gtk_widget_get_parent(GTK_WIDGET(preview)); while(page && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) page=gtk_widget_get_parent(page); g_assert_nonnull(page); GtkWidget *notebook=gtk_widget_get_parent(page); g_assert_true(GTK_IS_NOTEBOOK(notebook)); g_assert_cmpstr(gtk_notebook_get_tab_label_text(GTK_NOTEBOOK(notebook),page),==,"Context");
  gchar *directory=g_path_get_dirname(test_project),*filename=g_build_filename(directory,"double-click-large.txt",NULL); g_free(directory); gchar *large=g_malloc0(2200001); memset(large,'x',2200000); g_assert_true(g_file_set_contents(filename,large,2200000,NULL)); g_free(large);
  GtkWidget *view=find_named(window,"insight-project-files"); GtkListStore *model=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view))); GtkTreeIter iter; gtk_list_store_append(model,&iter); gtk_list_store_set(model,&iter,0,"double-click-large.txt",1,filename,-1); GtkTreePath *path=gtk_tree_model_get_path(GTK_TREE_MODEL(model),&iter); gtk_tree_view_row_activated(GTK_TREE_VIEW(view),path,gtk_tree_view_get_column(GTK_TREE_VIEW(view),0)); gtk_tree_path_free(path); g_assert_cmpstr(document_get_current()->file_name,==,filename); g_assert_cmpint(sci_get_length(document_get_current()->editor->sci),==,2200000); g_free(filename);
 } else if(phase==18) {
  gchar *filename=g_build_filename(test_directory,"overview-smoke.c",NULL); GeanyDocument *target=document_open_file(filename,FALSE,NULL,NULL); g_free(filename); g_assert_nonnull(target); gtk_widget_grab_focus(GTK_WIDGET(target->editor->sci)); const gchar *source="int worker_first(void) { return 1; }\nint worker_second(void) { return 2; }\nint caller(void) { return worker_first() + worker_second(); }\n"; sci_set_text(target->editor->sci,source); sci_set_current_position(target->editor->sci,(gint)(strstr(source,"return worker_first")-source)+9,FALSE); document_set_text_changed(target,FALSE);
 } else if(phase==19) {
  ScintillaObject *preview=SCINTILLA(find_named(window,"insight-context-code")); if(sci_get_current_line(preview)!=0 || !SSM(preview,SCI_INDICATORVALUEAT,28,4)) return G_SOURCE_CONTINUE; gchar *source=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1); gint position=(gint)(strstr(source,"+ worker_second")-source)+4; sci_set_current_position(doc->editor->sci,position,FALSE); g_free(source);
 } else if(phase==20) {
  ScintillaObject *preview=SCINTILLA(find_named(window,"insight-context-code")); if(sci_get_current_line(preview)!=1) return G_SOURCE_CONTINUE;
  GtkWidget *quit=ui_lookup_widget(window,"menu_quit1"); g_assert_true(GTK_IS_MENU_ITEM(quit)); gtk_menu_item_activate(GTK_MENU_ITEM(quit)); g_print("Overview GUI smoke passed: editor icons, syntax, independent read-only, edits, scrolling, toggling, keyword variations, saved query history, preview-only result navigation, exact hit opening, result columns, stale result rejection, Context links, project Apply/Cancel, menu Open Project, invalid/malformed project preserves session, UTF-8 paths, project switching, responsive 1.4 MiB file selection, Context notebook tab, double-click opens >2 MiB file, delayed worker definition lookup, tab close\n"); return G_SOURCE_REMOVE;
 }
 phase++; return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY"))) { g_print("Set GEANY_RUN_GUI_TESTS=1 and provide a display to run the real editor GUI smoke\n"); return 77; }
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("overview-gui-profile-%u",g_random_int()),*profile=g_build_filename(cwd,name,NULL); g_free(cwd); g_free(name); g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);
 gchar *config=g_build_filename(profile,"geany.conf",NULL); g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nuse_native_dialogs=false\n",-1,NULL)); g_free(config);
 test_directory=g_build_filename(profile,"исходники с пробелом",NULL); g_assert_cmpint(g_mkdir_with_parents(test_directory,0700),==,0); test_project=g_build_filename(profile,"проект тест.geany",NULL); g_assert_true(g_file_set_contents(test_project,"[project]\nname=GUI smoke\nbase_path=исходники с пробелом\n",-1,NULL));
 second_project=g_build_filename(profile,"второй проект.geany",NULL); g_assert_true(g_file_set_contents(second_project,"[project]\nname=GUI second\nbase_path=исходники с пробелом\n",-1,NULL));
 bad_project=g_build_filename(profile,"invalid.geany",NULL); g_assert_true(g_file_set_contents(bad_project,"[other]\nname=Not a project\n",-1,NULL));
 gchar *argv[]={"overview-gui-smoke","--new-instance","--no-session","--config",profile,test_project,NULL}; g_timeout_add(700,step,NULL); g_timeout_add_seconds(30,timeout_fail,NULL);
 g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 gint result=main_lib(6,argv); g_free(second_project); g_free(bad_project); g_free(test_project); g_free(test_directory); g_free(profile); return result;
}
