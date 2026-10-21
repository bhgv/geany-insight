/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static GeanyDocument *doc;static ScintillaObject *sci;static gchar *path;static gint guard;static guint timer;static gboolean image,hook_done;static const gchar *original="int target(void) {\r\n return 7;\r\n}\r\nint other;\r\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=list;p&&!f;p=p->next) f=named(p->data,name);g_list_free(list);return f; }return NULL;
}
static GtkWidget *dialog(void) { GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data)&&!g_strcmp0(gtk_widget_get_name(p->data),"insight-paste-symbol-dialog")) found=p->data;g_list_free(list);return found; }
static void check(const gchar *text) { gchar *actual=sci_get_contents(sci,-1);g_assert_cmpstr(actual,==,text);g_free(actual); }
static gboolean respond(gpointer unused) {
 (void)unused;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;
 ScintillaObject *before=SCINTILLA(named(d,"insight-paste-symbol-before")),*after=SCINTILLA(named(d,"insight-paste-symbol-after"));g_assert_true(SEND(before,SCI_GETREADONLY,0,0));g_assert_true(SEND(after,SCI_GETREADONLY,0,0));g_assert_cmpint(SEND(after,SCI_GETMARGINWIDTHN,1,0),>,0);
 if(image&&g_getenv("GEANY_PASTE_SYMBOL_CAPTURE")) { GdkPixbuf *pix=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(pix);g_assert_true(gdk_pixbuf_save(pix,g_getenv("GEANY_PASTE_SYMBOL_CAPTURE"),"jpeg",NULL,"quality","90",NULL));g_object_unref(pix); }
 if(guard==1) { SEND(sci,SCI_APPENDTEXT,1,(sptr_t)"x");SEND(sci,SCI_UNDO,0,0); }
 if(guard==2) SEND(sci,SCI_SETSEL,0,1);
 if(guard==3) SEND(sci,SCI_SETREADONLY,TRUE,0);
 if(guard==5) { document_set_text_changed(doc,FALSE);g_assert_true(document_close(doc)); }
 if(guard==6) document_new_file("other.c",NULL,"int n;\n");
 if(guard==7) { gtk_widget_destroy(d);timer=0;return G_SOURCE_REMOVE; }
 if(guard==9) SEND(sci,SCI_SETEOLMODE,SC_EOL_LF,0);
 if(guard==8) { g_free(doc->file_name);doc->file_name=g_strdup("renamed.c"); }
 timer=0;gtk_dialog_response(GTK_DIALOG(d),guard==4?GTK_RESPONSE_CANCEL:GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE;
}
static void reset(void) { SEND(sci,SCI_SETREADONLY,FALSE,0);sci_set_text(sci,original);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETEOLMODE,SC_EOL_CRLF,0);SEND(sci,SCI_SETSEL,23,23); }
static void command(gint g,gboolean capture) {
 guard=g;image=capture;timer=g_timeout_add(75,respond,NULL);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_PASTESYMBOL);
 /* Clipboard callbacks may run synchronously or asynchronously. */
 while(timer) g_main_context_iteration(NULL,TRUE);
}
static void interfere(ScintillaObject *s,gint id,SCNotification *nt,gpointer data) {
 (void)id;(void)data;if(!hook_done&&nt->nmhdr.code==SCN_MODIFIED&&(nt->modificationType&SC_MOD_INSERTCHECK)) { hook_done=TRUE;gchar *replacement=g_strnfill(nt->length,'X');SEND(s,SCI_CHANGEINSERTION,nt->length,(sptr_t)replacement);g_free(replacement); }
}
static gboolean step(gpointer unused) {
 (void)unused;doc=document_find_by_filename(path);if(!doc) return G_SOURCE_CONTINUE;sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));
 const gchar *payload="int helper(void) { return 1; }";gchar *expected=g_strconcat(payload,"\r\n",original,NULL);
 reset();gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),payload,-1);command(0,TRUE);check(expected);SEND(sci,SCI_UNDO,0,0);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);check(expected);
 for(gint g=1;g<=4;g++) { reset();command(g,FALSE);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0)); }
 reset();command(9,FALSE);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset();command(7,FALSE);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset();command(6,FALSE);check(original);document_open_file(path,FALSE,NULL,NULL);
 reset();sptr_t mask=SEND(sci,SCI_GETMODEVENTMASK,0,0);SEND(sci,SCI_SETMODEVENTMASK,mask|SC_MOD_INSERTCHECK,0);gulong hook=g_signal_connect(sci,"sci-notify",G_CALLBACK(interfere),NULL);command(0,FALSE);g_assert_true(hook_done);gchar *replacement=g_strnfill(strlen(payload)+2,'X'),*changed=g_strconcat(replacement,original,NULL);check(changed);g_free(replacement);g_free(changed);SEND(sci,SCI_UNDO,0,0);check(original);g_signal_handler_disconnect(sci,hook);SEND(sci,SCI_SETMODEVENTMASK,mask,0);
 reset();gchar *saved=g_strdup(doc->file_name);command(8,FALSE);check(original);g_free(doc->file_name);doc->file_name=saved;
 reset();gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"α\n",-1);gchar *mixed=g_strconcat("α\n",original,NULL);command(0,FALSE);check(mixed);g_free(mixed);
 gchar *disk=NULL;g_file_get_contents(path,&disk,NULL,NULL);g_assert_cmpstr(disk,==,original);g_free(disk);reset();command(5,FALSE);g_assert_null(document_find_by_filename(path));g_free(expected);
 GeanyDocument *current=document_get_current();if(current) document_set_text_changed(current,FALSE);g_print("Paste Symbol: key routing, exact clipboard/EOL, preview, Undo/Redo, revision/selection/readonly/rename/close/tab guards, destruction and InsertCheck substitution passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer unused) { (void)unused;g_error("Paste Symbol timeout guard %d",guard);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS")||!g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("paste-symbol-native-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL);g_mkdir(profile,0700);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL);path=g_build_filename(dir,"fixture.c",NULL);g_file_set_contents(path,original,-1,NULL);gchar *args[]={"paste-symbol-native","--new-instance","--no-session","--config",profile,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(200,step,NULL);g_timeout_add_seconds(45,timeout,NULL);return main_lib(6,args);
}
