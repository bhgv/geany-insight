/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define VIEW_SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GeanyDocument *doc_a,*doc_b,*doc_c,*doc_d,*untitled;
static GtkWidget *window,*dialog_widget;
static gchar *path_a,*path_b,*path_c,*path_d,*contents;
static guint phase;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p&&!found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *find_dialog(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-scroll-links-dialog")) found=p->data;g_list_free(windows);return found; }
static void activate(GeanyDocument *doc) { GtkWidget *child=GTK_WIDGET(doc->editor->sci);while(gtk_widget_get_parent(child)) { GtkWidget *parent=gtk_widget_get_parent(child);if(GTK_IS_NOTEBOOK(parent)) gtk_notebook_set_current_page(GTK_NOTEBOOK(parent),gtk_notebook_page_num(GTK_NOTEBOOK(parent),child));child=parent; }gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci)); }
static void command(guint id) { keybindings_send_command(GEANY_KEY_GROUP_EDITOR,id); }
static gint row(GeanyDocument *doc) { return (gint)VIEW_SEND(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0); }
static gint xpos(GeanyDocument *doc) { return (gint)VIEW_SEND(doc->editor->sci,SCI_GETXOFFSET,0,0); }
static void notify(GeanyDocument *doc) { SCNotification event={0};event.nmhdr.code=SCN_UPDATEUI;event.updated=SC_UPDATE_V_SCROLL|SC_UPDATE_H_SCROLL;g_signal_emit_by_name(doc->editor->sci,"sci-notify",0,&event); }
static void place(GeanyDocument *doc,gint line,gint x) { VIEW_SEND(doc->editor->sci,SCI_SETWRAPMODE,SC_WRAP_NONE,0);VIEW_SEND(doc->editor->sci,SCI_SETSCROLLWIDTH,12000,0);VIEW_SEND(doc->editor->sci,SCI_SETFIRSTVISIBLELINE,line,0);VIEW_SEND(doc->editor->sci,SCI_SETXOFFSET,x,0); }
static void scroll_to(GeanyDocument *doc,gint line,gint x) { place(doc,line,x);notify(doc); }
static void axes(gboolean v,gboolean h) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog_widget,"insight-scroll-links-vertical")),v);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog_widget,"insight-scroll-links-horizontal")),h); }
static void positions(gint a,gint b,gint c,gint ax,gint bx,gint cx,gboolean v,gboolean h) { axes(FALSE,FALSE);place(doc_a,a,ax);place(doc_b,b,bx);place(doc_c,c,cx);axes(v,h); }
static void show_for(GeanyDocument *doc) { activate(doc);command(GEANY_KEYS_INSIGHT_LINKWINDOW);dialog_widget=find_dialog();g_assert_nonnull(dialog_widget); }
static void pair(GeanyDocument *source,GeanyDocument *target,gboolean reverse) { show_for(source);gchar *id=g_strdup_printf("%u",target ? target->id:0);g_assert_true(gtk_combo_box_set_active_id(GTK_COMBO_BOX(named(dialog_widget,"insight-scroll-links-target")),id));g_free(id);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog_widget,"insight-scroll-links-two-way")),reverse);gtk_dialog_response(GTK_DIALOG(dialog_widget),GTK_RESPONSE_APPLY); }
static guint pair_count(void) { return gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(named(dialog_widget,"insight-scroll-links-list"))),NULL); }
static void intact(GeanyDocument *doc) { gchar *actual=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(actual,==,contents);g_free(actual);g_assert_false(doc->changed);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,0);g_assert_false(VIEW_SEND(doc->editor->sci,SCI_CANUNDO,0,0)); }
static gboolean tick(gpointer data) {
 (void)data;
 if(phase==0) {
  doc_a=document_get_current();if(!doc_a || g_strcmp0(doc_a->file_name,path_a)) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(doc_a->editor->sci));
  doc_b=document_open_file(path_b,FALSE,NULL,NULL);doc_c=document_open_file(path_c,FALSE,NULL,NULL);g_assert_nonnull(doc_b);g_assert_nonnull(doc_c);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"scroll-links-clipboard",-1);
  pair(doc_a,doc_b,FALSE);g_assert_cmpuint(pair_count(),==,1);positions(20,70,120,100,200,300,TRUE,FALSE);scroll_to(doc_a,25,100);g_assert_cmpint(row(doc_b),==,75);g_assert_cmpint(row(doc_c),==,120);scroll_to(doc_b,80,200);g_assert_cmpint(row(doc_a),==,25);
  /* Choose both endpoints inside the dialog while keeping the active editor. */
  GeanyDocument *active=document_get_current();gchar *source_id=g_strdup_printf("%u",doc_b->id),*target_id=g_strdup_printf("%u",doc_c->id);g_assert_true(gtk_combo_box_set_active_id(GTK_COMBO_BOX(named(dialog_widget,"insight-scroll-links-source-choice")),source_id));g_assert_true(gtk_combo_box_set_active_id(GTK_COMBO_BOX(named(dialog_widget,"insight-scroll-links-target")),target_id));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog_widget,"insight-scroll-links-two-way")),FALSE);gtk_dialog_response(GTK_DIALOG(dialog_widget),GTK_RESPONSE_APPLY);g_assert_cmpuint(pair_count(),==,2);g_assert_true(document_get_current()==active);
  GtkWidget *list=named(dialog_widget,"insight-scroll-links-list");GtkTreePath *selected=gtk_tree_path_new_from_indices(1,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(list),selected,NULL,FALSE);gtk_tree_path_free(selected);g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(named(dialog_widget,"insight-scroll-links-source-choice"))),==,source_id);g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(named(dialog_widget,"insight-scroll-links-target"))),==,target_id);GtkWidget *remove=named(dialog_widget,"insight-scroll-links-remove-selected");g_assert_true(gtk_widget_get_sensitive(remove));gtk_button_clicked(GTK_BUTTON(remove));g_assert_cmpuint(pair_count(),==,1);g_assert_false(gtk_widget_get_sensitive(remove));g_assert_true(document_get_current()==active);g_free(source_id);g_free(target_id);
  pair(doc_a,doc_b,TRUE);positions(20,70,120,100,200,300,TRUE,FALSE);scroll_to(doc_b,80,200);g_assert_cmpint(row(doc_a),==,30);g_assert_cmpint(row(doc_c),==,120);
  pair(doc_b,doc_c,FALSE);positions(20,70,120,100,200,300,TRUE,FALSE);scroll_to(doc_a,25,100);g_assert_cmpint(row(doc_b),==,75);g_assert_cmpint(row(doc_c),==,125);scroll_to(doc_c,130,300);g_assert_cmpint(row(doc_a),==,25);g_assert_cmpint(row(doc_b),==,75);
  pair(doc_c,doc_a,FALSE);g_assert_cmpuint(pair_count(),==,3);positions(20,70,120,100,200,300,TRUE,FALSE);scroll_to(doc_a,27,100);g_assert_cmpint(row(doc_a),==,27);g_assert_cmpint(row(doc_b),==,77);g_assert_cmpint(row(doc_c),==,127);intact(doc_a);intact(doc_b);intact(doc_c);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==1) {
  /* Let native document activation finish before measuring delayed scroll
   * notifications: opening/switching tabs may restore their own viewport. */
  positions(20,70,120,100,200,300,TRUE,FALSE);scroll_to(doc_a,27,100);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {
  g_assert_cmpint(row(doc_a),==,27);g_assert_cmpint(row(doc_b),==,77);g_assert_cmpint(row(doc_c),==,127);
  positions(20,70,120,100,200,300,FALSE,TRUE);scroll_to(doc_a,30,125);g_assert_cmpint(row(doc_b),==,70);g_assert_cmpint(row(doc_c),==,120);g_assert_cmpint(xpos(doc_b),==,225);g_assert_cmpint(xpos(doc_c),==,325);scroll_to(doc_b,70,240);g_assert_cmpint(xpos(doc_a),==,140);g_assert_cmpint(xpos(doc_c),==,340);
  command(GEANY_KEYS_INSIGHT_BROWSERMODE);g_assert_true(doc_c->readonly);scroll_to(doc_c,130,350);g_assert_cmpint(xpos(doc_a),==,150);g_assert_cmpint(xpos(doc_b),==,250);command(GEANY_KEYS_INSIGHT_BROWSERMODE);g_assert_false(doc_c->readonly);
  command(GEANY_KEYS_INSIGHT_UNLINKWINDOWS);g_assert_cmpuint(pair_count(),==,0);positions(10,50,90,10,20,30,TRUE,FALSE);command(GEANY_KEYS_INSIGHT_LINKALLWINDOWS);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(dialog_widget,"insight-scroll-links-all"))));scroll_to(doc_b,55,20);g_assert_cmpint(row(doc_a),==,15);g_assert_cmpint(row(doc_c),==,95);
  doc_d=document_open_file(path_d,FALSE,NULL,NULL);g_assert_nonnull(doc_d);notify(doc_d);g_assert_cmpint(row(doc_a),==,15);g_assert_cmpint(row(doc_b),==,55);g_assert_cmpint(row(doc_c),==,95);scroll_to(doc_d,3,0);g_assert_cmpint(row(doc_a),==,18);g_assert_cmpint(row(doc_b),==,58);g_assert_cmpint(row(doc_c),==,98);intact(doc_d);
  command(GEANY_KEYS_INSIGHT_UNLINKWINDOWS);g_assert_true(document_close(doc_d));activate(doc_a);
  GtkWidget *window_menu=named(window,"insight-window-menu"),*all_before=named(window_menu,"insight-scroll-links-all-command");g_assert_nonnull(all_before);g_signal_emit_by_name(window_menu,"show");g_signal_emit_by_name(window_menu,"show");g_assert_true(named(window_menu,"insight-scroll-links-all-command")==all_before);g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(all_before)));
  pair(doc_a,doc_b,FALSE);g_assert_cmpuint(pair_count(),==,1);intact(doc_a);intact(doc_b);g_assert_true(document_close(doc_b));g_assert_cmpuint(pair_count(),==,0);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog_widget),GTK_RESPONSE_APPLY)));gtk_dialog_response(GTK_DIALOG(dialog_widget),GTK_RESPONSE_APPLY);g_assert_cmpuint(pair_count(),==,0);
  pair(doc_a,doc_c,FALSE);g_assert_cmpuint(pair_count(),==,1);g_assert_true(document_close(doc_a));g_assert_cmpuint(pair_count(),==,0);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog_widget),GTK_RESPONSE_APPLY)));gtk_dialog_response(GTK_DIALOG(dialog_widget),GTK_RESPONSE_APPLY);g_assert_cmpuint(pair_count(),==,0);gtk_dialog_response(GTK_DIALOG(dialog_widget),GTK_RESPONSE_HELP);g_assert_true(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog_widget),GTK_RESPONSE_APPLY)));
  untitled=document_new_file(NULL,filetypes_lookup_by_name("None"),contents);g_assert_nonnull(untitled);pair(doc_c,untitled,TRUE);axes(FALSE,FALSE);place(doc_c,40,0);place(untitled,80,0);axes(TRUE,FALSE);gtk_dialog_response(GTK_DIALOG(dialog_widget),GTK_RESPONSE_CLOSE);dialog_widget=NULL;scroll_to(doc_c,45,0);g_assert_cmpint(row(untitled),==,85);show_for(doc_c);g_assert_cmpuint(pair_count(),==,1);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(dialog_widget,"insight-scroll-links-two-way"))));
  /* Language refresh preserves endpoints and an unapplied direction change. */
  gchar *source_id=g_strdup_printf("%u",doc_c->id),*target_id=g_strdup_printf("%u",untitled->id);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog_widget,"insight-scroll-links-two-way")),FALSE);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(named(dialog_widget,"insight-scroll-links-source-choice"))),==,source_id);g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(named(dialog_widget,"insight-scroll-links-target"))),==,target_id);g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(dialog_widget,"insight-scroll-links-two-way"))));g_assert_cmpuint(pair_count(),==,1);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);g_free(source_id);g_free(target_id);
  command(GEANY_KEYS_INSIGHT_UNLINKWINDOWS);scroll_to(doc_c,50,0);g_assert_cmpint(row(untitled),==,85);g_assert_cmpuint(pair_count(),==,0);document_set_text_changed(untitled,FALSE);g_assert_true(document_close(untitled));gtk_dialog_response(GTK_DIALOG(dialog_widget),GTK_RESPONSE_CLOSE);intact(doc_c);
  gchar *clipboard=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clipboard,==,"scroll-links-clipboard");g_free(clipboard);
  g_print("Source scroll links: source/target picker, row editing and selective unlink, language/pending-state retention, one/two-way, chains/cycles, independent axes, async stability, Link All/new buffers, Browser Mode, persistent Window commands, stale endpoints, untitled and closed-dialog links; text/caret/Undo/clipboard preserved\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Source scroll links timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("source-scroll-links-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL);g_assert_cmpint(g_mkdir(profile,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));GString *body=g_string_new(NULL);for(guint n=0;n<500;n++) { g_string_append_printf(body,"row %03u ",n);for(guint k=0;k<300;k++) g_string_append_c(body,'x');g_string_append_c(body,'\n'); }contents=g_string_free(body,FALSE);
 path_a=g_build_filename(dir,"a.txt",NULL);path_b=g_build_filename(dir,"b.txt",NULL);path_c=g_build_filename(dir,"c.txt",NULL);path_d=g_build_filename(dir,"d.txt",NULL);const gchar *paths[]={path_a,path_b,path_c,path_d};for(guint n=0;n<4;n++) g_assert_true(g_file_set_contents(paths[n],contents,-1,NULL));
 gchar *args[]={"source-scroll-links","--new-instance","--no-session","--config",profile,path_a,NULL};g_timeout_add(250,tick,NULL);g_timeout_add_seconds(40,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,args);
 for(guint n=0;n<4;n++) { gchar *disk=NULL;g_assert_true(g_file_get_contents(paths[n],&disk,NULL,NULL));g_assert_cmpstr(disk,==,contents);g_free(disk); }return result;
}
