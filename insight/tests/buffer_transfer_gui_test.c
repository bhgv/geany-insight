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
static GtkWidget *window;static GeanyDocument *source,*target;static gchar *path,*target_path;static guint reply,timer;static gint mode,guard;static gboolean whole,image;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=list;p&&!f;p=p->next) f=named(p->data,name);g_list_free(list);return f; }return NULL;
}
static GtkWidget *dialog(void) { GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data)&&!g_strcmp0(gtk_widget_get_name(p->data),"insight-buffer-transfer-dialog")) found=p->data;g_list_free(list);return found; }
static void check(ScintillaObject *s,const gchar *expected) { gchar *text=sci_get_contents(s,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;
 if(!reply++) {
  gchar *id=g_strdup_printf("%u",guard==9?source->id:target->id);gtk_combo_box_set_active_id(GTK_COMBO_BOX(named(d,"insight-transfer-destination")),id);g_free(id);gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-transfer-mode")),mode);gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-transfer-scope")),whole?1:0);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_HELP);return G_SOURCE_CONTINUE;
 }
 if(reply==2) return G_SOURCE_CONTINUE;
 if(guard==7&&reply==3) { gtk_dialog_response(GTK_DIALOG(d),2);return G_SOURCE_CONTINUE; }
 if(guard==7&&reply==4) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_HELP);return G_SOURCE_CONTINUE; }
 if(guard==8&&reply==3) { SEND(target->editor->sci,SCI_APPENDTEXT,1,(sptr_t)"!");gtk_dialog_response(GTK_DIALOG(d),1);return G_SOURCE_CONTINUE; }
 if(guard==8&&reply==4) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_HELP);return G_SOURCE_CONTINUE; }
 GtkWidget *apply=gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY);
 if(guard==4) { g_assert_false(gtk_widget_get_sensitive(apply));timer=0;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
 g_assert_true(gtk_widget_get_sensitive(apply));
 if(image&&g_getenv("GEANY_BUFFER_TRANSFER_CAPTURE")) {
  ScintillaObject *before=SCINTILLA(named(d,"insight-transfer-before")),*after=SCINTILLA(named(d,"insight-transfer-after"));g_assert_true(SEND(after,SCI_GETREADONLY,0,0));g_assert_cmpint(SEND(before,SCI_GETMARGINWIDTHN,1,0),>,0);
  GdkPixbuf *pix=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(pix);g_assert_true(gdk_pixbuf_save(pix,g_getenv("GEANY_BUFFER_TRANSFER_CAPTURE"),"jpeg",NULL,"quality","90",NULL));g_object_unref(pix);
 }
 if(guard==1) { SEND(source->editor->sci,SCI_APPENDTEXT,1,(sptr_t)"x");SEND(source->editor->sci,SCI_UNDO,0,0); }
 if(guard==2) SEND(target->editor->sci,SCI_SETREADONLY,TRUE,0);
 if(guard==3) SEND(source->editor->sci,SCI_SETSEL,0,1);
 timer=0;gtk_dialog_response(GTK_DIALOG(d),guard==5?GTK_RESPONSE_CANCEL:GTK_RESPONSE_APPLY);

 return G_SOURCE_REMOVE;
}
static gboolean cancel_stale(gpointer data) { (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY)));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
static void command(gint m,gint g,gboolean all,gboolean capture) {
 mode=m;guard=g;whole=all;image=capture;reply=0;document_open_file(path,FALSE,NULL,NULL);timer=g_timeout_add(50,respond,NULL);if(g>=1&&g<=3) g_timeout_add(300,cancel_stale,NULL);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_BUFFERTRANSFER);if(timer) { g_source_remove(timer);timer=0; }
}
static void reset(void) {
 SEND(source->editor->sci,SCI_SETREADONLY,FALSE,0);sci_set_text(source->editor->sci,"// α\r\nint value = 7;\r\n");SEND(source->editor->sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(source->editor->sci,SCI_SETSEL,5,3);
 SEND(target->editor->sci,SCI_SETREADONLY,FALSE,0);sci_set_text(target->editor->sci,"int count = 1;\r\n");SEND(target->editor->sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(target->editor->sci,SCI_SETSEL,4,9);
}
static gboolean armed,hook_done;
static void interfere(ScintillaObject *s,gint id,SCNotification *nt,gpointer data) {
 (void)id;(void)data;if(armed&&!hook_done&&nt->nmhdr.code==SCN_MODIFIED&&(nt->modificationType&SC_MOD_INSERTCHECK)) { hook_done=TRUE;SEND(s,SCI_CHANGEINSERTION,2,(sptr_t)"XX"); }
}
static gboolean step(gpointer data) {
 (void)data;source=document_find_by_filename(path);if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));target=document_open_file(target_path,FALSE,NULL,NULL);g_assert_nonnull(target);
 const gchar *expected[]={"int count = 1;\r\nα","α","int countα = 1;\r\n","int α = 1;\r\n"};
 for(gint m=0;m<4;m++) { reset();command(m,0,FALSE,FALSE);check(target->editor->sci,expected[m]);SEND(target->editor->sci,SCI_UNDO,0,0);check(target->editor->sci,"int count = 1;\r\n");g_assert_false(SEND(target->editor->sci,SCI_CANUNDO,0,0));SEND(target->editor->sci,SCI_REDO,0,0);check(target->editor->sci,expected[m]); }
 reset();command(1,0,TRUE,TRUE);check(target->editor->sci,"// α\r\nint value = 7;\r\n");
 reset();SEND(source->editor->sci,SCI_SETREADONLY,TRUE,0);command(0,0,FALSE,FALSE);check(target->editor->sci,expected[0]);
 for(gint g=1;g<=3;g++) { reset();command(0,g,FALSE,FALSE);check(target->editor->sci,"int count = 1;\r\n"); }
 reset();SEND(target->editor->sci,SCI_SETREADONLY,TRUE,0);command(0,4,FALSE,FALSE);check(target->editor->sci,"int count = 1;\r\n");
 reset();command(0,7,FALSE,FALSE);GeanyDocument *created=document_get_current();g_assert_true(created!=source&&created!=target);g_assert_null(created->file_name);check(created->editor->sci,"α");document_set_text_changed(created,FALSE);
 reset();command(0,8,FALSE,FALSE);check(target->editor->sci,"int count = 1;\r\n!α");
 reset();command(0,9,FALSE,FALSE);check(source->editor->sci,"// α\r\nint value = 7;\r\nα");SEND(source->editor->sci,SCI_UNDO,0,0);check(source->editor->sci,"// α\r\nint value = 7;\r\n");
 reset();sptr_t mask=SEND(target->editor->sci,SCI_GETMODEVENTMASK,0,0);SEND(target->editor->sci,SCI_SETMODEVENTMASK,mask|SC_MOD_INSERTCHECK,0);gulong hook=g_signal_connect(target->editor->sci,"sci-notify",G_CALLBACK(interfere),NULL);armed=TRUE;command(0,0,FALSE,FALSE);armed=FALSE;g_assert_true(hook_done);check(target->editor->sci,"int count = 1;\r\nXX");SEND(target->editor->sci,SCI_UNDO,0,0);check(target->editor->sci,"int count = 1;\r\n");g_signal_handler_disconnect(target->editor->sci,hook);SEND(target->editor->sci,SCI_SETMODEVENTMASK,mask,0);
 reset();command(0,5,FALSE,FALSE);check(target->editor->sci,"int count = 1;\r\n");gchar *disk=NULL;g_file_get_contents(target_path,&disk,NULL,NULL);g_assert_cmpstr(disk,==,"int count = 1;\r\n");g_free(disk);
 document_set_text_changed(source,FALSE);document_set_text_changed(target,FALSE);g_print("Buffer transfer: four modes, selected/whole Unicode CRLF, read-only source/target, stale edits and selections, preview, single Undo/Redo and no disk writes passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Buffer transfer timeout %u guard %d",reply,guard);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS")||!g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("buffer-transfer-native-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL);g_mkdir(profile,0700);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL);path=g_build_filename(dir,"source.c",NULL);target_path=g_build_filename(dir,"target.c",NULL);g_file_set_contents(path,"// α\r\nint value = 7;\r\n",-1,NULL);g_file_set_contents(target_path,"int count = 1;\r\n",-1,NULL);gchar *args[]={"buffer-transfer-native","--new-instance","--no-session","--config",profile,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(100,step,NULL);g_timeout_add_seconds(50,timeout,NULL);return main_lib(6,args);
}
