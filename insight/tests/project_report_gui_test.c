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
static const gchar *original="int zebra(void) { return 1; }\n\nint alpha(void) { return 2; }\n";
static gchar *root,*project,*path,*output,*saved,*source_text;static GtkWidget *window;static GeanyDocument *source;static guint phase,mode;static gint undo;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *top(const gchar *name) { GList *list=gtk_window_list_toplevels();GtkWidget *v=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data) && !g_strcmp0(gtk_widget_get_name(p->data),name)) v=p->data;g_list_free(list);return v; }
static gchar *preview(GtkWidget *v) { GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(v,"insight-report-preview")));GtkTextIter a,z;gtk_text_buffer_get_bounds(b,&a,&z);return gtk_text_buffer_get_text(b,&a,&z,FALSE); }
static void toggle(GtkWidget *v,const gchar *name,gboolean value) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(v,name)),value); }
static gboolean types(gpointer data) { GtkWidget *v=top("insight-report-types-dialog");if(!v) return G_SOURCE_CONTINUE;GtkWidget *area=gtk_dialog_get_content_area(GTK_DIALOG(v));GList *stack=gtk_container_get_children(GTK_CONTAINER(area));while(stack) { GtkWidget *w=stack->data;stack=g_list_delete_link(stack,stack);if(GTK_IS_TOGGLE_BUTTON(w)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),GPOINTER_TO_INT(data));if(GTK_IS_CONTAINER(w)) stack=g_list_concat(stack,gtk_container_get_children(GTK_CONTAINER(w))); }gtk_dialog_response(GTK_DIALOG(v),GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE; }
static gboolean choose_save(gpointer data) {
 (void)data;GtkWidget *v=top("insight-report-save-dialog");if(!v) return G_SOURCE_CONTINUE;gchar *dir=g_path_get_dirname(output),*name=g_path_get_basename(output);g_assert_true(gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(v),dir));gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(v),name);g_free(dir);g_free(name);gtk_dialog_response(GTK_DIALOG(v),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *v=top("insight-project-report-dialog");if(!v || g_object_get_data(G_OBJECT(v),"insight-report-busy")) return G_SOURCE_CONTINUE;
 gchar *text=preview(v);if(!*text) { g_free(text);return G_SOURCE_CONTINUE; }g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(named(v,"insight-report-preview"))));
 if(mode==1) { g_assert_cmpstr(text,==,"a.c\nb.c\nempty.h\n");g_assert_false(gtk_widget_get_sensitive(named(v,"insight-report-symbols")));gtk_dialog_response(GTK_DIALOG(v),203);g_free(text);return G_SOURCE_CONTINUE; }
 if(mode==2) { mode=3;gtk_dialog_response(GTK_DIALOG(v),GTK_RESPONSE_CLOSE);g_free(text);return G_SOURCE_CONTINUE; }
 switch(phase++) {
 case 0:
  g_assert_nonnull(strstr(text,"Files: 3"));g_assert_nonnull(strstr(text,"File: empty.h"));g_assert_nonnull(strstr(text,"zebra"));g_assert_true(strstr(text,"zebra")<strstr(text,"alpha"));
  toggle(v,"insight-report-lines",FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(named(v,"insight-report-sort")),1);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(v),202)));gtk_dialog_response(GTK_DIALOG(v),201);break;
 case 1:g_assert_true(strstr(text,"alpha")<strstr(text,"zebra"));g_assert_null(strstr(text,"  1\t"));toggle(v,"insight-report-symbols",FALSE);gtk_dialog_response(GTK_DIALOG(v),201);break;
 case 2:g_assert_null(strstr(text,"zebra"));g_assert_nonnull(strstr(text,"Symbols: 0"));toggle(v,"insight-report-symbols",TRUE);toggle(v,"insight-report-lines",TRUE);gtk_combo_box_set_active(GTK_COMBO_BOX(named(v,"insight-report-sort")),0);gtk_dialog_response(GTK_DIALOG(v),201);break;
 case 3:g_timeout_add(50,types,GINT_TO_POINTER(FALSE));gtk_button_clicked(GTK_BUTTON(named(v,"insight-report-types")));gtk_dialog_response(GTK_DIALOG(v),201);break;
 case 4:g_assert_nonnull(strstr(text,"Symbols: 0"));g_timeout_add(50,types,GINT_TO_POINTER(TRUE));gtk_button_clicked(GTK_BUTTON(named(v,"insight-report-types")));gtk_dialog_response(GTK_DIALOG(v),201);break;
 case 5:
  g_assert_nonnull(strstr(text,"zebra"));saved=g_strdup(text);gtk_dialog_response(GTK_DIALOG(v),202);break;
 case 6: {
  gchar *clip=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clip,==,saved);g_free(clip);
  if(g_getenv("GEANY_PROJECT_REPORT_CAPTURE")) { GdkPixbuf *im=gdk_pixbuf_get_from_window(gtk_widget_get_window(v),0,0,gtk_widget_get_allocated_width(v),gtk_widget_get_allocated_height(v));g_assert_nonnull(im);g_assert_true(gdk_pixbuf_save(im,g_getenv("GEANY_PROJECT_REPORT_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(im); }
  g_timeout_add(80,choose_save,NULL);gtk_dialog_response(GTK_DIALOG(v),GTK_RESPONSE_ACCEPT);break;
 }
 case 7: { gchar *disk=NULL;g_assert_true(g_file_get_contents(output,&disk,NULL,NULL));g_assert_cmpstr(disk,==,saved);g_free(disk);gtk_dialog_response(GTK_DIALOG(v),203);break; }
 default:g_error("Unexpected report phase");
 }
 g_free(text);return G_SOURCE_CONTINUE;
}
static void invoke(gboolean key,gboolean files) { if(key) keybindings_send_command(GEANY_KEY_GROUP_EDITOR,files ? GEANY_KEYS_INSIGHT_EXPORTPROJECTFILES:GEANY_KEYS_INSIGHT_PROJECTREPORT);else { GtkWidget *item=named(window,files ? "insight-export-project-files":"insight-project-report");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); } }
static gboolean step(gpointer data) {
 (void)data;gchar *text=NULL;
 if(!source) { source=document_get_current();if(!source || !source->file_name || strcmp(source->file_name,path)) { source=NULL;return G_SOURCE_CONTINUE; }window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));source_text=g_strconcat(original," ",NULL);sci_set_text(source->editor->sci,source_text);undo=SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0);SEND(source->editor->sci,SCI_SETSEL,8,3); }
 if(mode==0) { invoke(FALSE,FALSE);if(phase<8) return G_SOURCE_CONTINUE;
 GeanyDocument *report=document_get_current();g_assert_true(report!=source);text=sci_get_contents(report->editor->sci,-1);g_assert_cmpstr(text,==,saved);g_free(text);document_set_text_changed(report,FALSE);g_assert_true(document_close(report));
 mode=1;return G_SOURCE_CONTINUE; }
 if(mode==1) { invoke(TRUE,TRUE);GeanyDocument *list=document_get_current();if(list==source) return G_SOURCE_CONTINUE;text=sci_get_contents(list->editor->sci,-1);g_assert_cmpstr(text,==,"a.c\nb.c\nempty.h\n");g_free(text);document_set_text_changed(list,FALSE);g_assert_true(document_close(list));
 mode=2;return G_SOURCE_CONTINUE; }
 invoke(TRUE,FALSE);if(mode!=3) return G_SOURCE_CONTINUE;
 g_assert_cmpint(SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0),==,undo);g_assert_cmpint(SEND(source->editor->sci,SCI_GETANCHOR,0,0),==,8);g_assert_cmpint(SEND(source->editor->sci,SCI_GETCURRENTPOS,0,0),==,3);
 text=sci_get_contents(source->editor->sci,-1);gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(text,==,source_text);g_assert_cmpstr(disk,==,original);g_free(text);g_free(disk);document_set_text_changed(source,FALSE);
 g_print("Project report GUI: real completed project index, zero-symbol files, menu/keys, line/name/symbol/type options, invalidation, full copy/save/open, file-list export, unchanged source/selection/Undo and window closure passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Project report timeout phase %u mode %u",phase,mode);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("project-report-native-XXXXXX",NULL);root=g_build_filename(dir,"source",NULL);g_assert_cmpint(g_mkdir(root,0700),==,0);project=g_build_filename(dir,"report.geany",NULL);path=g_build_filename(root,"a.c",NULL);output=g_build_filename(dir,"output.RPT",NULL);gchar *b=g_build_filename(root,"b.c",NULL),*empty=g_build_filename(root,"empty.h",NULL),*profile=g_build_filename(dir,"profile",NULL);g_assert_true(g_file_set_contents(path,original,-1,NULL));g_assert_true(g_file_set_contents(b,"int other(void) { return 3; }\n",-1,NULL));g_assert_true(g_file_set_contents(empty,"/* no symbols here */\n",-1,NULL));gchar *settings=g_strdup_printf("[project]\nname=Project report fixture\nbase_path=%s\n",root);g_assert_true(g_file_set_contents(project,settings,-1,NULL));g_free(settings);
 gchar *args[]={"project-report-native","--new-instance","--no-session","--config",profile,project,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(100,respond,NULL);g_timeout_add(300,step,NULL);g_timeout_add_seconds(50,timeout,NULL);return main_lib(7,args);
}
