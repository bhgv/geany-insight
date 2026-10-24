/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GeanyDocument *docs[4];
static gchar *paths[4],*text;
static GtkWidget *window,*host,*chooser;
static guint phase;
static GtkWidget *named(GtkWidget *widget,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(widget),name)) return widget;
 if(GTK_IS_MENU_ITEM(widget)) {GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget));if(menu) {GtkWidget *found=named(menu,name);if(found) return found;}}
 if(GTK_IS_CONTAINER(widget)) {GList *children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found;}return NULL;
}
static GtkWidget *toplevel(const gchar *name) {GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p && !found;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),name)) found=p->data;g_list_free(windows);return found;}
static void action(guint command) {keybindings_send_command(GEANY_KEY_GROUP_EDITOR,command);host=toplevel("insight-linked-source-windows");}
static void activate(GeanyDocument *doc) {
 GtkWidget *page=GTK_WIDGET(doc->editor->sci);while(gtk_widget_get_parent(page) && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) page=gtk_widget_get_parent(page);
 if(gtk_widget_get_parent(page) && GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) gtk_notebook_set_current_page(GTK_NOTEBOOK(gtk_widget_get_parent(page)),gtk_notebook_page_num(GTK_NOTEBOOK(gtk_widget_get_parent(page)),page));gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
}
static GdkRectangle rect(GeanyDocument *doc) {GtkWidget *sci=GTK_WIDGET(doc->editor->sci);GdkRectangle result={0};g_assert_true(gtk_widget_translate_coordinates(sci,host,0,0,&result.x,&result.y));result.width=gtk_widget_get_allocated_width(sci);result.height=gtk_widget_get_allocated_height(sci);g_assert_cmpint(result.width,>,5);g_assert_cmpint(result.height,>,5);return result;}
static void separated(GeanyDocument *a,GeanyDocument *b,gboolean vertical) {GdkRectangle x=rect(a),y=rect(b);if(vertical) g_assert_true(x.x+x.width<=y.x || y.x+y.width<=x.x);else g_assert_true(x.y+x.height<=y.y || y.y+y.height<=x.y);}
static void intact(GeanyDocument *doc) {gchar *value=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(value,==,text);g_free(value);g_assert_false(doc->changed);}
static void key(guint val,GdkModifierType state) {GdkEventKey event={0};event.type=GDK_KEY_PRESS;event.keyval=val;event.state=state;gboolean handled=FALSE;g_signal_emit_by_name(host,"key-press-event",&event,&handled);g_assert_true(handled);}
static gboolean step(gpointer data) {
 (void)data;
 if(phase==0) {
  docs[0]=document_get_current();if(!DOC_VALID(docs[0]) || g_strcmp0(docs[0]->file_name,paths[0])) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(docs[0]->editor->sci));for(guint n=1;n<4;n++) {docs[n]=document_open_file(paths[n],FALSE,NULL,NULL);g_assert_nonnull(docs[n]);}
  activate(docs[2]);activate(docs[0]);action(GEANY_KEYS_INSIGHT_TILETWO);g_assert_nonnull(host);g_assert_true(gtk_widget_get_toplevel(GTK_WIDGET(docs[0]->editor->sci))==host);g_assert_true(gtk_widget_get_toplevel(GTK_WIDGET(docs[2]->editor->sci))==host);g_assert_true(gtk_widget_get_toplevel(GTK_WIDGET(docs[1]->editor->sci))==window);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==1) {
  separated(docs[0],docs[2],TRUE);g_assert_true(document_get_current()==docs[0]);g_assert_cmpint(document_get_notebook_page(docs[0]),>=,0);
  /* The real native keyboard dispatcher, source focus checks and callbacks. */
  gtk_widget_grab_focus(GTK_WIDGET(docs[0]->editor->sci));SEND(docs[0]->editor->sci,SCI_SETSEL,0,3);key(GDK_KEY_c,GDK_CONTROL_MASK);gchar *clipboard=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clipboard,==,"int");g_free(clipboard);
  SEND(docs[0]->editor->sci,SCI_SETSEL,0,0);key(GDK_KEY_d,GDK_CONTROL_MASK);g_assert_true(docs[0]->changed);key(GDK_KEY_z,GDK_CONTROL_MASK);intact(docs[0]);key(GDK_KEY_v,GDK_CONTROL_MASK);g_assert_true(docs[0]->changed);key(GDK_KEY_z,GDK_CONTROL_MASK);intact(docs[0]);
  SEND(docs[0]->editor->sci,SCI_APPENDTEXT,1,(sptr_t)"!");key(GDK_KEY_s,GDK_CONTROL_MASK);g_assert_false(docs[0]->changed);gchar *disk=NULL;g_assert_true(g_file_get_contents(paths[0],&disk,NULL,NULL));g_assert_true(g_str_has_suffix(disk,"!"));g_free(disk);key(GDK_KEY_z,GDK_CONTROL_MASK);key(GDK_KEY_s,GDK_CONTROL_MASK);intact(docs[0]);
  gtk_button_clicked(GTK_BUTTON(named(host,"insight-source-arrangement-horizontal")));host=toplevel("insight-linked-source-windows");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {separated(docs[0],docs[2],FALSE);action(GEANY_KEYS_INSIGHT_TILEVERTICAL);phase++;return G_SOURCE_CONTINUE;}
 if(phase==3) {
  g_assert_nonnull(host);for(guint n=0;n<4;n++) g_assert_true(gtk_widget_get_toplevel(GTK_WIDGET(docs[n]->editor->sci))==host);
  for(guint n=0;n<4;n++) for(guint k=n+1;k<4;k++) {GdkRectangle a=rect(docs[n]),b=rect(docs[k]);g_assert_true(a.x+a.width<=b.x || b.x+b.width<=a.x || a.y+a.height<=b.y || b.y+b.height<=a.y);}
  action(GEANY_KEYS_INSIGHT_ZOOMSOURCE);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==4) {
  g_assert_true(gtk_widget_get_mapped(GTK_WIDGET(docs[0]->editor->sci)));for(guint n=1;n<4;n++) g_assert_false(gtk_widget_get_mapped(GTK_WIDGET(docs[n]->editor->sci)));action(GEANY_KEYS_INSIGHT_ZOOMSOURCE);action(GEANY_KEYS_INSIGHT_EQUALIZESOURCES);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==5) {
  for(guint n=0;n<4;n++) g_assert_true(gtk_widget_get_mapped(GTK_WIDGET(docs[n]->editor->sci)));action(GEANY_KEYS_INSIGHT_TILEONE);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==6) {
  g_assert_true(gtk_widget_get_toplevel(GTK_WIDGET(docs[0]->editor->sci))==host);for(guint n=1;n<4;n++) g_assert_true(gtk_widget_get_toplevel(GTK_WIDGET(docs[n]->editor->sci))==window);action(GEANY_KEYS_INSIGHT_RESTORESOURCETABS);g_assert_null(host);for(guint n=0;n<4;n++) {g_assert_true(gtk_widget_get_toplevel(GTK_WIDGET(docs[n]->editor->sci))==window);intact(docs[n]);}
  action(GEANY_KEYS_INSIGHT_ARRANGESOURCES);chooser=toplevel("insight-source-arrangement-dialog");g_assert_nonnull(chooser);gtk_button_clicked(GTK_BUTTON(named(chooser,"insight-source-arrangement-clear")));gtk_entry_set_text(GTK_ENTRY(named(chooser,"insight-source-arrangement-query")),"b.c");gtk_button_clicked(GTK_BUTTON(named(chooser,"insight-source-arrangement-select")));gtk_entry_set_text(GTK_ENTRY(named(chooser,"insight-source-arrangement-query")),"d.c");gtk_button_clicked(GTK_BUTTON(named(chooser,"insight-source-arrangement-select")));gtk_entry_set_text(GTK_ENTRY(named(chooser,"insight-source-arrangement-query")),"");gtk_combo_box_set_active(GTK_COMBO_BOX(named(chooser,"insight-source-arrangement-axis")),1);gtk_dialog_response(GTK_DIALOG(chooser),GTK_RESPONSE_APPLY);host=toplevel("insight-linked-source-windows");g_assert_nonnull(host);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==7) {
  separated(docs[1],docs[3],FALSE);g_assert_true(document_get_current()==docs[1]);g_assert_true(gtk_widget_get_toplevel(GTK_WIDGET(docs[0]->editor->sci))==window);
  /* Checked document identities cannot be silently replaced after closing. */
  action(GEANY_KEYS_INSIGHT_RESTORESOURCETABS);g_assert_true(document_close(docs[3]));gtk_dialog_response(GTK_DIALOG(chooser),GTK_RESPONSE_APPLY);g_assert_null(toplevel("insight-linked-source-windows"));gtk_dialog_response(GTK_DIALOG(chooser),GTK_RESPONSE_HELP);gtk_dialog_response(GTK_DIALOG(chooser),GTK_RESPONSE_CLOSE);
  action(GEANY_KEYS_INSIGHT_TILEHORIZONTAL);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==8) {
  g_assert_nonnull(host);separated(docs[0],docs[1],FALSE);gboolean handled=FALSE;g_signal_emit_by_name(host,"delete-event",NULL,&handled);g_assert_true(handled);g_assert_null(toplevel("insight-linked-source-windows"));for(guint n=0;n<3;n++) {intact(docs[n]);g_assert_cmpint(document_get_notebook_page(docs[n]),>=,0);}
  g_print("Source arrangements: horizontal/vertical/two-MRU/one, non-overlapping real panes, filtered checked subset, stale IDs, zoom/restore/equalize, native Copy/Paste/Duplicate/Undo/Save key dispatch and document preservation passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean expired(gpointer data) { (void)data;g_error("Source arrangement GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;gchar *root=g_dir_make_tmp("source-arrangement-XXXXXX",NULL),*profile=g_build_filename(root,"profile",NULL);g_assert_cmpint(g_mkdir(profile,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nuse_native_dialogs=false\n",-1,NULL));GString *body=g_string_new(NULL);for(guint n=0;n<40;n++) g_string_append_printf(body,"int value_%u = %u;\n",n,n);text=g_string_free(body,FALSE);const gchar *names[]={"a.c","b.c","c.c","d.c"};for(guint n=0;n<4;n++) {paths[n]=g_build_filename(root,names[n],NULL);g_assert_true(g_file_set_contents(paths[n],text,-1,NULL));}
 gchar *args[]={"source-arrangement","--new-instance","--no-session","--config",profile,paths[0],NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(40,expired,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,args);for(guint n=0;n<4;n++) {gchar *disk=NULL;g_assert_true(g_file_get_contents(paths[n],&disk,NULL,NULL));g_assert_cmpstr(disk,==,text);g_free(disk);}return result;
}
