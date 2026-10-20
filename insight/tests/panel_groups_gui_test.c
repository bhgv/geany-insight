/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*pages[8],*floating;static guint phase;static gboolean restore,corrupt;static gchar *profile;static gboolean drag_done;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }
 if(GTK_IS_MENU_BUTTON(w)) { GtkWidget *menu=GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(w)));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *identified(GtkWidget *w,guint id) { if(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w),"insight-panel-id"))==id+1) return w;if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=identified(p->data,id);g_list_free(children);return found; }return NULL; }
static GtkWidget *find_page(guint id) { GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p && !found;p=p->next) found=identified(p->data,id);g_list_free(windows);return found; }
static void click(guint id,const gchar *name) { GtkWidget *button=named(pages[id],name);g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button)); }
static void command(GtkWidget *root,const gchar *name) { GtkWidget *item=named(root,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void join(guint source,guint target) { gchar *name=g_strdup_printf("insight-panel-group-with-%u",target);command(pages[source],name);g_free(name); }
static GtkWidget *check_group(void) {
 floating=gtk_widget_get_toplevel(pages[0]);g_assert_true(floating!=window);g_assert_true(floating==gtk_widget_get_toplevel(pages[1]));g_assert_true(floating==gtk_widget_get_toplevel(pages[2]));GtkWidget *book=gtk_widget_get_parent(pages[0]);g_assert_true(GTK_IS_NOTEBOOK(book));g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(book)),==,3);return book;
}
static void drag_exited(GPid pid,gint status,gpointer data) { (void)data;GError *error=NULL;g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error);drag_done=TRUE;g_spawn_close_pid(pid); }
static void widget_center(GtkWidget *widget,gint *x,gint *y) { GtkWidget *top=gtk_widget_get_toplevel(widget);gint left,upper,dx,dy;gdk_window_get_origin(gtk_widget_get_window(top),&left,&upper);g_assert_true(gtk_widget_translate_coordinates(widget,top,gtk_widget_get_allocated_width(widget)/2,gtk_widget_get_allocated_height(widget)/2,&dx,&dy));*x=left+dx;*y=upper+dy; }
static void drag_panel(guint source,guint target) {
 gint sx,sy,dx,dy;widget_center(named(pages[source],"insight-panel-drag-handle"),&sx,&sy);widget_center(named(pages[target],"insight-panel-drag-target"),&dx,&dy);
 g_printerr("Native panel drag %u → %u\n",source,target);
 gchar *x=g_strdup_printf("%d",sx),*y=g_strdup_printf("%d",sy),*mx=g_strdup_printf("%d",sx+20),*my=g_strdup_printf("%d",sy+8),*near=g_strdup_printf("%d",dx-12),*tx=g_strdup_printf("%d",dx),*ty=g_strdup_printf("%d",dy);
 gchar *args[]={"xdotool","mousemove","--sync",x,y,"mousedown","1","sleep","0.2","mousemove","--sync",mx,my,"sleep","0.4","mousemove","--sync",near,ty,"sleep","0.4","mousemove","--sync",tx,ty,"sleep","0.7","mouseup","1",NULL};GPid pid;GError *error=NULL;drag_done=FALSE;g_assert_true(g_spawn_async(NULL,args,NULL,G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&pid,&error));g_assert_no_error(error);g_child_watch_add(pid,drag_exited,NULL);g_free(x);g_free(y);g_free(mx);g_free(my);g_free(near);g_free(tx);g_free(ty);
}
static void prepare_three(void) {
 GtkWidget *book=check_group();gtk_window_resize(GTK_WINDOW(floating),720,600);gtk_notebook_reorder_child(GTK_NOTEBOOK(book),pages[2],0);gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),pages[1]));
 command(window,"insight-panel-transparency-toggle");gtk_scale_button_set_value(GTK_SCALE_BUTTON(named(pages[1],"insight-panel-opacity")),75);for(guint i=0;i<3;i++) g_assert_cmpfloat(gtk_scale_button_get_value(GTK_SCALE_BUTTON(named(pages[i],"insight-panel-opacity"))),==,75);phase=1;
}
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!window) { window=gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));for(guint i=0;i<8;i++) { pages[i]=find_page(i);g_assert_nonnull(pages[i]); }if(restore) phase=10;else if(corrupt) phase=20;return G_SOURCE_CONTINUE; }
 switch(phase) {
 case 0: {
  click(0,"insight-panel-toggle-dock");
  if(g_getenv("GEANY_TEST_DRAG")) { click(1,"insight-panel-toggle-dock");GtkWindow *a=GTK_WINDOW(gtk_widget_get_toplevel(pages[0])),*b=GTK_WINDOW(gtk_widget_get_toplevel(pages[1]));gtk_window_resize(a,500,500);gtk_window_resize(b,500,500);gtk_window_move(a,30,60);gtk_window_move(b,780,60);phase=50;break; }
  join(0,1);click(2,"insight-panel-toggle-dock");join(2,0);prepare_three();break;
 }
 case 50: drag_panel(0,1);phase=51;break;
 case 51: if(!drag_done) return G_SOURCE_CONTINUE;g_assert_true(gtk_widget_get_toplevel(pages[0])==gtk_widget_get_toplevel(pages[1]));click(2,"insight-panel-toggle-dock");gtk_window_move(GTK_WINDOW(gtk_widget_get_toplevel(pages[2])),30,60);phase=52;break;
 case 52: drag_panel(2,0);phase=53;break;
 case 53: {
  if(!drag_done) return G_SOURCE_CONTINUE;
  GtkWidget *book=check_group();gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),pages[0]));gtk_window_move(GTK_WINDOW(floating),30,60);GtkWidget *home=gtk_widget_get_parent(pages[3]);gtk_notebook_set_current_page(GTK_NOTEBOOK(home),gtk_notebook_page_num(GTK_NOTEBOOK(home),pages[3]));phase=54;break;
 }
 case 54: drag_panel(0,3);phase=55;break;
 case 55: if(!drag_done) return G_SOURCE_CONTINUE;g_assert_true(gtk_widget_get_toplevel(pages[0])==gtk_widget_get_toplevel(pages[3]));g_assert_true(gtk_widget_get_toplevel(pages[0])!=gtk_widget_get_toplevel(pages[1]));join(0,1);click(3,"insight-panel-toggle-dock");prepare_three();break;
 case 1: {
  command(window,"insight-layout-save-0");command(pages[1],"insight-panel-ungroup");g_assert_true(gtk_widget_get_toplevel(pages[1])!=gtk_widget_get_toplevel(pages[0]));g_assert_true(gtk_widget_get_toplevel(pages[0])==gtk_widget_get_toplevel(pages[2]));
  click(0,"insight-panel-toggle-dock");g_assert_true(gtk_widget_get_toplevel(pages[0])==window);g_assert_true(GTK_IS_WINDOW(gtk_widget_get_toplevel(pages[2])));g_assert_true(gtk_widget_get_toplevel(pages[2])!=window);
  GtkWidget *remaining=gtk_widget_get_toplevel(pages[2]);GdkEvent *event=gdk_event_new(GDK_DELETE);gboolean handled=FALSE;g_signal_emit_by_name(remaining,"delete-event",event,&handled);gdk_event_free(event);g_assert_true(handled);g_assert_true(gtk_widget_get_toplevel(pages[2])==window);
  command(window,"insight-layout-load-0");phase++;break;
 }
 case 2: {
  GtkWidget *book=check_group();g_assert_true(gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),0)==pages[2]);g_assert_true(gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),gtk_notebook_get_current_page(GTK_NOTEBOOK(book)))==pages[1]);
  click(1,"insight-panel-minimize");g_assert_false(gtk_widget_get_visible(floating));GtkWidget *tab=named(window,"insight-panel-tray-0");g_assert_nonnull(tab);g_assert_true(gtk_widget_get_visible(tab));g_assert_nonnull(strstr(gtk_button_get_label(GTK_BUTTON(tab)),"+2"));
  command(window,"insight-layout-save-1");command(window,"insight-layout-load-0");g_assert_true(gtk_widget_get_visible(gtk_widget_get_toplevel(pages[0])));command(window,"insight-layout-load-1");phase++;break;
 }
 case 3: {
  check_group();g_assert_false(gtk_widget_get_visible(floating));gchar *path=g_build_filename(profile,"insight","workbench.conf",NULL);GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL));g_free(path);for(guint i=0;i<3;i++) { gchar *name=g_strdup_printf("panel-%u-group",i);g_assert_cmpint(g_key_file_get_integer(key,"layout-B",name,NULL),==,1);g_free(name); }g_key_file_unref(key);
  gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 case 20: {
  for(guint i=0;i<3;i++) { GtkWidget *owner=gtk_widget_get_toplevel(pages[i]);g_assert_true(owner!=window);g_assert_false(gtk_widget_get_visible(owner));for(guint j=0;j<i;j++) g_assert_true(owner!=gtk_widget_get_toplevel(pages[j]));gchar *name=g_strdup_printf("insight-panel-tray-%u",i);g_assert_true(gtk_widget_get_visible(named(window,name)));g_free(name); }
  command(window,"insight-panel-restore-all");for(guint i=0;i<3;i++) g_assert_true(gtk_widget_get_visible(gtk_widget_get_toplevel(pages[i])));gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 case 10: check_group();g_assert_false(gtk_widget_get_visible(floating));gtk_button_clicked(GTK_BUTTON(named(window,"insight-panel-tray-0")));phase++;break;
 case 11: {
  GtkWidget *book=check_group();g_assert_true(gtk_widget_get_visible(floating));g_assert_true(gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),0)==pages[2]);g_assert_true(gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),gtk_notebook_get_current_page(GTK_NOTEBOOK(book)))==pages[1]);g_assert_cmpfloat(gtk_widget_get_opacity(floating),>,0.74);g_assert_cmpfloat(gtk_widget_get_opacity(floating),<,0.76);
  const gchar *capture=g_getenv("GEANY_GROUP_CAPTURE");if(capture) { GdkWindow *native=gtk_widget_get_window(floating);GdkPixbuf *image=gdk_pixbuf_get_from_window(native,0,0,gdk_window_get_width(native),MIN(250,gdk_window_get_height(native)));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  GdkEvent *event=gdk_event_new(GDK_DELETE);gboolean handled=FALSE;g_signal_emit_by_name(floating,"delete-event",event,&handled);gdk_event_free(event);g_assert_true(handled);for(guint i=0;i<3;i++) g_assert_true(gtk_widget_get_toplevel(pages[i])==window);
  gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 default:g_assert_not_reached();
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Panel group GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 if(g_getenv("GEANY_TEST_DRAG")) { gchar *driver=g_find_program_in_path("xdotool");if(!driver) { g_print("Physical panel drag test requires xdotool\n");return 77; }g_free(driver); }
 if(argc==1) {
  gchar *dir=g_dir_make_tmp("panel-groups-XXXXXX",NULL);g_assert_nonnull(dir);gchar *config=g_build_filename(dir,"geany.conf",NULL),*settings=g_build_filename(dir,"insight","workbench.conf",NULL),*snapshot=NULL;g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
  for(guint i=0;i<3;i++) {
   if(i==2) { GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_data(key,snapshot,-1,G_KEY_FILE_NONE,NULL));g_key_file_set_integer(key,"panel-1","group",8);g_assert_true(g_key_file_save_to_file(key,settings,NULL));g_key_file_unref(key); }
   gchar *child[]={argv[0],dir,i==0 ? "fresh" : i==1 ? "restore" : "corrupt",NULL};gint status;GError *error=NULL;g_assert_true(g_spawn_sync(NULL,child,NULL,0,NULL,NULL,NULL,NULL,&status,&error));g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error);
   if(!i) g_assert_true(g_file_get_contents(settings,&snapshot,NULL,NULL));
  }
  g_free(snapshot);g_free(settings);g_free(config);g_free(dir);g_print("Panel groups GUI: join, separate, partial dock, close, shared opacity/tray, layout order/active tab, restart and corrupted-session fallback passed\n");return 0;
 }
 profile=argv[1];restore=!g_strcmp0(argv[2],"restore");corrupt=!g_strcmp0(argv[2],"corrupt");gchar *args[]={"panel-groups-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(400,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);return main_lib(5,args);
}
