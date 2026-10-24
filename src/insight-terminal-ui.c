/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-terminal-ui.h"
#include "main.h"
#include "ui_utils.h"
#include "msgwindow.h"
#include "insight-i18n.h"
#include "vte.h"
#include <string.h>
static GtkWidget *group,*group_root;
static void group_destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;group=NULL;group_root=NULL; }
/* Status is an overlay over the unused header area: it never participates in
 * notebook measurement and its own viewport clips only the right-hand end. */
static gboolean status_position(GtkOverlay *overlay,GtkWidget *child,GdkRectangle *allocation,gpointer data) {
 (void)data;if(!group) return FALSE;GtkWidget *label=g_object_get_data(G_OBJECT(child),"terminal-status-label");if(!label) return FALSE;gint left=0,top=0,height=1,needed=0;
 for(gint i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(group));i++) { GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(group),i),*tab=gtk_notebook_get_tab_label(GTK_NOTEBOOK(group),page);gint natural=0,x,y;gtk_widget_get_preferred_width(tab,NULL,&natural);needed+=natural+24;if(gtk_widget_translate_coordinates(tab,GTK_WIDGET(overlay),0,0,&x,&y)) { left=MAX(left,x+gtk_widget_get_allocated_width(tab)+16);top=y;height=MAX(height,gtk_widget_get_allocated_height(tab)); } }
 gint width=gtk_widget_get_allocated_width(GTK_WIDGET(overlay)),text_width=0;pango_layout_get_pixel_size(gtk_label_get_layout(GTK_LABEL(label)),&text_width,NULL);if(width<needed+24) left=width;gint free_width=MAX(0,width-left-4);allocation->width=MAX(1,MIN(text_width,free_width));allocation->x=free_width ? MAX(left,width-text_width-4):width;allocation->y=MAX(0,top-2);allocation->height=height+4;return TRUE;
}
static void status_switch(GtkNotebook *book,GtkWidget *page,guint number,gpointer data) {
 (void)book;(void)number;(void)data;if(!group_root || !group) return;GList *children=gtk_container_get_children(GTK_CONTAINER(group_root));for(GList *p=children;p;p=p->next) { GtkWidget *owner=g_object_get_data(G_OBJECT(p->data),"terminal-status-page");if(owner) gtk_widget_set_visible(p->data,owner==page); }g_list_free(children);gtk_widget_queue_resize(group_root);
}
static void status_changed(GObject *label,GParamSpec *spec,gpointer data) { (void)spec;gtk_widget_set_tooltip_text(GTK_WIDGET(data),gtk_label_get_text(GTK_LABEL(label)));GtkWidget *page=g_object_get_data(G_OBJECT(data),"terminal-status-page");if(group && page) { GtkWidget *tab=gtk_notebook_get_tab_label(GTK_NOTEBOOK(group),page);if(tab) gtk_widget_set_tooltip_text(tab,gtk_label_get_text(GTK_LABEL(label))); }if(group_root) gtk_widget_queue_resize(group_root); }
void insight_terminal_ui_status(GtkWidget *page,GtkWidget *label) {
 g_return_if_fail(group_root!=NULL);GtkWidget *clip=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_name(clip,"insight-terminal-status-clip");gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(clip),GTK_POLICY_EXTERNAL,GTK_POLICY_EXTERNAL);gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(clip),GTK_SHADOW_NONE);gtk_widget_set_size_request(clip,0,0);gtk_widget_set_no_show_all(clip,TRUE);gtk_label_set_line_wrap(GTK_LABEL(label),FALSE);gtk_label_set_single_line_mode(GTK_LABEL(label),TRUE);gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_NONE);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_container_add(GTK_CONTAINER(clip),label);g_object_set_data(G_OBJECT(clip),"terminal-status-page",page);g_object_set_data(G_OBJECT(clip),"terminal-status-label",label);gtk_overlay_add_overlay(GTK_OVERLAY(group_root),clip);g_signal_connect_object(label,"notify::label",G_CALLBACK(status_changed),clip,0);g_signal_connect_object(page,"destroy",G_CALLBACK(gtk_widget_destroy),clip,G_CONNECT_SWAPPED);gtk_widget_show(label);gtk_widget_show(gtk_widget_get_parent(label));status_changed(G_OBJECT(label),NULL,clip);GtkWidget *active=gtk_notebook_get_nth_page(GTK_NOTEBOOK(group),gtk_notebook_get_current_page(GTK_NOTEBOOK(group)));status_switch(GTK_NOTEBOOK(group),active,0,NULL);
}
static void ensure_group(void) {
 if(group) return;GtkWidget *outer=main_widgets.message_window_notebook,*local=NULL,*label=NULL;gint position=-1;
#ifdef HAVE_VTE
 if(vte_info.have_vte && vte_config.vte) { GtkWidget *child=vte_config.vte;while(gtk_widget_get_parent(child) && gtk_widget_get_parent(child)!=outer) child=gtk_widget_get_parent(child);if(gtk_widget_get_parent(child)==outer) { local=child;position=gtk_notebook_page_num(GTK_NOTEBOOK(outer),local);label=gtk_notebook_get_tab_label(GTK_NOTEBOOK(outer),local);g_object_ref(local);if(label) g_object_ref(label);gtk_notebook_remove_page(GTK_NOTEBOOK(outer),position); } }
#endif
 group=gtk_notebook_new();gtk_widget_set_name(group,"insight-terminal-group");gtk_notebook_set_scrollable(GTK_NOTEBOOK(group),TRUE);group_root=gtk_overlay_new();gtk_container_add(GTK_CONTAINER(group_root),group);gtk_notebook_insert_page(GTK_NOTEBOOK(outer),group_root,gtk_label_new(insight_tr("Terminals")),position);g_object_set_data_full(G_OBJECT(group_root),"insight-help-topic",g_strdup("panel-terminal.md"),g_free);g_signal_connect(group_root,"get-child-position",G_CALLBACK(status_position),NULL);g_signal_connect(group,"switch-page",G_CALLBACK(status_switch),NULL);g_signal_connect(group,"destroy",G_CALLBACK(group_destroyed),NULL);
 if(local) { gtk_notebook_append_page(GTK_NOTEBOOK(group),local,label ? label:gtk_label_new(insight_tr("Local")));if(label) { if(GTK_IS_LABEL(label)) gtk_label_set_text(GTK_LABEL(label),insight_tr("Local"));g_object_unref(label); }g_object_unref(local); }gtk_widget_show_all(group_root);
}
void insight_terminal_ui_add(GtkWidget *page,const gchar *label) { ensure_group();gtk_notebook_append_page(GTK_NOTEBOOK(group),page,gtk_label_new(insight_tr(label)));gtk_widget_show_all(page); }
void insight_terminal_ui_reveal(GtkWidget *page) {
 if(!page) return;msgwin_show_hide(TRUE);GtkWidget *child=page;
 while(gtk_widget_get_parent(child)) { GtkWidget *parent=gtk_widget_get_parent(child);if(GTK_IS_NOTEBOOK(parent)) gtk_notebook_set_current_page(GTK_NOTEBOOK(parent),gtk_notebook_page_num(GTK_NOTEBOOK(parent),child));child=parent; }
}
typedef struct { GtkWidget *overlay,*button;GCallback callback;gpointer data;gboolean dragging,moved;gdouble x,y,start_x,start_y,press_x,press_y; } Overlay;
static void position(GtkWidget *widget,GtkAllocation *allocation,gpointer data) { (void)widget;Overlay *o=data;gint width=MAX(0,allocation->width-gtk_widget_get_allocated_width(o->button)),height=MAX(0,allocation->height-gtk_widget_get_allocated_height(o->button));gint x=o->x*width,y=o->y*height;if(gtk_widget_get_margin_start(o->button)!=x) gtk_widget_set_margin_start(o->button,x);if(gtk_widget_get_margin_top(o->button)!=y) gtk_widget_set_margin_top(o->button,y); }
static gboolean press(GtkWidget *widget,GdkEventButton *event,gpointer data) { (void)widget;Overlay *o=data;if(event->button!=1) return FALSE;o->dragging=TRUE;o->moved=FALSE;o->press_x=event->x_root;o->press_y=event->y_root;o->start_x=gtk_widget_get_margin_start(o->button);o->start_y=gtk_widget_get_margin_top(o->button);gtk_grab_add(o->button);return TRUE; }
static gboolean motion(GtkWidget *widget,GdkEventMotion *event,gpointer data) { (void)widget;Overlay *o=data;if(!o->dragging) return FALSE;gdouble dx=event->x_root-o->press_x,dy=event->y_root-o->press_y;if(dx*dx+dy*dy<25 && !o->moved) return TRUE;o->moved=TRUE;gint width=MAX(1,gtk_widget_get_allocated_width(o->overlay)-gtk_widget_get_allocated_width(o->button)),height=MAX(1,gtk_widget_get_allocated_height(o->overlay)-gtk_widget_get_allocated_height(o->button));o->x=CLAMP((o->start_x+dx)/width,0,1);o->y=CLAMP((o->start_y+dy)/height,0,1);GtkAllocation allocation;gtk_widget_get_allocation(o->overlay,&allocation);position(o->overlay,&allocation,o);return TRUE; }
static void clicked(GtkButton *button,gpointer data) { Overlay *o=data;((void (*)(GtkWidget *,gpointer))o->callback)(GTK_WIDGET(button),o->data); }
static gboolean release(GtkWidget *widget,GdkEventButton *event,gpointer data) { Overlay *o=data;if(event->button!=1 || !o->dragging) return FALSE;o->dragging=FALSE;gtk_grab_remove(widget);if(!o->moved) clicked(GTK_BUTTON(widget),o);return TRUE; }
static void overlay_destroyed(GtkWidget *widget,gpointer data) { (void)widget;Overlay *o=data;if(o->dragging) { gtk_grab_remove(o->button);o->dragging=FALSE; } }
GtkWidget *insight_terminal_ui_overlay(GtkWidget *content,const gchar *name,GCallback callback,gpointer data) {
 Overlay *o=g_new0(Overlay,1);o->overlay=gtk_overlay_new();o->x=1;o->callback=callback;o->data=data;gtk_container_add(GTK_CONTAINER(o->overlay),content);o->button=gtk_button_new_with_label("Ai");gtk_widget_set_name(o->button,name);gtk_widget_set_opacity(o->button,0.8);gtk_widget_set_tooltip_text(o->button,insight_tr("AI assistant — drag to move"));gtk_widget_set_halign(o->button,GTK_ALIGN_START);gtk_widget_set_valign(o->button,GTK_ALIGN_START);gtk_widget_add_events(o->button,GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK);gtk_overlay_add_overlay(GTK_OVERLAY(o->overlay),o->button);g_signal_connect(o->button,"clicked",G_CALLBACK(clicked),o);g_signal_connect(o->button,"button-press-event",G_CALLBACK(press),o);g_signal_connect(o->button,"motion-notify-event",G_CALLBACK(motion),o);g_signal_connect(o->button,"button-release-event",G_CALLBACK(release),o);g_signal_connect(o->overlay,"size-allocate",G_CALLBACK(position),o);g_signal_connect(o->overlay,"destroy",G_CALLBACK(overlay_destroyed),o);g_object_set_data_full(G_OBJECT(o->overlay),"insight-terminal-overlay",o,g_free);return o->overlay;
}
static void free_weak(gpointer data) { GWeakRef *ref=data;g_weak_ref_clear(ref);g_free(ref); }
GtkWidget *insight_terminal_ui_assistant(const gchar *scope,const gchar *label,const gchar *context,GtkWidget *owner,InsightAiInsert insert,gpointer data) {
 insight_terminal_ui_reveal(owner);GWeakRef *ref=g_object_get_data(G_OBJECT(owner),"insight-terminal-assistant");GtkWidget *window=ref ? g_weak_ref_get(ref):NULL;gchar *path=insight_ai_history_path(scope);if(window && g_strcmp0(g_object_get_data(G_OBJECT(window),"insight-ai-history-path"),path)) { gtk_widget_destroy(window);g_object_unref(window);window=NULL; }g_free(path);if(window && g_object_get_data(G_OBJECT(window),"insight-ai-closed")) { g_object_unref(window);window=NULL; }gchar *valid=g_utf8_make_valid(context ? context:"",-1);gsize size=strlen(valid),offset=size>61440 ? size-61440:0;while(offset<size && ((guchar)valid[offset]&0xc0)==0x80) offset++;
 if(!window) { window=insight_ai_terminal_open(scope,label,valid+offset,owner,insert,data);ref=g_new0(GWeakRef,1);g_weak_ref_init(ref,window);g_object_set_data_full(G_OBJECT(owner),"insight-terminal-assistant",ref,free_weak);g_signal_connect_object(owner,"destroy",G_CALLBACK(gtk_widget_destroy),window,G_CONNECT_SWAPPED); }else { insight_ai_context_update(window,valid+offset);gtk_window_deiconify(GTK_WINDOW(window));gtk_window_present(GTK_WINDOW(window));g_object_unref(window); }
 g_free(valid);return window;
}
