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
static GtkWidget *window,*shade;
static ScintillaObject *sci,*map;
static guint phase;
static gchar *profile;
static GtkWidget *find(GtkWidget *root,const char *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root)); GtkWidget *found=NULL; for(GList *p=children;p && !found;p=p->next) found=find(p->data,name); g_list_free(children); if(found) return found; }
 return NULL;
}
static void activate(const char *name) { GtkWidget *item=find(ui_lookup_widget(window,"edit1_menu"),name); g_assert_nonnull(item); gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void color(gint line,gboolean bookmark,gboolean saved) {
 gint first=(gint)SSM(map,SCI_DOCLINEFROMVISIBLE,SSM(map,SCI_GETFIRSTVISIBLELINE,0,0),0),height=(gint)SSM(map,SCI_TEXTHEIGHT,0,0),width=gtk_widget_get_allocated_width(shade);
 cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,gtk_widget_get_allocated_height(shade)); cairo_t *cr=cairo_create(surface); gtk_widget_draw(shade,cr); cairo_destroy(cr); cairo_surface_flush(surface);
 gint y=(line-first)*height+height/2,x=bookmark ? 5 : width-3; g_assert_cmpint(y,>=,0); g_assert_cmpint(y,<,gtk_widget_get_allocated_height(shade));
 guint32 pixel=*(guint32 *)(cairo_image_surface_get_data(surface)+y*cairo_image_surface_get_stride(surface)+x*4); guint r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
 if(bookmark) { g_assert_cmpuint(b,>,r); g_assert_cmpuint(b,>,g); }
 else if(saved) { g_assert_cmpuint(g,>,r); g_assert_cmpuint(g,>,b); }
 else { g_assert_cmpuint(r,>,g); g_assert_cmpuint(g,>,b); }
 cairo_surface_destroy(surface);
}
static gboolean options(gpointer data) {
 (void)data; GList *windows=gtk_window_list_toplevels(); GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(GTK_IS_DIALOG(p->data) && !g_strcmp0(gtk_window_get_title(GTK_WINDOW(p->data)),"Overview Options")) dialog=p->data;
 g_list_free(windows); g_assert_nonnull(dialog);
 const char *names[]={"insight-overview-bookmarks","insight-overview-changes","insight-overview-saved","insight-overview-tips"};
 for(guint i=0;i<G_N_ELEMENTS(names);i++) { GtkWidget *check=find(dialog,names[i]); g_assert_nonnull(check); g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check))); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),FALSE); }
 gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_APPLY); return G_SOURCE_REMOVE;
}
static gboolean step(gpointer data) {
 (void)data; GeanyDocument *doc=document_get_current(); g_assert_nonnull(doc);
 if(phase==0) {
  sci=doc->editor->sci; window=gtk_widget_get_toplevel(GTK_WIDGET(sci)); gtk_window_resize(GTK_WINDOW(window),1100,800);
  GtkWidget *pane=gtk_widget_get_parent(GTK_WIDGET(sci)),*host=gtk_paned_get_child2(GTK_PANED(pane)); map=SCINTILLA(gtk_bin_get_child(GTK_BIN(host))); shade=find(host,"insight-overview-markers"); g_assert_nonnull(shade);
  g_assert_true(SSM(sci,SCI_GETCHANGEHISTORY,0,0)&SC_CHANGE_HISTORY_ENABLED);
  SSM(sci,SCI_INSERTTEXT,sci_get_position_from_line(sci,2),(sptr_t)"// change A "); SSM(sci,SCI_INSERTTEXT,sci_get_position_from_line(sci,3),(sptr_t)"// change A "); SSM(sci,SCI_INSERTTEXT,sci_get_position_from_line(sci,8),(sptr_t)"// change B ");
  SSM(sci,SCI_MARKERADD,5,1); sci_set_current_position(sci,0,FALSE);
 } else if(phase==1) {
  color(2,FALSE,FALSE); color(5,TRUE,FALSE);
  gchar *before=sci_get_contents(sci,sci_get_length(sci)+1); activate("insight-next-change"); g_assert_cmpint(sci_get_current_line(sci),==,2);
  activate("insight-next-change"); g_assert_cmpint(sci_get_current_line(sci),==,8);
  activate("insight-previous-change"); g_assert_cmpint(sci_get_current_line(sci),==,2);
  gchar *after=sci_get_contents(sci,sci_get_length(sci)+1); g_assert_cmpstr(before,==,after); g_free(before); g_free(after);
  SSM(sci,SCI_UNDO,0,0); g_assert_false(SSM(sci,SCI_MARKERGET,8,0)&(1<<SC_MARKNUM_HISTORY_MODIFIED));
  SSM(sci,SCI_REDO,0,0); g_assert_true(SSM(sci,SCI_MARKERGET,8,0)&(1<<SC_MARKNUM_HISTORY_MODIFIED));
  g_assert_true(document_save_file(doc,TRUE));
 } else if(phase==2) {
  color(2,FALSE,TRUE); g_assert_false(SSM(sci,SCI_MARKERGET,2,0)&(1<<SC_MARKNUM_HISTORY_MODIFIED));
  SSM(sci,SCI_INSERTTEXT,0,(sptr_t)"// inserted\n");
 } else if(phase==3) {
  color(6,TRUE,FALSE); g_assert_true(SSM(sci,SCI_MARKERGET,6,0)&2); SSM(sci,SCI_UNDO,0,0); g_assert_true(SSM(sci,SCI_MARKERGET,5,0)&2);
  GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS); event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map))); event->button.button=3; event->button.time=GDK_CURRENT_TIME; gdk_event_set_device(event,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()))); gtk_widget_event(GTK_WIDGET(map),event); gdk_event_free(event);
  GList *menus=gtk_menu_get_for_attach_widget(GTK_WIDGET(map)); g_assert_nonnull(menus); GtkWidget *item=find(menus->data,"insight-overview-options"); g_assert_nonnull(item); g_idle_add(options,NULL); gtk_menu_item_activate(GTK_MENU_ITEM(item));
 } else {
  document_set_text_changed(doc,FALSE); gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1"))); return G_SOURCE_REMOVE;
 }
 phase++; return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data; g_error("Overview marker GUI timeout at phase %u",phase); return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("overview-markers-%u",g_random_int()); profile=g_build_filename(cwd,name,NULL); g_free(cwd); g_free(name); g_mkdir_with_parents(profile,0700);
 gchar *config=g_build_filename(profile,"geany.conf",NULL); g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL); g_free(config);
 gchar *folder=g_build_filename(profile,"insight",NULL); g_mkdir_with_parents(folder,0700); config=g_build_filename(folder,"workbench.conf",NULL); g_file_set_contents(config,"[workbench]\noverview-selection=false\noverview-viewport=false\noverview-scale=50\n",-1,NULL); g_free(folder);
 gchar *path=g_build_filename(profile,"markers.c",NULL); GString *source=g_string_new(NULL); for(guint i=0;i<30;i++) g_string_append_printf(source,"int line_%u = %u;\n",i,i); g_file_set_contents(path,source->str,-1,NULL); g_string_free(source,TRUE);
 gchar *argv[]={"overview-markers","--new-instance","--no-session","--config",profile,path,NULL}; g_timeout_add(300,step,NULL); g_timeout_add_seconds(15,timeout,NULL); g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL); gint result=main_lib(6,argv);
 GKeyFile *key=g_key_file_new(); g_assert_true(g_key_file_load_from_file(key,config,G_KEY_FILE_NONE,NULL)); const char *keys[]={"overview-bookmarks","overview-changes","overview-saved","overview-tips"}; for(guint i=0;i<G_N_ELEMENTS(keys);i++) g_assert_false(g_key_file_get_boolean(key,"workbench",keys[i],NULL)); g_key_file_unref(key); g_free(config); g_free(path); g_free(profile); g_print("Overview markers GUI passed: rendered colors, change blocks, save, undo/redo, moving bookmarks and saved options\n"); return result;
}
