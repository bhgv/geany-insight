/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>

#undef SSM
#define SSM(sci,msg,w,l) scintilla_send_message((sci),(msg),(w),(l))
static guint phase,original_id; static gint initial_size; static gchar *test_directory;
static GtkWidget *window,*overview_menu;
static ScintillaObject *map_for(GeanyDocument *doc) {
 GtkWidget *pane=gtk_widget_get_parent(GTK_WIDGET(doc->editor->sci)); g_assert_true(GTK_IS_PANED(pane)); GtkWidget *host=gtk_paned_get_child2(GTK_PANED(pane)); g_assert_true(GTK_IS_OVERLAY(host));
 return SCINTILLA(gtk_bin_get_child(GTK_BIN(host)));
}
static void same_text(GeanyDocument *doc) {
 ScintillaObject *map=map_for(doc); gchar *source=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1),*copy=sci_get_contents(map,sci_get_length(map)+1);
 g_assert_cmpuint(strlen(copy),==,strlen(source)); g_assert_true(!strcmp(copy,source)); g_free(source); g_free(copy); g_assert_false(SSM(doc->editor->sci,SCI_GETREADONLY,0,0)); g_assert_true(SSM(map,SCI_GETREADONLY,0,0));
}
static void screenshot(void) {
 if(!g_getenv("GEANY_GUI_SCREENSHOT")) return;
 GdkWindow *root=gdk_get_default_root_window(); GdkPixbuf *picture=gdk_pixbuf_get_from_window(root,0,0,MIN(1280,gdk_window_get_width(root)),MIN(900,gdk_window_get_height(root))); if(!picture) return;
 gint w=gdk_pixbuf_get_width(picture),h=gdk_pixbuf_get_height(picture),stride=gdk_pixbuf_get_rowstride(picture),channels=gdk_pixbuf_get_n_channels(picture); guchar *pixels=gdk_pixbuf_get_pixels(picture); GString *ppm=g_string_new(NULL); g_string_append_printf(ppm,"P6\n%d %d\n255\n",w,h);
 for(gint y=0;y<h;y++) for(gint x=0;x<w;x++) g_string_append_len(ppm,(const gchar *)pixels+y*stride+x*channels,3);
 g_file_set_contents("overview-gui-smoke.ppm",ppm->str,ppm->len,NULL); g_string_free(ppm,TRUE); g_object_unref(picture);
}
static gboolean timeout_fail(gpointer data) { (void)data; g_error("Overview GUI smoke timed out"); return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data; GeanyDocument *doc=document_get_current(); g_assert_nonnull(doc);
 if(phase==0) {
  GString *source=g_string_new(NULL); for(guint i=0;i<500;i++) g_string_append_printf(source,"int function_%u(void) { return %u; }\n",i,i);
  gchar *path=g_build_filename(test_directory,"overview-smoke.c",NULL); g_assert_true(g_file_set_contents(path,source->str,-1,NULL)); doc=document_open_file(path,FALSE,filetypes_lookup_by_name("C"),NULL); g_free(path); g_string_free(source,TRUE); original_id=doc->id;
  window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci)); gtk_window_resize(GTK_WINDOW(window),1100,800);
  GtkWidget *view=ui_lookup_widget(window,"menu_view1_menu"); GList *items=gtk_container_get_children(GTK_CONTAINER(view));
  for(GList *p=items;p;p=p->next) if(GTK_IS_CHECK_MENU_ITEM(p->data) && !g_strcmp0(gtk_menu_item_get_label(GTK_MENU_ITEM(p->data)),"Overview")) overview_menu=p->data;
  g_list_free(items); g_assert_nonnull(overview_menu); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview_menu),TRUE);
 } else if(phase==1) {
  same_text(doc); GtkNotebook *sidebar=GTK_NOTEBOOK(ui_lookup_widget(window,"notebook3")); GtkWidget *page=gtk_notebook_get_nth_page(sidebar,gtk_notebook_get_current_page(sidebar)); g_assert_cmpstr(gtk_notebook_get_tab_label_text(sidebar,page),==,"File Symbols"); g_assert_nonnull(doc->tm_file); g_assert_cmpuint(doc->tm_file->tags_array->len,>,0); g_assert_cmpint(SSM(map_for(doc),SCI_GETSTYLEAT,0,0),!=,0);
  SSM(doc->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"/* modified */\n"); SSM(doc->editor->sci,SCI_SETSEL,0,3); SSM(doc->editor->sci,SCI_SETFIRSTVISIBLELINE,200,0);
 } else if(phase==2) {
  same_text(doc); ScintillaObject *map=map_for(doc); gint first=(gint)SSM(map,SCI_GETFIRSTVISIBLELINE,0,0),main_first=(gint)SSM(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0);
  g_assert_cmpint(first,<=,main_first); g_assert_cmpint(first+SSM(map,SCI_LINESONSCREEN,0,0),>,main_first);
  screenshot(); initial_size=(gint)SSM(map,SCI_STYLEGETSIZEFRACTIONAL,STYLE_DEFAULT,0);
  gint caret=sci_get_current_position(doc->editor->sci); GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS); event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map))); event->button.button=1; event->button.x=10; event->button.y=10; gtk_widget_event(GTK_WIDGET(map),event); gdk_event_free(event);
  event=gdk_event_new(GDK_BUTTON_RELEASE); event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map))); event->button.button=1; event->button.x=10; event->button.y=10; gtk_widget_event(GTK_WIDGET(map),event); gdk_event_free(event);
  g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret); g_assert_cmpint(SSM(doc->editor->sci,SCI_GETANCHOR,0,0),==,0); g_assert_cmpint(SSM(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0),!=,main_first);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview_menu),FALSE); g_assert_false(gtk_widget_get_visible(gtk_widget_get_parent(GTK_WIDGET(map)))); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview_menu),TRUE);
  event=gdk_event_new(GDK_SCROLL); event->scroll.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map))); event->scroll.state=GDK_CONTROL_MASK; event->scroll.direction=GDK_SCROLL_UP; gtk_widget_event(GTK_WIDGET(map),event); gdk_event_free(event);
 } else if(phase==3) {
  same_text(doc); g_assert_cmpint(SSM(map_for(doc),SCI_STYLEGETSIZEFRACTIONAL,STYLE_DEFAULT,0),>,initial_size); document_set_text_changed(doc,FALSE); GeanyDocument *second=document_new_file(NULL,filetypes_lookup_by_name("C"),"int second(void) { return 2; }\n"); g_assert_nonnull(second); g_assert_true(document_close(document_find_by_id(original_id)));
 } else if(phase==4) {
  same_text(doc); document_set_text_changed(doc,FALSE);

  GtkWidget *quit=ui_lookup_widget(window,"menu_quit1"); g_assert_true(GTK_IS_MENU_ITEM(quit)); gtk_menu_item_activate(GTK_MENU_ITEM(quit)); g_print("Overview GUI smoke passed: syntax, independent read-only, edits, scrolling, toggling, tab close\n"); return G_SOURCE_REMOVE;
 }
 phase++; return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY"))) { g_print("Set GEANY_RUN_GUI_TESTS=1 and provide a display to run the real editor GUI smoke\n"); return 77; }
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("overview-gui-profile-%u",g_random_int()),*profile=g_build_filename(cwd,name,NULL); g_free(cwd); g_free(name); g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);
 gchar *config=g_build_filename(profile,"geany.conf",NULL); g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL)); g_free(config);
 gchar *argv[]={"overview-gui-smoke","--new-instance","--no-session","--config",profile,NULL}; g_timeout_add(700,step,NULL); g_timeout_add_seconds(20,timeout_fail,NULL);
 g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 test_directory=profile; gint result=main_lib(5,argv); g_free(profile); return result;
}
