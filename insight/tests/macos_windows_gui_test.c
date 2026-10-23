/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GeanyDocument *doc;
static GtkWidget *window,*options[4],*left,*context,*project,*icon;
static ScintillaObject *map;
static guint phase,ticks;static gint original_size;static gboolean restoring;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *c=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL;
}
static GtkWidget *any(const gchar *name) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *f=NULL;for(GList *p=windows;p && !f;p=p->next) f=named(p->data,name);g_list_free(windows);return f;
}
static void unchanged(void) {
 gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,"int main(void) { return 0; }\n");g_free(text);
 g_assert_false(doc->changed);g_assert_false(SEND(doc->editor->sci,SCI_CANUNDO,0,0));
 g_assert_cmpint(SEND(doc->editor->sci,SCI_STYLEGETSIZEFRACTIONAL,STYLE_DEFAULT,0),==,original_size);
 g_assert_true(gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci))==window);
}
static void miniature(guint percent) {
 g_assert_true(SEND(map,SCI_GETREADONLY,0,0));g_assert_cmpint(SEND(map,SCI_GETZOOM,0,0),==,0);
 for(guint i=0;i<=STYLE_MAX;i++) g_assert_cmpint(SEND(map,SCI_STYLEGETSIZEFRACTIONAL,i,0),==,MAX(100,SEND(doc->editor->sci,SCI_STYLEGETSIZEFRACTIONAL,i,0)*percent/100));
 unchanged();
}
static void choose(guint which) { gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(options[which]),TRUE); }
static gboolean step(gpointer unused) {
 (void)unused;if(++ticks>160) g_error("macOS windows timeout phase %u",phase);
 if(!doc) doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(phase==0) {
  if(ticks<8 || !any("insight-appearance-0-3")) return G_SOURCE_CONTINUE;
  for(guint i=0;i<4;i++) { gchar *name=g_strdup_printf("insight-appearance-0-%u",i);options[i]=any(name);g_free(name);g_assert_nonnull(options[i]); }
  left=any("insight-local-symbols");context=any("insight-context");project=any("insight-project-files");g_assert_nonnull(left);g_assert_nonnull(context);g_assert_nonnull(project);
  map=SCINTILLA(any("insight-overview-code"));g_assert_nonnull(map);original_size=SEND(doc->editor->sci,SCI_STYLEGETSIZEFRACTIONAL,STYLE_DEFAULT,0);
  icon=gtk_image_new_from_icon_name("document-open",GTK_ICON_SIZE_MENU);GtkToolItem *item=gtk_tool_button_new(icon,NULL);gtk_toolbar_insert(GTK_TOOLBAR(main_widgets.toolbar),item,-1);gtk_widget_show_all(GTK_WIDGET(item));
  phase=restoring ? 5:1;return G_SOURCE_CONTINUE;
 }
 if(phase==1) { miniature(10);SEND(map,SCI_STYLESETSIZEFRACTIONAL,0,2200);SEND(map,SCI_STYLESETSIZEFRACTIONAL,STYLE_DEFAULT,2200);SEND(map,SCI_SETZOOM,8,0);gtk_widget_queue_draw(GTK_WIDGET(doc->editor->sci));phase++;return G_SOURCE_CONTINUE; }
 if(phase==2) {
  miniature(10);GdkEventScroll event={0};event.type=GDK_SCROLL;event.window=gtk_widget_get_window(GTK_WIDGET(map));event.direction=GDK_SCROLL_UP;event.state=GDK_CONTROL_MASK;gboolean handled=FALSE;
  g_signal_emit_by_name(map,"scroll-event",&event,&handled);g_assert_true(handled);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==3) { miniature(15);choose(1);phase++;return G_SOURCE_CONTINUE; }
 if(phase==4) {
  const gchar *name=NULL;GtkIconSize size;gtk_image_get_icon_name(GTK_IMAGE(icon),&name,&size);
  if(gtk_icon_theme_has_icon(gtk_icon_theme_get_default(),"document-open-symbolic")) g_assert_cmpstr(name,==,"document-open-symbolic");
  GtkBorder padding;gtk_style_context_get_padding(gtk_widget_get_style_context(options[0]),GTK_STATE_FLAG_NORMAL,&padding);g_assert_cmpint(padding.top,<=,2);g_assert_cmpint(padding.bottom,<=,2);
  choose(3);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==5) {
  GtkWidget *tools=any("insight-tools-palette");g_assert_nonnull(tools);g_assert_true(gtk_widget_get_toplevel(ui_lookup_widget(window,"menubar1"))==tools);
  GtkWidget *l=gtk_widget_get_toplevel(left),*r=gtk_widget_get_toplevel(project),*c=gtk_widget_get_toplevel(context);
  g_assert_true(l!=window && r!=window && c!=window && l!=r && l!=c && r!=c);
  g_assert_true(GTK_IS_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(tools))));g_assert_true(GTK_IS_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(l))));unchanged();
  const gchar *capture=g_getenv("INSIGHT_MACOS_WINDOWS_CAPTURE");
  if(capture && !restoring) { GdkWindow *root=gdk_get_default_root_window();GdkPixbuf *pix=gdk_pixbuf_get_from_window(root,0,0,gdk_window_get_width(root),gdk_window_get_height(root));g_assert_nonnull(pix);g_assert_true(gdk_pixbuf_save(pix,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(pix); }
  choose(1);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==6) {
  g_assert_null(any("insight-tools-palette"));g_assert_true(gtk_widget_get_toplevel(left)==window);g_assert_true(gtk_widget_get_toplevel(context)==window);g_assert_true(gtk_widget_get_toplevel(project)==window);unchanged();
  choose(3);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==7) { GtkWidget *tools=any("insight-tools-palette");g_assert_nonnull(tools);gtk_window_close(GTK_WINDOW(tools));phase++;return G_SOURCE_CONTINUE; }
 if(phase==8) { if(any("insight-tools-palette")) return G_SOURCE_CONTINUE;g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(options[1])));choose(0);phase++;return G_SOURCE_CONTINUE; }
 if(phase==9) {
  const gchar *name=NULL;GtkIconSize size;gtk_image_get_icon_name(GTK_IMAGE(icon),&name,&size);g_assert_cmpstr(name,==,"document-open");unchanged();
  g_print("Independent macOS windows, compact menus, symbolic icon restoration, CSD, join/Close, minimap font/zoom recovery and Ctrl-wheel passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 restoring=g_getenv("GEANY_TEST_INDEPENDENT_RESTORE")!=NULL;
 gchar *dir=g_dir_make_tmp("macos-windows-XXXXXX",NULL),*folder=g_build_filename(dir,"insight",NULL),*config=g_build_filename(dir,"geany.conf",NULL),*source=g_build_filename(dir,"source.c",NULL);
 g_assert_cmpint(g_mkdir_with_parents(folder,0700),==,0);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(source,"int main(void) { return 0; }\n",-1,NULL));
 gchar *workbench=g_build_filename(folder,"workbench.conf",NULL);g_assert_true(g_file_set_contents(workbench,"[workbench]\noverview-visibility-policy=1\noverview-visible=true\noverview-scale=10\n",-1,NULL));
 if(restoring) { gchar *style=g_build_filename(folder,"interface-style.conf",NULL);g_assert_true(g_file_set_contents(style,"[interface]\nversion=1\nstyle=macos-windows\n",-1,NULL));g_free(style); }
 gchar *args[]={"macos-windows","--new-instance","--no-session","--config",dir,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(250,step,NULL);return main_lib(6,args);
}
