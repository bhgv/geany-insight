/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-virtual-list.h"
#include <gdk/gdkkeysyms.h>
#define WINDOW_ROWS 256u
typedef struct {
 GtkTreeView *view;GtkListStore *store;GtkWidget *scroll,*bar;
 GtkAdjustment *position;guint count,first;gboolean updating;
 InsightVirtualFill fill;gpointer data;GDestroyNotify destroy;
 InsightVirtualReload reload;gpointer reload_data;
} VirtualList;
static VirtualList *state(GtkTreeView *view) { return g_object_get_data(G_OBJECT(view),"insight-virtual-list"); }
static guint visible_rows(VirtualList *s) {
 gint height=22;
 GList *columns=gtk_tree_view_get_columns(s->view);
 if(columns) {
  GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(columns->data));
  if(cells) {gint minimum=0,natural=0;gtk_cell_renderer_get_preferred_height(cells->data,GTK_WIDGET(s->view),&minimum,&natural);height=MAX(height,natural+2);}
  g_list_free(cells);
 }
 g_list_free(columns);
 GdkRectangle visible={0};if(gtk_widget_get_realized(GTK_WIDGET(s->view))) gtk_tree_view_get_visible_rect(s->view,&visible);
 gint available=visible.height>0?visible.height:gtk_widget_get_allocated_height(GTK_WIDGET(s->view));
 /* A conservative page size keeps the final row visible despite theme padding. */
 return CLAMP(available/MAX(1,height)-1,1,WINDOW_ROWS/2);
}
static void range(VirtualList *s) {
 guint page=visible_rows(s);s->updating=TRUE;
 gtk_adjustment_configure(s->position,s->first,0,MAX(s->count,page),1,MAX(1,page-1),page);
 gtk_widget_set_sensitive(s->bar,s->count>page);s->updating=FALSE;
}
static void render(VirtualList *s,guint first) {
 if(s->updating) return;
 guint page=visible_rows(s);first=MIN(first,s->count>page?s->count-page:0);
 if(first==s->first && gtk_tree_model_iter_n_children(GTK_TREE_MODEL(s->store),NULL)>0) return;
 s->updating=TRUE;s->first=first;
 gboolean sensitive=gtk_widget_get_sensitive(GTK_WIDGET(s->view));
 gtk_widget_set_sensitive(GTK_WIDGET(s->view),FALSE);
 gtk_list_store_clear(s->store);
 for(guint n=first;s->fill && n<s->count && n-first<WINDOW_ROWS;n++) s->fill(s->store,n,s->data);
 GtkAdjustment *local=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(s->scroll));
 gtk_adjustment_set_value(local,0);
 gtk_widget_set_sensitive(GTK_WIDGET(s->view),sensitive);s->updating=FALSE;range(s);
 g_object_set_data(G_OBJECT(s->view),"insight-virtual-window-rows",GUINT_TO_POINTER(MIN(WINDOW_ROWS,s->count-first)));
}
static void changed(GtkAdjustment *adjustment,gpointer data) {
 VirtualList *s=data;if(!s->updating) render(s,(guint)gtk_adjustment_get_value(adjustment));
}
static gboolean wheel(GtkWidget *widget,GdkEventScroll *event,gpointer data) {
 (void)widget;VirtualList *s=data;gdouble delta=0;
 if(event->direction==GDK_SCROLL_DOWN) delta=3;
 else if(event->direction==GDK_SCROLL_UP) delta=-3;
 else if(event->direction==GDK_SCROLL_SMOOTH) delta=event->delta_y*3;
 if(!delta) return FALSE;
 gtk_adjustment_set_value(s->position,gtk_adjustment_get_value(s->position)+delta);return TRUE;
}
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;VirtualList *s=data;gdouble next=s->first;guint page=visible_rows(s);
 if((event->keyval==GDK_KEY_Down || event->keyval==GDK_KEY_Up) &&
    gtk_tree_view_get_model(s->view)==GTK_TREE_MODEL(s->store) && !(event->state&(GDK_CONTROL_MASK|GDK_SHIFT_MASK))) {
  GtkTreeIter iter;GtkTreeModel *model=NULL;gint64 absolute=s->first;
  if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(s->view),&model,&iter)) {
   GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gint *indices=gtk_tree_path_get_indices(path);
   if(indices) absolute+=indices[0]+(event->keyval==GDK_KEY_Down?1:-1);gtk_tree_path_free(path);
  }
  if(!s->count) return TRUE;
  absolute=CLAMP(absolute,0,(gint64)s->count-1);
  if(absolute<s->first) gtk_adjustment_set_value(s->position,absolute);
  else if(absolute>=s->first+page) gtk_adjustment_set_value(s->position,absolute-page+1);
  GtkTreePath *path=gtk_tree_path_new_from_indices((gint)(absolute-s->first),-1);
  gtk_tree_view_set_cursor(s->view,path,NULL,FALSE);gtk_tree_path_free(path);return TRUE;
 }
 switch(event->keyval) {
 case GDK_KEY_Home:next=0;break;
 case GDK_KEY_End:next=s->count;break;
 case GDK_KEY_Page_Down:next+=MAX(1,page-1);break;
 case GDK_KEY_Page_Up:next-=MAX(1,page-1);break;
 default:return FALSE;
 }
 gtk_adjustment_set_value(s->position,next);return TRUE;
}
static void allocated(GtkWidget *widget,GtkAllocation *allocation,gpointer data) { (void)widget;(void)allocation;VirtualList *s=data;if(!s->updating) {
 guint page=visible_rows(s),first=MIN(s->first,s->count>page?s->count-page:0);
 if(first!=s->first) render(s,first);else range(s);
} }
static void sort_changed(GtkTreeSortable *store,gpointer data) { (void)store;VirtualList *s=state(GTK_TREE_VIEW(data));if(s && !s->updating && s->reload) s->reload(s->view,s->reload_data); }
static void freed(gpointer data) {
 VirtualList *s=data;g_signal_handlers_disconnect_by_data(s->position,s);if(s->destroy) s->destroy(s->data);g_object_unref(s->store);g_object_unref(s->position);g_free(s);
}
GtkWidget *insight_virtual_list_wrap(GtkWidget *scroll,GtkTreeView *view,GtkListStore *store,InsightVirtualReload reload,gpointer data) {
 VirtualList *s=g_new0(VirtualList,1);s->view=view;s->store=g_object_ref(store);s->scroll=scroll;s->reload=reload;s->reload_data=data;
 s->position=GTK_ADJUSTMENT(g_object_ref_sink(gtk_adjustment_new(0,0,1,1,10,1)));
 s->bar=gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL,s->position);gtk_widget_set_name(s->bar,"insight-virtual-scrollbar");
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(box),s->bar,FALSE,FALSE,0);
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_NEVER);
 g_object_set_data_full(G_OBJECT(view),"insight-virtual-list",s,freed);
 g_signal_connect(s->position,"value-changed",G_CALLBACK(changed),s);
 g_signal_connect(view,"scroll-event",G_CALLBACK(wheel),s);
 g_signal_connect(view,"key-press-event",G_CALLBACK(key),s);
 g_signal_connect(view,"size-allocate",G_CALLBACK(allocated),s);
 g_signal_connect_object(store,"sort-column-changed",G_CALLBACK(sort_changed),view,0);
 return box;
}
gboolean insight_virtual_list_attached(GtkTreeView *view) { return state(view)!=NULL; }
void insight_virtual_list_replace(GtkTreeView *view,guint count,InsightVirtualFill fill,gpointer data,GDestroyNotify destroy) {
 VirtualList *s=state(view);if(!s) {if(destroy) destroy(data);return;}
 if(s->destroy) s->destroy(s->data);s->data=data;s->destroy=destroy;s->fill=fill;s->count=count;
 guint first=s->first;s->first=G_MAXUINT;render(s,first);
}
void insight_virtual_list_clear(GtkTreeView *view) { insight_virtual_list_replace(view,0,NULL,NULL,NULL); }
void insight_virtual_list_reload(GtkTreeView *view) { VirtualList *s=state(view);if(s && s->reload && !s->updating) s->reload(view,s->reload_data); }
void insight_virtual_list_position(GtkTreeView *view,guint first) { VirtualList *s=state(view);if(s) gtk_adjustment_set_value(s->position,first); }
guint insight_virtual_list_count(GtkTreeView *view) { VirtualList *s=state(view);return s?s->count:0; }
guint insight_virtual_list_first(GtkTreeView *view) { VirtualList *s=state(view);return s?s->first:0; }
