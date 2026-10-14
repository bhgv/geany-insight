/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include "keybindings.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*tree,*entry,*follow,*count;
static gchar *profile,*source,*other,*cyrillic,*outside,*settings,*root;
static guint phase,doc_id; static gint caret,preview_line;
static GtkWidget *named(GtkWidget *widget,const char *name) {
 if(!strcmp(gtk_widget_get_name(widget),name)) return widget;
 if(GTK_IS_MENU_ITEM(widget)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget)); if(sub) { GtkWidget *found=named(sub,name); if(found) return found; } }
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget)); GtkWidget *found=NULL; for(GList *p=children;p && !found;p=p->next) found=named(p->data,name); g_list_free(children); return found; } return NULL;
}
typedef struct { const char *path; GtkTreePath *row; guint files; } Find;
static gboolean find_row(GtkTreeModel *model,GtkTreePath *row,GtkTreeIter *iter,gpointer data) {
 Find *find=data; gchar *path=NULL; gboolean directory=FALSE; gtk_tree_model_get(model,iter,1,&path,2,&directory,-1); if(!directory) find->files++;
 if(find->path && !g_strcmp0(path,find->path)) find->row=gtk_tree_path_copy(row);
 g_free(path); return find->row!=NULL;
}
static GtkTreePath *path_row(const char *path) { Find find={.path=path}; gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),find_row,&find); return find.row; }
static guint files(void) { Find find={0}; gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),find_row,&find); return find.files; }
static gchar *selected(void) { GtkTreeModel *model; GtkTreeIter iter; gchar *path=NULL; if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),&model,&iter)) gtk_tree_model_get(model,&iter,1,&path,-1); return path; }
static void select_path(const char *path) { GtkTreePath *row=path_row(path); g_assert_nonnull(row); gtk_tree_view_expand_to_path(GTK_TREE_VIEW(tree),row); gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),row,NULL,FALSE); gtk_tree_path_free(row); }
static void unchanged(void) { GeanyDocument *doc=document_get_current(); g_assert_cmpuint(doc->id,==,doc_id); g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret); g_assert_cmpint(sci_get_current_line(SCINTILLA(named(window,"insight-context-code"))),==,preview_line); }
static void key(guint value,guint mods) { GdkEvent *e=gdk_event_new(GDK_KEY_PRESS); e->key.window=g_object_ref(gtk_widget_get_window(window)); e->key.keyval=value; e->key.state=mods; gdk_event_set_device(e,gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default()))); gtk_main_do_event(e); gdk_event_free(e); }
static GtkWidget *popup(void) { gtk_widget_grab_focus(tree); key(GDK_KEY_Menu,0); GtkWidget *menu=g_object_get_data(G_OBJECT(tree),"insight-folder-popup"); g_assert_nonnull(menu); g_object_ref(menu); gtk_menu_popdown(GTK_MENU(menu)); return menu; }
static GtkWidget *command(GtkWidget *menu,guint action) { gchar *name=g_strdup_printf("insight-folder-action-%u",action); GtkWidget *item=named(menu,name); g_free(name); g_assert_nonnull(item); return item; }
static void act(guint action) { GtkWidget *menu=popup(),*item=command(menu,action); g_assert_true(gtk_widget_get_sensitive(item)); gtk_menu_item_activate(GTK_MENU_ITEM(item)); gtk_widget_destroy(menu); g_object_unref(menu); }
static void clip(const char *expected) { gchar *text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)); g_assert_cmpstr(text,==,expected); g_free(text); }
static gboolean timeout(gpointer data) { (void)data; g_error("Project folders GUI timeout at phase %u",phase); return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data; static guint last_phase=G_MAXUINT; if(last_phase!=phase) { g_printerr("Folders GUI phase %u\n",phase); last_phase=phase; } GeanyDocument *doc=document_get_current(); if(!doc) return G_SOURCE_CONTINUE;
 if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(phase==0) { document_open_file(other,FALSE,NULL,NULL); document_open_file(source,FALSE,NULL,NULL); phase++; return G_SOURCE_CONTINUE;
  }
 if(!tree) { tree=named(window,"insight-project-folders"); entry=named(window,"insight-folder-query"); follow=named(window,"insight-folder-follow"); count=named(window,"insight-folder-count"); g_assert_nonnull(tree); gtk_window_resize(GTK_WINDOW(window),1200,850); gtk_paned_set_position(GTK_PANED(named(window,"insight-project-split")),650); GtkWidget *page=tree; while(gtk_widget_get_parent(page) && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) page=gtk_widget_get_parent(page); gtk_widget_show(page); gtk_widget_show(gtk_widget_get_parent(page)); gtk_notebook_set_current_page(GTK_NOTEBOOK(gtk_widget_get_parent(page)),gtk_notebook_page_num(GTK_NOTEBOOK(gtk_widget_get_parent(page)),page)); }
 if(phase==1) {
  GtkTreePath *row=path_row(cyrillic);
  if(!row) return G_SOURCE_CONTINUE;
  gtk_tree_path_free(row); if(files()!=3) return G_SOURCE_CONTINUE;
  g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(follow))); select_path(source); phase++;
 } else if(phase==2) {
  doc_id=doc->id; caret=sci_get_current_position(doc->editor->sci); preview_line=sci_get_current_line(SCINTILLA(named(window,"insight-context-code"))); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(follow),FALSE);
  act(1); clip(source); act(2); clip("src/a source.cpp"); unchanged();
  gchar *folder=g_path_get_dirname(source); select_path(folder); act(1); clip(folder); act(2); clip("src"); act(5); GtkTreePath *row=path_row(folder); g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tree),row)); act(4); g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tree),row)); gtk_tree_path_free(row); g_free(folder); unchanged();
  act(7); gtk_button_clicked(GTK_BUTTON(named(window,"insight-folder-locate"))); gchar *path=selected(); g_assert_cmpstr(path,==,source); g_free(path); unchanged();
  /* Right click selects a different source without preview/navigation. */
  row=path_row(other); gtk_tree_view_expand_to_path(GTK_TREE_VIEW(tree),row); gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree),row,NULL,FALSE,0,0); gtk_tree_path_free(row); phase++;
 } else if(phase==3) {
  GtkTreePath *row=path_row(other); GdkRectangle rect; gtk_tree_view_expand_to_path(GTK_TREE_VIEW(tree),row); gtk_tree_view_get_cell_area(GTK_TREE_VIEW(tree),row,NULL,&rect);
  GtkTreePath *hit=NULL;
  if(!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree),rect.x+4,rect.y+rect.height/2,&hit,NULL,NULL,NULL) || gtk_tree_path_compare(hit,row)) { if(hit) gtk_tree_path_free(hit); gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree),row,NULL,TRUE,0.5,0); gtk_tree_path_free(row); return G_SOURCE_CONTINUE; }
  gtk_tree_path_free(hit); gtk_tree_path_free(row);
  GdkEvent *e=gdk_event_new(GDK_BUTTON_PRESS); e->button.window=g_object_ref(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(tree))); e->button.button=3; e->button.x=rect.x+4; e->button.y=rect.y+rect.height/2; gdk_event_set_device(e,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()))); gboolean handled=FALSE; g_signal_emit_by_name(tree,"button-press-event",e,&handled); g_assert_true(handled); gdk_event_free(e);
  GtkWidget *menu=g_object_get_data(G_OBJECT(tree),"insight-folder-popup"); g_object_ref(menu); gtk_menu_popdown(GTK_MENU(menu)); gtk_widget_destroy(menu); g_object_unref(menu);
  gchar *path=selected(); g_assert_cmpstr(path,==,other); g_free(path); unchanged(); gtk_entry_set_text(GTK_ENTRY(entry),"~ткст"); phase++;
 } else if(phase==4) {
  if(files()!=1) return G_SOURCE_CONTINUE;
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(count)),==,"Folder files: 1 / 3"); GtkTreePath *row=path_row(cyrillic); g_assert_nonnull(row); gtk_tree_path_free(row); unchanged();
  gtk_button_clicked(GTK_BUTTON(named(window,"insight-folder-locate"))); g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)),==,"~ткст"); unchanged(); gtk_entry_set_text(GTK_ENTRY(entry),"?["); phase++;
 } else if(phase==5) {
  if(!gtk_style_context_has_class(gtk_widget_get_style_context(entry),"error")) return G_SOURCE_CONTINUE;
  g_assert_cmpuint(files(),==,1); unchanged();
  gtk_widget_grab_focus(entry); key(GDK_KEY_Escape,0); g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)),==,""); phase++;
 } else if(phase==6) {
  if(files()!=3) return G_SOURCE_CONTINUE;
  gtk_widget_grab_focus(tree); key(GDK_KEY_f,GEANY_PRIMARY_MOD_MASK); g_assert_true(gtk_widget_has_focus(entry)); gtk_entry_set_text(GTK_ENTRY(entry),"*b.cpp"); phase++;
 } else if(phase==7) {
  if(files()!=1) return G_SOURCE_CONTINUE;
  gtk_widget_grab_focus(entry); key(GDK_KEY_Down,0); g_assert_true(gtk_widget_has_focus(tree)); key(GDK_KEY_c,GEANY_PRIMARY_MOD_MASK|GDK_SHIFT_MASK); clip("tests/b.cpp");
  gtk_widget_grab_focus(tree); key(GDK_KEY_Return,GEANY_PRIMARY_MOD_MASK); phase++;
 } else if(phase==8) {
  if(g_strcmp0(doc->file_name,other)) return G_SOURCE_CONTINUE;
  gtk_entry_set_text(GTK_ENTRY(entry),""); phase++;
 } else if(phase==9) {
  if(files()!=3) return G_SOURCE_CONTINUE;
  gchar *path=selected(); g_assert_cmpstr(path,==,other); g_free(path); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(follow),TRUE); document_open_file(source,FALSE,NULL,NULL); phase++;
 } else if(phase==10) {
  gchar *path=selected(); gboolean correct=!g_strcmp0(path,source); g_free(path); if(!correct) return G_SOURCE_CONTINUE;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(follow),FALSE); document_open_file(outside,FALSE,NULL,NULL); phase++;
 } else if(phase==11) {
  if(g_strcmp0(doc->file_name,outside)) return G_SOURCE_CONTINUE;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(follow),TRUE); gchar *path=selected(); g_assert_cmpstr(path,==,source); g_free(path);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE); phase++;
 } else if(phase==12) {
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(count)),==,"Файлы дерева: 3 / 3"); GtkWidget *menu=popup(); g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(command(menu,2))),==,"Копировать путь относительно проекта"); gtk_widget_destroy(menu); g_object_unref(menu); act(0); phase++;
 } else if(phase==13) {
  if(g_strcmp0(doc->file_name,source)) return G_SOURCE_CONTINUE;
  gtk_widget_grab_focus(entry); gtk_entry_set_text(GTK_ENTRY(entry),"*текст.c"); key(GDK_KEY_Return,0); phase++;
 } else if(phase==14) {
  if(g_strcmp0(doc->file_name,cyrillic)) return G_SOURCE_CONTINUE;
  doc_id=doc->id; caret=sci_get_current_position(doc->editor->sci); preview_line=sci_get_current_line(SCINTILLA(named(window,"insight-context-code"))); gtk_entry_set_text(GTK_ENTRY(entry),"no-such-project-file-zz"); phase++;
 } else if(phase==15) {
  if(files()!=0) return G_SOURCE_CONTINUE;
  unchanged(); GtkWidget *menu=popup(); g_assert_false(gtk_widget_get_sensitive(command(menu,0))); g_assert_false(gtk_widget_get_sensitive(command(menu,1))); gtk_widget_destroy(menu); g_object_unref(menu); gtk_widget_grab_focus(entry); key(GDK_KEY_Escape,0); phase++;
 } else {
  if(files()!=3) return G_SOURCE_CONTINUE;
  g_print("Project Folders GUI passed: manifest/ancestors, fuzzy/glob/invalid filters, counts, menu/right click, clipboard, keyboard open, branch controls, locate/follow, out-of-project file, RU and saved Follow\n"); gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1"))); return G_SOURCE_REMOVE;
 } return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("folders-gui-%u",g_random_int()),*base=g_build_filename(cwd,name,NULL); g_free(cwd); g_free(name); root=g_build_filename(base,"project",NULL); profile=g_build_filename(base,"profile",NULL); source=g_build_filename(root,"src","a source.cpp",NULL); other=g_build_filename(root,"tests","b.cpp",NULL); cyrillic=g_build_filename(root,"src","текст.c",NULL); outside=g_build_filename(base,"outside.cpp",NULL);
 gchar *dir=g_path_get_dirname(source); g_assert_cmpint(g_mkdir_with_parents(dir,0700),==,0); g_free(dir); dir=g_path_get_dirname(other); g_assert_cmpint(g_mkdir_with_parents(dir,0700),==,0); g_free(dir); dir=g_build_filename(profile,"insight",NULL); g_assert_cmpint(g_mkdir_with_parents(dir,0700),==,0); settings=g_build_filename(dir,"workbench.conf",NULL); g_free(dir);
 const char *paths[]={source,other,cyrillic,outside}; for(guint i=0;i<G_N_ELEMENTS(paths);i++) g_assert_true(g_file_set_contents(paths[i],"int sample() { return 1; }\n",-1,NULL));
 gchar *encoded=g_base64_encode((const guchar *)root,strlen(root)),*prefs=g_strdup_printf("[workbench]\nversion=1\nfollow=false\nproject-visible=true\nproject-tab=2\nfolder-follow-current-file=true\nroot-base64=%s\n",encoded); g_assert_true(g_file_set_contents(settings,prefs,-1,NULL)); g_free(encoded); g_free(prefs);
 gchar *config=g_build_filename(profile,"geany.conf",NULL); g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL)); g_free(config);
 gchar *argv[]={"folders-gui","--new-instance","--no-session","--config",profile,NULL}; g_timeout_add(250,step,NULL); g_timeout_add_seconds(50,timeout,NULL); g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL); gint result=main_lib(5,argv);
 GKeyFile *keyfile=g_key_file_new(); g_assert_true(g_key_file_load_from_file(keyfile,settings,G_KEY_FILE_NONE,NULL)); g_assert_true(g_key_file_get_boolean(keyfile,"workbench","folder-follow-current-file",NULL)); g_key_file_unref(keyfile);
 g_free(base); g_free(root); g_free(profile); g_free(source); g_free(other); g_free(cyrillic); g_free(outside); g_free(settings); return result;
}
