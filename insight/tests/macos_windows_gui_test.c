/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#include <math.h>
#ifdef G_OS_UNIX
#include <unistd.h>
#endif
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GeanyDocument *doc,*other;
static gint other_first,other_x;
static GtkWidget *window,*options[4],*left,*context,*project,*icon;
static ScintillaObject *map;
static guint phase,ticks;static gboolean saw_motion,saw_alpha;static gdouble last_alpha=.7;static gint original_size;static gboolean restoring;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *c=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL;
}
static GtkWidget *any(const gchar *name) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *f=NULL;for(GList *p=windows;p && !f;p=p->next) f=named(p->data,name);g_list_free(windows);return f;
}
static gboolean moving(void) { return g_object_get_data(G_OBJECT(window),"insight-independent-motion")!=NULL; }
static GdkRectangle frame(GtkWidget *widget) {
 GdkWindow *native=gtk_widget_get_window(widget);GdkRectangle rect;gdk_window_get_frame_extents(native,&rect);
 GtkWidget *header=gtk_window_get_titlebar(GTK_WINDOW(widget)),*body=gtk_bin_get_child(GTK_BIN(widget));
 if(header && body) {
  gint hx,hy,bx,by,ox,oy,x,y;gdk_window_get_origin(native,&ox,&oy);gtk_window_get_position(GTK_WINDOW(widget),&x,&y);
  g_assert_true(gtk_widget_translate_coordinates(header,widget,0,0,&hx,&hy));g_assert_true(gtk_widget_translate_coordinates(body,widget,0,0,&bx,&by));
  gint border=MAX(0,ox+hx-x);rect=(GdkRectangle){ox+hx-border,oy+hy-border,gtk_widget_get_allocated_width(header)+2*border,oy+by+gtk_widget_get_allocated_height(body)+border-y};
 }
 return rect;
}
static void edge_positions(void) {
 GdkMonitor *monitor=gdk_display_get_monitor_at_window(gtk_widget_get_display(window),gtk_widget_get_window(window));GdkRectangle area;gdk_monitor_get_workarea(monitor,&area);GtkWidget *tools=any("insight-tools-palette");g_assert_nonnull(tools);GdkRectangle t=frame(tools),l=frame(gtk_widget_get_toplevel(left)),r=frame(gtk_widget_get_toplevel(project)),b=frame(gtk_widget_get_toplevel(context)),e=frame(window);GdkRectangle all[]={t,l,r,b,e};
 for(guint n=0;n<G_N_ELEMENTS(all);n++) {g_assert_cmpint(all[n].x,>=,area.x-3);g_assert_cmpint(all[n].y,>=,area.y-3);g_assert_cmpint(all[n].x+all[n].width,<=,area.x+area.width+3);g_assert_cmpint(all[n].y+all[n].height,<=,area.y+area.height+3);}
 g_assert_cmpint(abs(t.y-area.y),<=,3);g_assert_cmpint(abs(2*t.x+t.width-2*area.x-area.width),<=,4);g_assert_cmpint(abs(l.x-area.x),<=,3);g_assert_cmpint(abs(2*l.y+l.height-2*area.y-area.height),<=,4);g_assert_cmpint(abs(r.x+r.width-area.x-area.width),<=,3);g_assert_cmpint(abs(2*r.y+r.height-2*area.y-area.height),<=,4);g_assert_cmpint(abs(b.y+b.height-area.y-area.height),<=,3);g_assert_cmpint(abs(2*b.x+b.width-2*area.x-area.width),<=,4);g_assert_cmpint(abs(2*e.x+e.width-2*area.x-area.width),<=,4);g_assert_cmpint(abs(2*e.y+e.height-2*area.y-area.height),<=,4);
 GtkWidget *split=ui_lookup_widget(window,"vpaned1");g_assert_null(gtk_paned_get_child2(GTK_PANED(split)));
 g_assert_true(gtk_widget_get_toplevel(any("insight-message-height-host"))==gtk_widget_get_toplevel(context));
 /* Moving the abandoned source divider must never hide detached Context. */
 gtk_paned_set_position(GTK_PANED(split),gtk_widget_get_allocated_height(split));
 g_assert_true(gtk_widget_get_visible(ui_lookup_widget(window,"notebook_info")));
 GtkWidget *terminals=any("insight-terminal-group");g_assert_nonnull(terminals); g_assert_true(gtk_widget_get_toplevel(terminals)==gtk_widget_get_toplevel(context));
 for(guint n=0;n<5;n++) {GtkWidget *owner=n==0 ? tools:n==1 ? gtk_widget_get_toplevel(left):n==2 ? gtk_widget_get_toplevel(project):n==3 ? gtk_widget_get_toplevel(context):window;g_assert_cmpfloat(gtk_widget_get_opacity(owner),>,.995);if(owner!=window) {g_assert_null(gtk_window_get_transient_for(GTK_WINDOW(owner)));GtkWidget *header=gtk_window_get_titlebar(GTK_WINDOW(owner));g_assert_true(GTK_IS_HEADER_BAR(header));g_assert_false(gtk_header_bar_get_has_subtitle(GTK_HEADER_BAR(header)));g_assert_cmpint(gtk_widget_get_allocated_height(header),<=,24);}}
}
static void unchanged(void) {
 gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,"int main(void) { return 0; }\n");g_free(text);
 if(other) {g_assert_cmpint(SEND(other->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0),==,other_first);g_assert_cmpint(SEND(other->editor->sci,SCI_GETXOFFSET,0,0),==,other_x);g_assert_cmpint(SEND(other->editor->sci,SCI_GETCURRENTPOS,0,0),==,0);}
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
 (void)unused;static guint last=G_MAXUINT;if(last!=phase) { g_printerr("macOS window phase %u\n",phase);last=phase; }
 if(++ticks>320) g_error("macOS windows timeout phase %u",phase);
 if(!doc) doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(phase==0) {
  if(ticks<8 || !any("insight-appearance-0-3")) return G_SOURCE_CONTINUE;
  for(guint i=0;i<4;i++) {if(i==2) continue;gchar *name=g_strdup_printf("insight-appearance-0-%u",i);options[i]=any(name);g_free(name);g_assert_nonnull(options[i]); }
  left=any("insight-local-symbols");context=any("insight-context");project=any("insight-project-files");g_assert_nonnull(left);g_assert_nonnull(context);g_assert_nonnull(project);
  GList *items=gtk_container_get_children(GTK_CONTAINER(ui_lookup_widget(window,"menu_view1_menu")));gboolean found=FALSE;
  for(GList *p=items;p;p=p->next) if(GTK_IS_CHECK_MENU_ITEM(p->data) && !g_strcmp0(gtk_menu_item_get_label(GTK_MENU_ITEM(p->data)),"Overview")) { gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(p->data),TRUE);found=TRUE;break; }
  g_list_free(items);g_assert_true(found);
  map=SCINTILLA(any("insight-overview-code"));g_assert_nonnull(map);original_size=SEND(doc->editor->sci,SCI_STYLEGETSIZEFRACTIONAL,STYLE_DEFAULT,0);
  icon=gtk_image_new_from_icon_name("document-open",GTK_ICON_SIZE_MENU);GtkToolItem *item=gtk_tool_button_new(icon,NULL);GtkWidget *toolbar=gtk_widget_get_ancestor(any("insight-find-tool"),GTK_TYPE_TOOLBAR);g_assert_true(GTK_IS_TOOLBAR(toolbar));gtk_toolbar_insert(GTK_TOOLBAR(toolbar),item,-1);gtk_widget_show_all(GTK_WIDGET(item));
  phase=restoring ? 5:1;return G_SOURCE_CONTINUE;
 }
 if(phase==1) { miniature(10);SEND(map,SCI_STYLESETSIZEFRACTIONAL,0,2200);SEND(map,SCI_STYLESETSIZEFRACTIONAL,STYLE_DEFAULT,2200);SEND(map,SCI_SETZOOM,8,0);gtk_widget_queue_draw(GTK_WIDGET(doc->editor->sci));phase++;return G_SOURCE_CONTINUE; }
 if(phase==2) {
  miniature(10);GdkEventScroll event={0};event.type=GDK_SCROLL;event.window=gtk_widget_get_window(GTK_WIDGET(map));event.direction=GDK_SCROLL_UP;event.state=GDK_CONTROL_MASK;gboolean handled=FALSE;
  g_signal_emit_by_name(map,"scroll-event",&event,&handled);g_assert_true(handled);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==3) {
  miniature(15);GString *text=g_string_new(NULL);for(guint n=0;n<700;n++) g_string_append(text,"// A deliberately long source line to exercise both horizontal and vertical viewport preservation during window layout transitions.\n");
  other=document_new_file(NULL,NULL,text->str);g_string_free(text,TRUE);SEND(other->editor->sci,SCI_SETSAVEPOINT,0,0);SEND(other->editor->sci,SCI_SETWRAPMODE,SC_WRAP_NONE,0);SEND(other->editor->sci,SCI_GOTOPOS,0,0);
  SEND(other->editor->sci,SCI_SETFIRSTVISIBLELINE,240,0);SEND(other->editor->sci,SCI_SETXOFFSET,96,0);gtk_notebook_set_current_page(GTK_NOTEBOOK(ui_lookup_widget(window,"notebook1")),document_get_notebook_page(doc));choose(1);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==4) {
  const gchar *name=NULL;GtkIconSize size;gtk_image_get_icon_name(GTK_IMAGE(icon),&name,&size);
  if(gtk_icon_theme_has_icon(gtk_icon_theme_get_default(),"document-open-symbolic")) g_assert_cmpstr(name,==,"document-open-symbolic");
  GtkBorder padding;gtk_style_context_get_padding(gtk_widget_get_style_context(options[0]),GTK_STATE_FLAG_NORMAL,&padding);g_assert_cmpint(padding.top,<=,2);g_assert_cmpint(padding.bottom,<=,2);
  other_first=SEND(other->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0);other_x=SEND(other->editor->sci,SCI_GETXOFFSET,0,0);g_assert_cmpint(other_first,>,0);
  GtkWidget *opacity=any("insight-opacity-0-70");g_assert_true(GTK_IS_RADIO_MENU_ITEM(opacity));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(opacity),TRUE);g_assert_cmpfloat_with_epsilon(gtk_widget_get_opacity(window),.7,.006);last_alpha=gtk_widget_get_opacity(window);choose(3);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==5) {
  if(moving()) {saw_motion=TRUE;gdouble alpha=gtk_widget_get_opacity(window);g_assert_cmpfloat(alpha+.005,>=,last_alpha);if(alpha<.995) saw_alpha=TRUE;last_alpha=alpha;return G_SOURCE_CONTINUE;}edge_positions();if(!restoring) {g_assert_true(saw_motion);g_assert_true(saw_alpha);}g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(any("insight-opacity-0-100"))));
  GtkWidget *tools=any("insight-tools-palette");g_assert_nonnull(tools);g_assert_true(gtk_widget_get_toplevel(ui_lookup_widget(window,"menubar1"))==tools);
  GtkWidget *l=gtk_widget_get_toplevel(left),*r=gtk_widget_get_toplevel(project),*c=gtk_widget_get_toplevel(context);
  g_assert_true(l!=window && r!=window && c!=window && l!=r && l!=c && r!=c);
  if(other) {gtk_notebook_set_current_page(GTK_NOTEBOOK(ui_lookup_widget(window,"notebook1")),document_get_notebook_page(other));gtk_notebook_set_current_page(GTK_NOTEBOOK(ui_lookup_widget(window,"notebook1")),document_get_notebook_page(doc));g_assert_null(gtk_paned_get_child2(GTK_PANED(ui_lookup_widget(window,"vpaned1"))));g_assert_true(gtk_widget_get_visible(ui_lookup_widget(window,"notebook_info")));}
  gtk_window_set_title(GTK_WINDOW(tools),"Updated palette title");
  g_assert_cmpstr(gtk_header_bar_get_title(GTK_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(tools)))),==,"Updated palette title");
  g_assert_true(GTK_IS_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(tools))));g_assert_true(GTK_IS_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(l))));unchanged();
  const gchar *capture=g_getenv("INSIGHT_MACOS_WINDOWS_CAPTURE");
  if(capture && !restoring) { GdkWindow *root=gdk_get_default_root_window();GdkPixbuf *pix=gdk_pixbuf_get_from_window(root,0,0,gdk_window_get_width(root),gdk_window_get_height(root));g_assert_nonnull(pix);g_assert_true(gdk_pixbuf_save(pix,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(pix); }
  choose(1);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==6) {
  if(moving()) return G_SOURCE_CONTINUE;
  g_assert_null(any("insight-tools-palette"));g_assert_true(gtk_widget_get_visible(any("insight-message-height-host"))) ;g_assert_true(gtk_widget_get_toplevel(left)==window);g_assert_true(gtk_widget_get_toplevel(context)==window);g_assert_true(gtk_widget_get_toplevel(project)==window);unchanged();
  choose(3);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==7) { if(moving()) return G_SOURCE_CONTINUE;edge_positions();GtkWidget *tools=any("insight-tools-palette");g_assert_nonnull(tools);gtk_window_close(GTK_WINDOW(tools));phase++;return G_SOURCE_CONTINUE; }
 if(phase==8) { if(moving()) return G_SOURCE_CONTINUE;if(any("insight-tools-palette")) return G_SOURCE_CONTINUE;g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(options[1])));choose(0);phase++;return G_SOURCE_CONTINUE; }
 if(phase==9) {
  const gchar *name=NULL;GtkIconSize size;gtk_image_get_icon_name(GTK_IMAGE(icon),&name,&size);g_assert_cmpstr(name,==,"document-open");unchanged();
  choose(3);phase=10;return G_SOURCE_CONTINUE;
 }
 if(phase==10) {g_assert_true(moving());choose(1);choose(3);phase++;return G_SOURCE_CONTINUE;}
 if(phase==11) {if(moving()) return G_SOURCE_CONTINUE;edge_positions();choose(1);phase++;return G_SOURCE_CONTINUE;}
 if(phase==12) {if(moving()) return G_SOURCE_CONTINUE;g_assert_null(any("insight-tools-palette"));unchanged();
  g_print("Workarea anchors, constant-speed split/join, reversible transitions, grouped Context/terminals, native opacity fade and independent macOS windows, compact menus, symbolic icon restoration, CSD, join/Close, minimap font/zoom recovery and Ctrl-wheel passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 #ifdef G_OS_UNIX
 alarm(70);
 #endif
 restoring=g_getenv("GEANY_TEST_INDEPENDENT_RESTORE")!=NULL;
 gchar *dir=g_dir_make_tmp("macos-windows-XXXXXX",NULL),*folder=g_build_filename(dir,"insight",NULL),*config=g_build_filename(dir,"geany.conf",NULL),*source=g_build_filename(dir,"source.c",NULL);
 g_assert_cmpint(g_mkdir_with_parents(folder,0700),==,0);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(source,"int main(void) { return 0; }\n",-1,NULL));
 if(restoring) { gchar *style=g_build_filename(folder,"interface-style.conf",NULL);g_assert_true(g_file_set_contents(style,"[interface]\nversion=1\nstyle=macos-windows\n",-1,NULL));g_free(style); }
 gchar *args[]={"macos-windows","--new-instance","--no-session","--config",dir,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(250,step,NULL);return main_lib(6,args);
}
