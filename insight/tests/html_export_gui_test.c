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
static GtkWidget *window;static GeanyDocument *source;static gchar *path,*output,*html;static guint phase,mode;static gint undo;static gint64 capture_after;
static const gchar *disk_text="// original\r\nint value = 1;\r\n",*current="// α <& comment\r\nint value = 42;\r\n\r\nint answer(void) { return value; }\r\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=list;p && !found;p=p->next) found=named(p->data,name);g_list_free(list);return found; }return NULL;
}
static GtkWidget *top(const gchar *name) { GList *list=gtk_window_list_toplevels();GtkWidget *v=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data) && !g_strcmp0(gtk_widget_get_name(p->data),name)) v=p->data;g_list_free(list);return v; }
static gboolean choose(gpointer data) {
 (void)data;GtkWidget *v=top("insight-html-save-dialog");if(!v) return G_SOURCE_CONTINUE;if(!g_object_get_data(G_OBJECT(v),"fixture-path")) { gchar *dir=g_path_get_dirname(output),*name=g_path_get_basename(output);g_assert_true(gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(v),dir));gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(v),name);g_free(dir);g_free(name);g_object_set_data(G_OBJECT(v),"fixture-path",GINT_TO_POINTER(1));return G_SOURCE_CONTINUE; }gchar *p=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(v));gboolean ready=!g_strcmp0(p,output);g_free(p);if(!ready) return G_SOURCE_CONTINUE;gtk_dialog_response(GTK_DIALOG(v),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static gboolean respond(gpointer data) {
 (void)data;if(top("insight-html-save-dialog")) return G_SOURCE_CONTINUE;GtkWidget *v=top("insight-html-export-dialog");if(!v || g_object_get_data(G_OBJECT(v),"insight-html-busy")) return G_SOURCE_CONTINUE;
 if(mode==1) { phase++;gtk_dialog_response(GTK_DIALOG(v),202);mode=2;return G_SOURCE_CONTINUE; }
 if(mode==2) { gchar *selection=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(strstr(selection,"id=\"L2\""));g_assert_null(strstr(selection,"id=\"L1\""));g_assert_nonnull(strstr(selection,"value"));g_assert_null(strstr(selection,"answer"));g_free(selection);gtk_dialog_response(GTK_DIALOG(v),GTK_RESPONSE_CLOSE);return G_SOURCE_CONTINUE; }
 if(mode==3) { phase++;gtk_dialog_response(GTK_DIALOG(v),GTK_RESPONSE_CLOSE);return G_SOURCE_CONTINUE; }
 GtkWidget *sci=named(v,"insight-html-source-preview");g_assert_true(SEND(SCINTILLA(sci),SCI_GETREADONLY,0,0));gchar *text=sci_get_contents(SCINTILLA(sci),-1);g_assert_cmpstr(text,==,current);g_free(text);
 switch(phase++) {
 case 0:
  g_assert_cmpint(SEND(SCINTILLA(sci),SCI_GETSTYLEAT,0,0),!=,SEND(SCINTILLA(sci),SCI_GETSTYLEAT,strlen("// α <& comment\r\n"),0));gtk_dialog_response(GTK_DIALOG(v),202);break;
 case 1:html=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(strstr(html,"&lt;&amp;"));g_assert_nonnull(strstr(html,"id=\"L1\""));g_assert_nonnull(strstr(html,"aria-hidden=\"true\""));g_assert_null(strstr(html,"\r"));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(v,"insight-html-lines")),FALSE);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(v),202)));gtk_dialog_response(GTK_DIALOG(v),201);break;
 case 2:gtk_dialog_response(GTK_DIALOG(v),202);break;
 case 3: { gchar *copy=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_null(strstr(copy,"aria-hidden=\"true\""));g_free(copy);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(v,"insight-html-lines")),TRUE);gtk_dialog_response(GTK_DIALOG(v),201);break; }
 case 4:g_assert_cmpint(SEND(source->editor->sci,SCI_GETANCHOR,0,0),==,8);g_assert_cmpint(SEND(source->editor->sci,SCI_GETCURRENTPOS,0,0),==,3);g_assert_cmpint(SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0),==,undo);capture_after=g_get_monotonic_time()+150*G_TIME_SPAN_MILLISECOND;break;
 case 5:
  if(g_get_monotonic_time()<capture_after) { phase--;break; }
  if(g_getenv("GEANY_HTML_EXPORT_CAPTURE")) { GdkPixbuf *im=gdk_pixbuf_get_from_window(gtk_widget_get_window(v),0,0,gtk_widget_get_allocated_width(v),gtk_widget_get_allocated_height(v));g_assert_nonnull(im);g_assert_true(gdk_pixbuf_save(im,g_getenv("GEANY_HTML_EXPORT_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(im); }
  /* Later source edits must not replace the captured HTML/code preview. */
  sci_set_text(source->editor->sci,"later edit\n");g_timeout_add(80,choose,NULL);gtk_dialog_response(GTK_DIALOG(v),GTK_RESPONSE_ACCEPT);break;
 case 6: { gchar *saved=NULL;g_assert_true(g_file_get_contents(output,&saved,NULL,NULL));g_assert_cmpstr(saved,==,html);g_free(saved);gtk_dialog_response(GTK_DIALOG(v),203);break; }
 default:g_error("Unexpected HTML phase");
 }
 return G_SOURCE_CONTINUE;
}
static void invoke(gboolean key,gboolean selection) { if(key) keybindings_send_command(GEANY_KEY_GROUP_EDITOR,selection ? GEANY_KEYS_INSIGHT_EXPORTSELECTIONHTML:GEANY_KEYS_INSIGHT_EXPORTHTML);else { GtkWidget *item=named(window,selection ? "insight-export-selection-html":"insight-export-html");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); } }
static gboolean step(gpointer data) {
 (void)data;source=document_get_current();if(!source || !source->file_name || strcmp(source->file_name,path)) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));sci_set_text(source->editor->sci,current);SEND(source->editor->sci,SCI_SETSEL,8,3);undo=SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0);
 invoke(FALSE,FALSE);g_assert_cmpuint(phase,==,7);GeanyDocument *exported=document_get_current();g_assert_true(exported!=source);gchar *text=sci_get_contents(exported->editor->sci,-1);g_assert_cmpstr(text,==,html);g_free(text);document_set_text_changed(exported,FALSE);g_assert_true(document_close(exported));
 text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,"later edit\n");g_free(text);SEND(source->editor->sci,SCI_UNDO,0,0);text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,current);g_free(text);g_assert_cmpint(SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0),==,undo);
 gint begin=strlen("// α <& comment\r\n"),end=begin+strlen("int value = 42;");SEND(source->editor->sci,SCI_SETSEL,end,begin);mode=1;invoke(TRUE,TRUE);g_assert_null(top("insight-html-export-dialog"));g_assert_cmpint(SEND(source->editor->sci,SCI_GETANCHOR,0,0),==,end);g_assert_cmpint(SEND(source->editor->sci,SCI_GETCURRENTPOS,0,0),==,begin);
 SEND(source->editor->sci,SCI_SETSEL,0,0);invoke(TRUE,TRUE);g_assert_null(top("insight-html-export-dialog"));SEND(source->editor->sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);SEND(source->editor->sci,SCI_SETSEL,8,3);invoke(FALSE,TRUE);g_assert_null(top("insight-html-export-dialog"));SEND(source->editor->sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);
 SEND(source->editor->sci,SCI_SETREADONLY,TRUE,0);mode=3;guint before=phase;invoke(TRUE,FALSE);g_assert_cmpuint(phase,==,before+1);SEND(source->editor->sci,SCI_SETREADONLY,FALSE,0);
 gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,disk_text);g_free(disk);text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,current);g_free(text);document_set_text_changed(source,FALSE);
 g_print("HTML export GUI: menu/keys, actual lexer style capture, read-only colored preview, line option invalidation, full copy/save/open, captured versus later text, reversed selection anchors, unsupported selections, read-only source and unchanged disk/Undo passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("HTML export timeout phase %u mode %u",phase,mode);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;gchar *dir=g_dir_make_tmp("html-export-native-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL);path=g_build_filename(dir,"source.c",NULL);output=g_build_filename(dir,"export.html",NULL);g_assert_true(g_file_set_contents(path,disk_text,-1,NULL));gchar *args[]={"html-export-native","--new-instance","--no-session","--config",profile,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(100,respond,NULL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(50,timeout,NULL);return main_lib(6,args);
}
