/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static ScintillaObject *sci;static guint phase;static const gchar *code;static gint radix,guard;static gboolean valid;static gchar *ready,*executable;static GSubprocess *owner;
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL; }
static GtkWidget *top(const gchar *name) { GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),name)) found=p->data;g_list_free(windows);return found; }
static void command(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void check(const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void reset(void) { SEND(sci,SCI_SETREADONLY,0,0);sci_set_text(sci,"aαb");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSEL,3,1); }
static gboolean character_capture(gpointer data) {
 GtkWidget *dialog=data;GdkWindow *w=gtk_widget_get_window(dialog);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_CHARACTER_CAPTURE"),"jpeg",NULL,"quality","90",NULL));g_object_unref(image);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static gboolean character_response(gpointer data) {
 (void)data;GtkWidget *dialog=top("insight-insert-character-dialog");g_assert_nonnull(dialog);GtkWidget *entry=named(dialog,"insight-character-code"),*combo=named(dialog,"insight-character-radix");
 gtk_combo_box_set_active(GTK_COMBO_BOX(combo),radix);gtk_entry_set_text(GTK_ENTRY(entry),code);
 g_assert_cmpint(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT)),==,valid);
 if(guard==1) SEND(sci,SCI_INSERTTEXT,0,(sptr_t)"stale");
 if(guard==2) SEND(sci,SCI_SETSEL,0,0);
 if(guard==3) SEND(sci,SCI_SETREADONLY,1,0);
 if(guard==4) { GeanyDocument *doc=document_get_current();SEND(sci,SCI_SETSAVEPOINT,0,0);g_assert_true(document_close(doc)); }
 if(g_getenv("GEANY_CHARACTER_CAPTURE") && !g_strcmp0(code,"U+03B1")) { g_timeout_add(120,character_capture,dialog);return G_SOURCE_REMOVE; }
 gtk_dialog_response(GTK_DIALOG(dialog),valid ? GTK_RESPONSE_ACCEPT : GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
static void insert(const gchar *value,gint base,gboolean acceptable,gint mode) { code=value;radix=base;valid=acceptable;guard=mode;g_idle_add(character_response,NULL);command("insight-insert-character"); }
static void modal_checks(void) {
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"unchanged clipboard",-1);
 reset();command("insight-insert-guid");gchar *text=sci_get_contents(sci,-1);g_assert_cmpuint(strlen(text),==,38);gchar *guid=g_strndup(text+1,36);g_assert_true(g_uuid_string_is_valid(guid));g_assert_cmpint(guid[14],==,'4');g_assert_nonnull(strchr("89ab",guid[19]));g_assert_cmpint(text[37],==,'b');g_free(text);SEND(sci,SCI_UNDO,0,0);check("aαb");SEND(sci,SCI_REDO,0,0);text=sci_get_contents(sci,-1);g_assert_cmpstr(text+1+36,==,"b");g_free(text);reset();command("insight-insert-guid");text=sci_get_contents(sci,-1);g_assert_cmpint(strncmp(text+1,guid,36),!=,0);g_free(text);g_free(guid);
 const gchar *codes[]={"65","0x41","U+0041","0o101","0b1000001","41","101","1000001","U+03B1","0x1F642","9","10","13"};
 const gint bases[]={0,0,0,0,0,2,3,4,0,0,1,1,1};
 const gchar *results[]={"aAb","aAb","aAb","aAb","aAb","aAb","aAb","aAb","aαb","a🙂b","a\tb","a\nb","a\rb"};
 for(guint i=0;i<G_N_ELEMENTS(codes);i++) { reset();insert(codes[i],bases[i],TRUE,0);check(results[i]);SEND(sci,SCI_UNDO,0,0);check("aαb");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);check(results[i]); }
 const gchar *invalid[]={"","0","-1","+65","0x","U+D800","0x110000","9999999999999999999999999","0b102","65x","1 2"};
 for(guint i=0;i<G_N_ELEMENTS(invalid);i++) { reset();insert(invalid[i],0,FALSE,0);check("aαb");g_assert_false(SEND(sci,SCI_CANUNDO,0,0)); }
 reset();insert("65",0,TRUE,1);check("staleaαb");SEND(sci,SCI_UNDO,0,0);check("aαb");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset();insert("65",0,TRUE,2);check("aαb");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset();insert("65",0,TRUE,3);check("aαb");SEND(sci,SCI_SETREADONLY,0,0);
 reset();SEND(sci,SCI_SETREADONLY,1,0);command("insight-insert-guid");command("insight-insert-character");g_assert_null(top("insight-insert-character-dialog"));check("aαb");SEND(sci,SCI_SETREADONLY,0,0);
 reset();SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);command("insight-insert-guid");check("aαb");SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);
 gchar *clipboard=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clipboard,==,"unchanged clipboard");g_free(clipboard);
}
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current() && phase<6) return G_SOURCE_CONTINUE;
 if(!phase) {
  sci=document_get_current()->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));modal_checks();
  GError *error=NULL;owner=g_subprocess_new(G_SUBPROCESS_FLAGS_NONE,&error,executable,"--clipboard-owner",ready,NULL);g_assert_no_error(error);g_assert_nonnull(owner);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==1) { if(!g_file_test(ready,G_FILE_TEST_EXISTS)) return G_SOURCE_CONTINUE;
  /* The provider is another process: close while X selection reads are pending. */
  command("insight-show-clipboard");GtkWidget *view=top("insight-clipboard-window");g_assert_nonnull(view);gtk_widget_destroy(view);command("insight-show-clipboard");phase++;return G_SOURCE_CONTINUE;
 }
 GtkWidget *view=top("insight-clipboard-window");g_assert_nonnull(view);GtkWidget *label=named(view,"insight-clipboard-text"),*status=named(view,"insight-clipboard-status");const gchar *message=gtk_label_get_text(GTK_LABEL(status));
 if(!g_strcmp0(message,"Reading clipboard…")) return G_SOURCE_CONTINUE;
 if(phase==2) {
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)),==,"external clipboard\nα β\t  spaces\n");g_assert_false(gtk_label_get_selectable(GTK_LABEL(label)));
  command("insight-show-clipboard");g_assert_true(view==top("insight-clipboard-window"));gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"Options\nα β\t  spaces\n",-1);gtk_button_clicked(GTK_BUTTON(named(view,"insight-clipboard-refresh")));phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==3) {
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)),==,"Options\nα β\t  spaces\n");
  GtkWidget *ru=named(window,"insight-interface-ru"),*en=named(window,"insight-interface-en");g_assert_nonnull(ru);g_assert_nonnull(en);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ru),TRUE);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)),==,"Options\nα β\t  spaces\n");g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(named(window,"insight-show-clipboard"))),==,"Показать буфер обмена");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(en),TRUE);
  if(g_getenv("GEANY_CLIPBOARD_CAPTURE")) { GdkWindow *w=gtk_widget_get_window(view);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_CLIPBOARD_CAPTURE"),"jpeg",NULL,"quality","90",NULL));g_object_unref(image); }
  gchar *big=g_malloc0(256*1024+2);memset(big,'x',256*1024+1);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),big,-1);g_free(big);gtk_button_clicked(GTK_BUTTON(named(view,"insight-clipboard-refresh")));phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==4) { g_assert_cmpstr(message,==,"Clipboard text exceeds the 256 KiB display limit.");g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)),==,"");gtk_clipboard_clear(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));gtk_button_clicked(GTK_BUTTON(named(view,"insight-clipboard-refresh")));phase++;return G_SOURCE_CONTINUE; }
 g_assert_cmpstr(message,==,"Clipboard has no text.");gtk_widget_destroy(view);g_subprocess_force_exit(owner);g_subprocess_wait(owner,NULL,NULL);g_object_unref(owner);
 reset();guint id=document_get_current()->id;insert("65",0,TRUE,4);g_assert_null(document_find_by_id(id));
 g_print("Insert tools: UUID v4, radix/Unicode validation, exact selection replacement, Undo/Redo, stale/read-only/closed targets, external clipboard async lifetime, refresh, no-text and size limit passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Insert tools timeout phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean owner_timeout(gpointer data) { (void)data;gtk_main_quit();return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 if(argc==3 && !strcmp(argv[1],"--clipboard-owner")) { gtk_init(&argc,&argv);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"external clipboard\nα β\t  spaces\n",-1);g_assert_true(g_file_set_contents(argv[2],"ready",-1,NULL));g_timeout_add_seconds(35,owner_timeout,NULL);gtk_main();return 0; }
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 executable=g_canonicalize_filename(argv[0],NULL);gchar *profile=g_dir_make_tmp("insert-tools-XXXXXX",NULL),*config=g_build_filename(profile,"geany.conf",NULL);ready=g_build_filename(profile,"clipboard-ready",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *args[]={"insert-tools","--new-instance","--no-session","--config",profile,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(500,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(5,args);
}
