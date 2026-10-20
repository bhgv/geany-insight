/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "filetypes.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static const gchar *content="void f() {\r\n const char *s = \"ignore { ) }\"; /* { ) } */\r\n outer(α, inner[x](value)); // { ) }\r\n}\r\n";
static GeanyDocument *source,*other;
static GtkWidget *window;
static guint mode,tick;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *dialog(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-delimiter-dialog")) { found=p->data;break; }g_list_free(windows);return found; }
static void action(const gchar *suffix) { gchar *name=g_strconcat("insight-delimiter-",suffix,NULL);GtkWidget *item=named(window,name);g_free(name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void text_is(const gchar *expected) { gchar *text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void reset(const gchar *text,gint anchor,gint caret) {
 ScintillaObject *sci=source->editor->sci;source->readonly=FALSE;SEND(sci,SCI_SETREADONLY,0,0);SEND(sci,SCI_CLEARSELECTIONS,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,0);SEND(sci,SCI_SETSELECTIONNANCHORVIRTUALSPACE,0,0);document_set_filetype(source,filetypes_lookup_by_name("C"));sci_set_text(sci,text);SEND(sci,SCI_SETSEL,anchor,caret);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);
}
static void position(gint expected) { g_assert_cmpint(sci_get_current_position(source->editor->sci),==,expected); }
static void row(GtkWidget *tree,gint index) { GtkTreePath *path=gtk_tree_path_new_from_indices(index,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path); }
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;GtkWidget *tree=named(d,"insight-delimiter-list");g_assert_nonnull(tree);GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
 if(mode==0) {
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,3);if(!tick++) return G_SOURCE_CONTINUE;
  if(g_getenv("GEANY_DELIMITER_CAPTURE")) { GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_DELIMITER_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
 }
 if(mode==1) { row(tree,1);gtk_dialog_response(GTK_DIALOG(d),203);return G_SOURCE_REMOVE; }
 if(mode==2) {
  if(!tick++) { SEND(source->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"prefix\n");gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
  if(tick==2) { tick++;gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
 }
 if(mode==3) {
  if(!tick++) { SEND(source->editor->sci,SCI_SETSEL,0,0);gtk_dialog_response(GTK_DIALOG(d),202);return G_SOURCE_CONTINUE; }
  if(tick==2) { tick++;gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==4) {
  if(!tick++) { other=document_new_file("other.c",NULL,"other");gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
  gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_REMOVE;
 }
 if(mode==5) { document_set_text_changed(source,FALSE);g_assert_true(document_close(source));gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_REMOVE; }
 if(mode==6) {
  if(!tick++) { document_set_filetype(source,filetypes_lookup_by_name("Python"));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==7) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(mode==8) { gtk_dialog_response(GTK_DIALOG(d),202);return G_SOURCE_REMOVE; }
 if(mode==9) {
  if(!tick++) { SEND(source->editor->sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==10) { g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT)));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(mode==12) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE; }
 if(mode==11) { g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,256);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(d,"insight-delimiter-count"))),"300"));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static void browser(guint which) { mode=which;tick=0;g_timeout_add(120,respond,NULL);action("browser"); }
static gboolean step(gpointer data) {
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));ScintillaObject *sci=source->editor->sci;
 gint value=strstr(content,"value")-content,inner=value-1,outer=strstr(content,"(α")-content,brace=strchr(content,'{')-content,closing=strrchr(content,'}')-content;
 reset(content,value,value);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);
 action("paren-left");position(inner);action("paren-left");position(outer);action("paren-left");position(outer);action("paren-right");position(value+6);action("paren-right");position(value+6);
 reset(content,value,value);action("paren-right");position(value+5);action("block-up");position(brace);action("block-down");position(closing);action("block-down");position(closing);action("block-up");position(brace);
 reset(content,value,value);gtk_widget_grab_focus(GTK_WIDGET(sci));keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_INSIGHT_PARENLEFT);position(inner);keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_GOTO_BACK);position(value);keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_GOTO_FORWARD);position(inner);
 reset(content,value,value);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-extend-mode")),TRUE);action("paren-left");position(inner);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,value);action("paren-right");position(value+5);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,value);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-extend-mode")),FALSE);
 reset(content,value,value);action("select-paren-left");position(inner);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,value);action("select-block-down");position(closing);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,value);
 reset(content,value,value);SEND(sci,SCI_SETREADONLY,1,0);action("paren-left");position(inner);text_is(content);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(content,value,value);browser(0);position(value+6);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,inner);text_is(content);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(content,value,value);browser(1);position(value+6);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,value+6);
 reset(content,value,value);browser(8);position(inner);
 reset(content,value,value);browser(2);position(value+13);gchar *prefixed=g_strconcat("prefix\n",content,NULL);text_is(prefixed);g_free(prefixed);
 reset(content,value,value);browser(3);position(0);text_is(content);
 reset(content,value,value);browser(4);g_assert_true(document_get_current()==other);g_assert_true(document_close(other));text_is(content);
 reset(content,value,value);browser(6);position(value);text_is(content);
 reset(content,value,value);browser(7);position(value);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(content,value,value);browser(9);position(value);
 reset(content,0,0);browser(10);position(0);
 reset(content,value+3,value);browser(12);position(inner);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,value+6);
 reset(content,value,value);SEND(sci,SCI_SETREADONLY,1,0);browser(12);position(value+6);text_is(content);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 const gchar *langs[]={"Python","Lua","Javascript"};const gchar *samples[]={"call(') {', inner(value)) # ) }\n","call(') {', inner(value)) -- ) }\n","call(') {', inner(value)) // ) }\n"};
 for(guint i=0;i<3;i++) { gint at=strstr(samples[i],"value")-samples[i];reset(samples[i],at,at);GeanyFiletype *type=filetypes_lookup_by_name(langs[i]);g_assert_nonnull(type);document_set_filetype(source,type);action("paren-left");position(at-1);action("paren-right");position(at+5);text_is(samples[i]); }
 reset(content,value,value);document_set_filetype(source,filetypes_lookup_by_name("None"));action("paren-left");position(value);action("browser");g_assert_null(dialog());
 reset(content,value,value);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);action("paren-left");position(value);action("browser");g_assert_null(dialog());
 reset(content,value,value);SEND(sci,SCI_ADDSELECTION,0,0);action("paren-left");position(value);action("browser");g_assert_null(dialog());
 reset(content,value,value);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,1);action("paren-left");position(value);
 reset("(α)",2,2);action("paren-left");position(2);action("browser");g_assert_null(dialog());
 reset("(a\r\nb)",3,3);action("paren-left");position(3);
 reset("",0,0);SEND(sci,SCI_ADDTEXT,5,(sptr_t)"(a\0b)");SEND(sci,SCI_SETSEL,0,0);action("paren-left");position(0);action("browser");g_assert_null(dialog());
 gchar *nested=g_strnfill(601,'(');nested[300]='x';memset(nested+301,')',300);reset(nested,300,300);browser(11);g_free(nested);
 reset(content,value,value);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);browser(7);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);
 GtkWidget *entry=gtk_entry_new();gtk_box_pack_start(GTK_BOX(ui_lookup_widget(window,"vbox1")),entry,FALSE,FALSE,0);gtk_widget_show(entry);gtk_widget_grab_focus(entry);keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_INSIGHT_PARENLEFT);position(value);gtk_widget_destroy(entry);
 reset(content,value,value);gchar *clipboard=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clipboard,==,"sentinel");g_free(clipboard);browser(5);
 g_print("Delimiter GUI: lexical C/Python/Lua/JS, strict pair/brace navigation, exact Back/Forward, native shortcuts/Extend/reverse selections, read-only, guarded list/refresh/closed/switched targets, unsupported/invalid inputs, row cap and RU/EN passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Delimiter GUI timeout mode %u tick %u",mode,tick);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("delimiter-navigation-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL),*path=g_build_filename(dir,"source.c",NULL);g_assert_true(g_file_set_contents(path,content,-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *args[]={"delimiter-navigation","--new-instance","--no-session","--config",dir,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(40,timeout,NULL);return main_lib(6,args);
}
