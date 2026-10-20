/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static const gchar *content="alpha αβ\r\nalphabet alpha\r\nALPHA αβ\r\nalpha ~a.b ~a.b\r\n";
static GeanyDocument *source,*other;
static GtkWidget *window;
static guint mode,tick;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *f=named(menu,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *dialog(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-selection-matches-dialog")) { found=p->data;break; }g_list_free(windows);return found; }
static void action(const gchar *suffix) { gchar *name=g_strconcat("insight-selection-search-",suffix,NULL);GtkWidget *item=named(window,name);g_free(name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void select_at(gint anchor,gint caret) { SEND(source->editor->sci,SCI_SETSEL,anchor,caret); }
static void position(gint anchor,gint caret,guint line) { ScintillaObject *sci=source->editor->sci;if(SEND(sci,SCI_GETANCHOR,0,0)!=anchor || sci_get_current_position(sci)!=caret) g_error("Selection search test line %u mode %u expected %d,%d got %d,%d",line,mode,anchor,caret,(gint)SEND(sci,SCI_GETANCHOR,0,0),sci_get_current_position(sci)); }
#define AT(a,c) position((a),(c),__LINE__)
static void text_is(const gchar *expected) { gchar *text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void reset(const gchar *text,gint anchor,gint caret) {
 ScintillaObject *sci=source->editor->sci;source->readonly=FALSE;SEND(sci,SCI_SETREADONLY,0,0);SEND(sci,SCI_CLEARSELECTIONS,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,0);SEND(sci,SCI_SETSELECTIONNANCHORVIRTUALSPACE,0,0);sci_set_text(sci,text);select_at(anchor,caret);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-file-search-case")),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-file-search-wrap")),TRUE);
}
static void rows(GtkTreeModel *model,gint count) { g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,count); }
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;GtkWidget *tree=named(d,"insight-selection-matches-list"),*entry=named(d,"insight-selection-matches-pattern");GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
 if(mode==0) {
  rows(model,5);if(!tick++) return G_SOURCE_CONTINUE;
  if(g_getenv("GEANY_SELECTION_SEARCH_CAPTURE")) { GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_SELECTION_SEARCH_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  GtkTreePath *path=gtk_tree_path_new_from_indices(1,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
 }
 if(mode==1) {
  if(!tick++) { gtk_dialog_response(GTK_DIALOG(d),202);return G_SOURCE_CONTINUE; }
  gchar *copy=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(strstr(copy,"Line\tColumn\tText"));g_assert_nonnull(strstr(copy,"alphabet alpha"));g_free(copy);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==2) {
  if(!tick++) { gtk_entry_set_text(GTK_ENTRY(entry),"αβ");rows(model,0);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT)));gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  rows(model,2);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
 }
 if(mode==3) {
  if(!tick++) { SEND(source->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"alpha\n");gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
  if(tick==2) { tick++;gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  rows(model,6);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
 }
 if(mode==4) {
  if(!tick++) { other=document_new_file("other.txt",NULL,"other");gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
  gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_REMOVE;
 }
 if(mode==5) { document_set_text_changed(source,FALSE);g_assert_true(document_close(source));gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_REMOVE; }
 if(mode==6) {
  if(!tick++) { gtk_entry_set_text(GTK_ENTRY(entry),"absent");gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  rows(model,0);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),202)));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==7) {
  if(!tick++) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(d,"insight-selection-matches-word")),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(d,"insight-selection-matches-case")),TRUE);gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  rows(model,3);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==8) { rows(model,5000);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(d,"insight-selection-matches-count"))),"limit"));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(mode==9) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(mode==10) {
  if(!tick++) { SEND(source->editor->sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);gtk_dialog_response(GTK_DIALOG(d),202);return G_SOURCE_CONTINUE; }
  gchar *copy=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(copy,==,"sentinel");g_free(copy);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==11) {
  if(!tick++) { gtk_entry_set_text(GTK_ENTRY(entry),"");gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  rows(model,0);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static void list(guint which) { mode=which;tick=0;g_timeout_add(120,respond,NULL);action("all"); }
static gboolean step(gpointer data) {
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));ScintillaObject *sci=source->editor->sci;gint second=strstr(content,"alpha\r")-content,last=strstr(content,"alpha ~")-content;
 reset(content,0,5);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-file-search-incremental")),TRUE);action("load");AT(0,5);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(window,"insight-file-search-query"))),==,"alpha");text_is(content);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-file-search-incremental")),FALSE);action("forward");AT(second,second+5);action("backward");AT(5,0);
 gtk_widget_grab_focus(GTK_WIDGET(sci));keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_INSIGHT_SELECTIONSEARCHFORWARD);AT(second,second+5);keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_GOTO_BACK);AT(5,0);
 reset(content,2,2);action("forward");AT(second,second+5);action("backward");AT(5,0);action("backward");AT(last+5,last);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-file-search-wrap")),FALSE);select_at(last,last+5);action("forward");AT(last,last+5);action("forward");AT(last,last+5);
 reset(content,0,5);action("load");gtk_widget_grab_focus(GTK_WIDGET(sci));keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_SEARCH_FINDNEXT);gint alphabet=strstr(content,"alphabet")-content;AT(alphabet,alphabet+5);
 reset("~a.b ~axb ~a.b",0,4);action("load");keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_SEARCH_FINDNEXT);AT(10,14);text_is("~a.b ~axb ~a.b");
 reset(content,5,0);action("load");AT(5,0);action("forward");AT(second,second+5);
 reset(content,0,0);action("load");AT(0,0);
 reset(content,6,6);action("forward");gint greek=strrchr(content,'\n')-content;(void)greek;gint greek_second=strstr(content,"ALPHA αβ")-content+6;AT(greek_second,greek_second+4);
 reset(content,0,5);list(0);AT(alphabet,alphabet+5);reset(content,0,5);list(1);AT(0,5);
 list(2);AT(6,10);reset(content,0,5);list(3);AT(0,5);gchar *prefixed=g_strconcat("alpha\n",content,NULL);text_is(prefixed);g_free(prefixed);
 reset(content,0,5);list(4);g_assert_true(document_get_current()==other);g_assert_true(document_close(other));text_is(content);
 reset(content,0,5);list(6);AT(0,5);reset(content,0,5);list(7);AT(0,5);
 reset(content,0,5);SEND(sci,SCI_SETREADONLY,1,0);list(9);action("forward");AT(second,second+5);text_is(content);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(content,0,5);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);list(10);
 reset(content,0,5);list(11);
 reset(content,0,5);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);action("load");action("all");g_assert_null(dialog());
 reset(content,0,5);SEND(sci,SCI_ADDSELECTION,10,10);SEND(sci,SCI_SETMAINSELECTION,0,0);action("forward");AT(0,5);
 reset(content,0,5);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,1);action("all");g_assert_null(dialog());
 reset("(α)",2,2);action("all");g_assert_null(dialog());AT(2,2);
 reset("a\r\nb",2,2);action("load");AT(2,2);
 reset("",0,0);SEND(sci,SCI_ADDTEXT,3,(sptr_t)"a\0b");select_at(0,1);action("all");g_assert_null(dialog());
 gchar *large=g_strnfill(4097,'x');reset(large,0,4097);action("load");action("all");g_assert_null(dialog());g_free(large);
 reset("a\nb",0,3);action("load");action("all");g_assert_null(dialog());
 GString *many=g_string_new(NULL);for(guint i=0;i<5010;i++) g_string_append(many,"x\n");reset(many->str,0,1);list(8);g_string_free(many,TRUE);
 reset(content,0,5);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);list(9);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);
 GtkWidget *entry=gtk_entry_new();gtk_box_pack_start(GTK_BOX(ui_lookup_widget(window,"vbox1")),entry,FALSE,FALSE,0);gtk_widget_show(entry);gtk_widget_grab_focus(entry);keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_INSIGHT_SELECTIONSEARCHFORWARD);AT(0,5);gtk_widget_destroy(entry);
 reset(content,0,5);list(5);
 g_print("Selection Search GUI: literal load/F3/tilde, Unicode whole words, direction/wrap/case, native keys/history, match list/copy/refresh/dirty/stale/switched/closed guards, read-only/unsupported/invalid/limits, focus and RU/EN passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Selection Search GUI timeout mode %u tick %u",mode,tick);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("selection-search-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL),*path=g_build_filename(dir,"source.txt",NULL);g_assert_true(g_file_set_contents(path,content,-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *args[]={"selection-search","--new-instance","--no-session","--config",dir,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(40,timeout,NULL);return main_lib(6,args);
}
