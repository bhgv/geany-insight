/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#undef SSM
#define SSM(sci,msg,w,l) scintilla_send_message((sci),(msg),(w),(l))
static guint phase,attempts;
static gchar *profile,*settings;
static GtkWidget *window,*shade;
static ScintillaObject *sci,*map;
static GtkWidget *find(GtkWidget *root,const char *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root)); GtkWidget *found=NULL; for(GList *p=children;p && !found;p=p->next) found=find(p->data,name); g_list_free(children); if(found) return found; } return NULL;
}
static guint alpha(gint line) {
 gint first=(gint)SSM(map,SCI_DOCLINEFROMVISIBLE,SSM(map,SCI_GETFIRSTVISIBLELINE,0,0),0),height=(gint)SSM(map,SCI_TEXTHEIGHT,0,0),width=gtk_widget_get_allocated_width(shade),y=(line-first)*height+height/2;
 g_assert_cmpint(y,>=,0); g_assert_cmpint(y,<,gtk_widget_get_allocated_height(shade));
 cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,gtk_widget_get_allocated_height(shade)); cairo_t *cr=cairo_create(surface); gtk_widget_draw(shade,cr); cairo_destroy(cr); cairo_surface_flush(surface);
 guint32 pixel=*(guint32 *)(cairo_image_surface_get_data(surface)+y*cairo_image_surface_get_stride(surface)+10*4); cairo_surface_destroy(surface); return pixel>>24;
}
static void caret(gint line) { sci_set_current_position(sci,sci_get_position_from_line(sci,line)+2,FALSE); }
static gboolean edit_options(gpointer data) {
 (void)data; GList *windows=gtk_window_list_toplevels(); GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(GTK_IS_DIALOG(p->data) && !g_strcmp0(gtk_window_get_title(GTK_WINDOW(p->data)),"Overview Options")) dialog=p->data;
 g_list_free(windows); g_assert_nonnull(dialog); GtkWidget *current=find(dialog,"insight-overview-function"),*all=find(dialog,"insight-overview-all-functions"); g_assert_nonnull(current); g_assert_nonnull(all); g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(current))); g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(all))); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(all),TRUE); gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_APPLY); return G_SOURCE_REMOVE;
}
static gboolean step(gpointer data) {
 (void)data; GeanyDocument *doc=document_get_current(); if(phase<7) g_assert_nonnull(doc);
 if(phase==0) {
  sci=doc->editor->sci; window=gtk_widget_get_toplevel(GTK_WIDGET(sci)); gtk_window_resize(GTK_WINDOW(window),1100,800); GtkWidget *host=gtk_paned_get_child2(GTK_PANED(gtk_widget_get_parent(GTK_WIDGET(sci)))); map=SCINTILLA(gtk_bin_get_child(GTK_BIN(host))); shade=find(host,"insight-overview-markers"); g_assert_nonnull(shade); caret(2);
 } else if(phase==1) {
  if(alpha(2)<200) { g_assert_cmpuint(++attempts,<,30); return G_SOURCE_CONTINUE; }
  g_assert_cmpuint(alpha(4),>,200); g_assert_cmpuint(alpha(7),==,0); g_assert_cmpuint(alpha(10),==,0); caret(10);
 } else if(phase==2) {
  g_assert_cmpuint(alpha(10),>,200); g_assert_cmpuint(alpha(2),==,0); caret(12);
 } else if(phase==3) {
  g_assert_cmpuint(alpha(10),==,0); g_assert_cmpuint(alpha(2),==,0);
  GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS); event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map))); event->button.button=3; event->button.time=GDK_CURRENT_TIME; gdk_event_set_device(event,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()))); gtk_widget_event(GTK_WIDGET(map),event); gdk_event_free(event);
  GList *menus=gtk_menu_get_for_attach_widget(GTK_WIDGET(map)); g_assert_nonnull(menus); GtkWidget *item=find(menus->data,"insight-overview-options"); g_assert_nonnull(item); g_idle_add(edit_options,NULL); gtk_menu_item_activate(GTK_MENU_ITEM(item));
 } else if(phase==4) {
  g_assert_cmpuint(alpha(2),>,0); g_assert_cmpuint(alpha(10),>,0); g_assert_cmpuint(alpha(7),==,0);
  SSM(sci,SCI_INSERTTEXT,0,(sptr_t)"// inserted line\n"); caret(3);
 } else if(phase==5) {
  if(alpha(3)<200) { g_assert_cmpuint(++attempts,<,30); return G_SOURCE_CONTINUE; }
  g_assert_cmpuint(alpha(8),==,0);
  const char *text="int incomplete(void) {\n  return 1;\n"; sci_set_text(sci,text); caret(1);
 } else if(phase==6) {
  g_assert_cmpuint(alpha(1),==,0);
  /* Queue a fresh computation then destroy its pane/document before completion. */
  sci_set_text(sci,"int closing(void) { return 1; }\n"); document_set_text_changed(doc,FALSE); document_close(doc);
 } else {
  gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1"))); return G_SOURCE_REMOVE;
 }
 phase++; return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data; g_error("Overview function GUI timeout at phase %u",phase); return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("overview-functions-%u",g_random_int()); profile=g_build_filename(cwd,name,NULL); g_free(cwd); g_free(name); g_mkdir_with_parents(profile,0700);
 gchar *config=g_build_filename(profile,"geany.conf",NULL); g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL); g_free(config); gchar *folder=g_build_filename(profile,"insight",NULL); g_mkdir_with_parents(folder,0700); settings=g_build_filename(folder,"workbench.conf",NULL); g_file_set_contents(settings,"[workbench]\nversion=1\noverview-selection=false\noverview-viewport=false\noverview-bookmarks=false\noverview-changes=false\noverview-scale=50\n",-1,NULL); g_free(folder);
 gchar *path=g_build_filename(profile,"functions.c",NULL); g_file_set_contents(path,"int first(void)\n{\n  const char *text = \"}\";\n  /* } */\n  return 1;\n}\n\nint prototype(void);\nint second(void)\n{\n  return 2;\n}\nint outside = 0;\n",-1,NULL);
 gchar *argv[]={"overview-functions","--new-instance","--no-session","--config",profile,path,NULL}; g_timeout_add(250,step,NULL); g_timeout_add_seconds(20,timeout,NULL); g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL); gint result=main_lib(6,argv);
 GKeyFile *key=g_key_file_new(); g_assert_true(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL)); g_assert_true(g_key_file_get_boolean(key,"workbench","overview-function",NULL)); g_assert_true(g_key_file_get_boolean(key,"workbench","overview-all-functions",NULL)); g_key_file_unref(key); g_free(settings); g_free(profile); g_free(path); g_print("Overview function GUI passed: caret tracking, comments/strings, prototypes, all functions, edits, incomplete function, close and persistent options\n"); return result;
}
