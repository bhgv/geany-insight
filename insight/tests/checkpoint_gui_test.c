/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;
static GeanyDocument *source,*second,*extra;
static gchar *path,*second_path;
static guint mode;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *dialog(void) {
 GList *list=gtk_window_list_toplevels();GtkWidget *result=NULL;
 for(GList *p=list;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-checkpoint-dialog") && gtk_widget_get_visible(p->data)) result=p->data;
 g_list_free(list);return result;
}
static void action(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void contents(GeanyDocument *doc,const gchar *expected) { gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void disk(const gchar *name,const gchar *expected) { gchar *text=NULL;g_assert_true(g_file_get_contents(name,&text,NULL,NULL));g_assert_cmpstr(text,==,expected);g_free(text); }
static void edit(GeanyDocument *doc,const gchar *text) { sci_set_text(doc->editor->sci,text);g_assert_true(SEND(doc->editor->sci,SCI_CANUNDO,0,0)); }
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;
 GtkWidget *tree=named(d,"insight-checkpoint-files");g_assert_nonnull(tree);
 g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),NULL),==,mode==4 || mode==6 || mode==8 ? 2:1);
 if(mode==0 && g_getenv("GEANY_CHECKPOINT_CAPTURE")) { GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_CHECKPOINT_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
 if(mode==2) edit(source,"changed during confirmation\n");
 if(mode==3) extra=document_new_file(NULL,NULL,"extra\n");
 if(mode==6) edit(second,"other changed during confirmation\n");
 if(mode==7) { source->readonly=TRUE;sci_set_readonly(source->editor->sci,TRUE); }
 if(mode==8) { document_set_text_changed(second,FALSE);g_assert_true(document_close(second));second=NULL; }
 gtk_dialog_response(GTK_DIALOG(d),mode==1 ? GTK_RESPONSE_CANCEL:GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static void run(guint which,gboolean all,gboolean key) { mode=which;g_timeout_add(100,respond,NULL);if(key) keybindings_send_command(GEANY_KEY_GROUP_EDITOR,all ? GEANY_KEYS_INSIGHT_CHECKPOINTALL:GEANY_KEYS_INSIGHT_CHECKPOINT);else action(all ? "insight-checkpoint-all":"insight-checkpoint");g_assert_null(dialog()); }
static void clean(GeanyDocument *doc) { g_assert_false(SEND(doc->editor->sci,SCI_CANUNDO,0,0));g_assert_false(SEND(doc->editor->sci,SCI_CANREDO,0,0));g_assert_false(doc->changed); }
static void original(const gchar *expected) { GeanyDocument *before=document_get_current();action("insight-original-contents");GeanyDocument *view=document_get_current();g_assert_true(view!=before);contents(view,expected);g_assert_true(view->readonly);g_assert_true(document_close(view)); }
static gboolean mutate_other(GSignalInvocationHint *hint,guint n,const GValue *values,gpointer data) { (void)hint;(void)n;(void)data;GeanyDocument *doc=g_value_get_boxed(&values[1]);if(doc==source) edit(second,"modified by save hook\n");return TRUE; }
static gboolean step(gpointer data) {
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));
 edit(source,"α checkpoint\n");run(1,FALSE,FALSE);g_assert_true(SEND(source->editor->sci,SCI_CANUNDO,0,0));disk(path,"initial\n");original("initial\n");
 run(2,FALSE,FALSE);contents(source,"changed during confirmation\n");g_assert_true(SEND(source->editor->sci,SCI_CANUNDO,0,0));disk(path,"initial\n");
 run(0,FALSE,TRUE);clean(source);disk(path,"changed during confirmation\n");original("changed during confirmation\n");
 edit(source,"after checkpoint\n");original("changed during confirmation\n");action("menu_undo2");contents(source,"changed during confirmation\n");g_assert_true(SEND(source->editor->sci,SCI_CANREDO,0,0));run(0,FALSE,FALSE);clean(source);
 /* A clean file is checkpointed too, including native encoding/BOM history. */
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ui_lookup_widget(window,"menu_write_unicode_bom1")),TRUE);g_assert_true(source->has_bom);run(0,FALSE,FALSE);clean(source);action("menu_undo2");g_assert_true(source->has_bom);
 source->has_bom=FALSE;document_set_text_changed(source,TRUE);g_assert_true(document_save_file(source,TRUE));
 edit(source,"unsaved\n");run(3,FALSE,FALSE);g_assert_true(document_get_current()==extra);g_assert_true(SEND(source->editor->sci,SCI_CANUNDO,0,0));document_set_text_changed(extra,FALSE);g_assert_true(document_close(extra));
 run(7,FALSE,FALSE);g_assert_true(SEND(source->editor->sci,SCI_CANUNDO,0,0));source->readonly=FALSE;sci_set_readonly(source->editor->sci,FALSE);
 extra=document_new_file(NULL,NULL,"untitled\n");action("insight-checkpoint-all");g_assert_null(dialog());g_assert_true(SEND(source->editor->sci,SCI_CANUNDO,0,0));document_set_text_changed(extra,FALSE);g_assert_true(document_close(extra));
 second=document_open_file(second_path,FALSE,NULL,NULL);edit(second,"second checkpoint\n");edit(source,"first checkpoint\n");
 run(6,TRUE,FALSE);g_assert_true(SEND(source->editor->sci,SCI_CANUNDO,0,0));g_assert_true(SEND(second->editor->sci,SCI_CANUNDO,0,0));disk(second_path,"second initial\n");
 /* A save hook changing another member aborts before clearing either history. */
 guint signal=g_signal_lookup("document-before-save",g_type_from_name("GeanyObject"));g_assert_cmpuint(signal,!=,0);gulong hook=g_signal_add_emission_hook(signal,0,mutate_other,NULL,NULL);
 run(4,TRUE,TRUE);g_signal_remove_emission_hook(signal,hook);g_assert_true(SEND(source->editor->sci,SCI_CANUNDO,0,0));g_assert_true(SEND(second->editor->sci,SCI_CANUNDO,0,0));contents(second,"modified by save hook\n");
 run(4,TRUE,TRUE);clean(source);clean(second);disk(path,"first checkpoint\n");disk(second_path,"modified by save hook\n");original("modified by save hook\n");
 edit(source,"pending first\n");edit(second,"pending second\n");run(8,TRUE,FALSE);g_assert_true(SEND(source->editor->sci,SCI_CANUNDO,0,0));disk(path,"first checkpoint\n");
 /* Snapshot rejection keeps undo even when saving already succeeded. */
 gchar invalid[]={'b',(gchar)0xff,'\n',0};sci_set_text(source->editor->sci,invalid);run(0,FALSE,FALSE);g_assert_true(SEND(source->editor->sci,SCI_CANUNDO,0,0));
 document_set_text_changed(source,FALSE);
 g_print("Checkpoint GUI: Cancel, stale edits/tab/file set/read-only, single/all key dispatch, save-hook failure, native BOM and text Undo/Redo, UTF-8 guard, new original and saved disk passed\n");
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Checkpoint timeout mode %u",mode);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("checkpoint-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL);path=g_build_filename(dir,"source.c",NULL);second_path=g_build_filename(dir,"second.c",NULL);
 g_assert_true(g_file_set_contents(path,"initial\n",-1,NULL));g_assert_true(g_file_set_contents(second_path,"second initial\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 gchar *args[]={"checkpoint","--new-instance","--no-session","--config",dir,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(40,timeout,NULL);return main_lib(6,args);
}
