/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "insight-relation-graph.h"
#include "../insight/core/relation_graph_layout.h"
#include <pango/pangocairo.h>
typedef struct {
 GtkWidget *canvas,*direction,*zoom,*status,*scroll;GtkTreeView *tree;GtkEntry *query;GtkTreeModel *model;
 gint name,file,line;gboolean vertical;gdouble scale;
} RelationGraph;
static void update(RelationGraph *g) {
 gint total=gtk_tree_model_iter_n_children(g->model,NULL),count=MIN(total,INSIGHT_GRAPH_LIMIT);
 InsightGraphRect last=insight_graph_rect(count ? count-1 : -1,g->vertical);
 gtk_widget_set_size_request(g->canvas,(gint)((last.x+last.width+16)*g->scale),(gint)((last.y+last.height+16)*g->scale));
 gchar *text=g_strdup_printf("%d of %d results • click: Context • double click: editor",count,total);gtk_label_set_text(GTK_LABEL(g->status),text);g_free(text);gtk_widget_queue_draw(g->canvas);
}
static void controls(GtkWidget *widget,gpointer data) {
 (void)widget;RelationGraph *g=data;g->vertical=gtk_combo_box_get_active(GTK_COMBO_BOX(g->direction))==1;g->scale=gtk_spin_button_get_value(GTK_SPIN_BUTTON(g->zoom))/100.0;update(g);
}
static void text_at(GtkWidget *widget,cairo_t *cr,const gchar *text,double x,double y,int width) {
 PangoLayout *layout=gtk_widget_create_pango_layout(widget,text ? text : "");pango_layout_set_width(layout,width*PANGO_SCALE);pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_END);pango_layout_set_single_paragraph_mode(layout,TRUE);cairo_move_to(cr,x,y);pango_cairo_show_layout(cr,layout);g_object_unref(layout);
}
static void card(RelationGraph *g,cairo_t *cr,InsightGraphRect r,const gchar *title,const gchar *detail,gboolean selected) {
 GtkStyleContext *style=gtk_widget_get_style_context(g->canvas);GdkRGBA fg,bg;
 GtkStateFlags state=selected ? GTK_STATE_FLAG_SELECTED : GTK_STATE_FLAG_NORMAL;
 gtk_style_context_get_color(style,GTK_STATE_FLAG_NORMAL,&fg);bg=fg;bg.alpha=0.06;
 if(state==GTK_STATE_FLAG_SELECTED) { bg=(GdkRGBA){0.19,0.5,0.82,1};fg=(GdkRGBA){1,1,1,1}; }
 gdk_cairo_set_source_rgba(cr,&bg);cairo_rectangle(cr,r.x,r.y,r.width,r.height);cairo_fill(cr);
 cairo_set_source_rgb(cr,0.19,0.5,0.82);cairo_set_line_width(cr,selected ? 2.5 : 1);cairo_rectangle(cr,r.x+0.5,r.y+0.5,r.width-1,r.height-1);cairo_stroke(cr);
 gdk_cairo_set_source_rgba(cr,&fg);text_at(g->canvas,cr,title,r.x+8,r.y+4,184);text_at(g->canvas,cr,detail,r.x+8,r.y+25,184);
}
static gboolean draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 (void)widget;RelationGraph *g=data;gint total=gtk_tree_model_iter_n_children(g->model,NULL),count=MIN(total,INSIGHT_GRAPH_LIMIT);
 cairo_save(cr);cairo_scale(cr,g->scale,g->scale);InsightGraphRect root=insight_graph_rect(-1,g->vertical);
 GtkTreeSelection *selection=gtk_tree_view_get_selection(g->tree);GtkTreeModel *selected_model=NULL;GtkTreeIter selected;GtkTreePath *selected_path=NULL;
 if(gtk_tree_selection_get_selected(selection,&selected_model,&selected)) selected_path=gtk_tree_model_get_path(selected_model,&selected);
 GtkTreeIter iter;gboolean valid=gtk_tree_model_get_iter_first(g->model,&iter);
 for(gint i=0;i<count && valid;i++) {
  InsightGraphRect r=insight_graph_rect(i,g->vertical);cairo_set_source_rgba(cr,0.19,0.5,0.82,0.65);cairo_set_line_width(cr,1);
  if(g->vertical) { cairo_move_to(cr,root.x+100,root.y+48);cairo_line_to(cr,root.x+100,76);cairo_line_to(cr,r.x+100,76);cairo_line_to(cr,r.x+100,r.y); }
  else { cairo_move_to(cr,root.x+200,root.y+24);cairo_line_to(cr,228,root.y+24);cairo_line_to(cr,228,r.y+24);cairo_line_to(cr,r.x,r.y+24); }cairo_stroke(cr);
  gchar *name=NULL,*file=NULL;gulong line=0;gtk_tree_model_get(g->model,&iter,g->name,&name,g->file,&file,g->line,&line,-1);
  gchar *base=file ? g_path_get_basename(file) : g_strdup("untitled"),*detail=g_strdup_printf("%s:%lu",base,line);
  GtkTreePath *path=gtk_tree_model_get_path(g->model,&iter);gboolean active=selected_path && gtk_tree_path_compare(path,selected_path)==0;
  card(g,cr,r,name,detail,active);gtk_tree_path_free(path);g_free(name);g_free(file);g_free(base);g_free(detail);valid=gtk_tree_model_iter_next(g->model,&iter);
 }
 card(g,cr,root,gtk_entry_get_text(g->query),count ? "Relation query" : "No visible results",FALSE);
 if(selected_path) gtk_tree_path_free(selected_path);
 cairo_restore(cr);return FALSE;
}
static gboolean click(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)widget;RelationGraph *g=data;if(event->button!=1 || (event->type!=GDK_BUTTON_PRESS && event->type!=GDK_2BUTTON_PRESS)) return FALSE;
 gint row=insight_graph_hit(event->x/g->scale,event->y/g->scale,gtk_tree_model_iter_n_children(g->model,NULL),g->vertical);if(row<0) return FALSE;
 gtk_widget_grab_focus(g->canvas);GtkTreePath *path=gtk_tree_path_new_from_indices(row,-1);gtk_tree_view_set_cursor(g->tree,path,NULL,FALSE);
 if(event->type==GDK_2BUTTON_PRESS) gtk_tree_view_row_activated(g->tree,path,gtk_tree_view_get_column(g->tree,0));
 gtk_tree_path_free(path);gtk_widget_queue_draw(g->canvas);return TRUE;
}
static void reveal(RelationGraph *g,gint row) {
 InsightGraphRect r=insight_graph_rect(row,g->vertical);GtkAdjustment *adjustments[]={gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(g->scroll)),gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(g->scroll))};
 double starts[]={r.x*g->scale,r.y*g->scale},ends[]={(r.x+r.width)*g->scale,(r.y+r.height)*g->scale};
 for(guint i=0;i<2;i++) { GtkAdjustment *a=adjustments[i];double value=gtk_adjustment_get_value(a),page=gtk_adjustment_get_page_size(a);
  if(starts[i]<value) value=starts[i];else if(ends[i]>value+page) value=ends[i]-page;
  gtk_adjustment_set_value(a,CLAMP(value,gtk_adjustment_get_lower(a),MAX(gtk_adjustment_get_lower(a),gtk_adjustment_get_upper(a)-page)));
 }
}
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;RelationGraph *g=data;if(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK)) return FALSE;
 gint count=MIN(gtk_tree_model_iter_n_children(g->model,NULL),INSIGHT_GRAPH_LIMIT);if(!count) return FALSE;
 GtkTreeModel *model;GtkTreeIter iter;gint row=-1;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(g->tree),&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);if(gtk_tree_path_get_depth(path)==1) row=gtk_tree_path_get_indices(path)[0];gtk_tree_path_free(path); }
 if(event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter) {
  if(row>=0 && row<count) { GtkTreePath *path=gtk_tree_path_new_from_indices(row,-1);gtk_tree_view_row_activated(g->tree,path,gtk_tree_view_get_column(g->tree,0));gtk_tree_path_free(path); }return TRUE;
 }
 if(event->keyval==GDK_KEY_Down || event->keyval==GDK_KEY_Right) row=row<0 ? 0 : MIN(row+1,count-1);
 else if(event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_Left) row=row<0 ? count-1 : MAX(0,MIN(row-1,count-1));
 else if(event->keyval==GDK_KEY_Home) row=0;else if(event->keyval==GDK_KEY_End) row=count-1;else return FALSE;
 GtkTreePath *path=gtk_tree_path_new_from_indices(row,-1);gtk_tree_view_set_cursor(g->tree,path,NULL,FALSE);gtk_tree_path_free(path);reveal(g,row);return TRUE;
}
static void selection_changed(GtkTreeSelection *selection,gpointer data) { (void)selection;RelationGraph *g=data;gtk_widget_queue_draw(g->canvas); }
static void destroy(GtkWidget *widget,gpointer data) {
 (void)widget;RelationGraph *g=data;g_signal_handlers_disconnect_by_data(g->model,g);g_signal_handlers_disconnect_by_data(g->query,g);g_signal_handlers_disconnect_by_data(gtk_tree_view_get_selection(g->tree),g);
 g_object_unref(g->tree);g_object_unref(g->model);g_object_unref(g->query);g_free(g);
}
GtkWidget *insight_relation_graph_new(GtkTreeView *tree,GtkEntry *query,gint name,gint file,gint line) {
 RelationGraph *g=g_new0(RelationGraph,1);g->tree=g_object_ref(tree);g->model=g_object_ref(gtk_tree_view_get_model(tree));g->query=g_object_ref(query);g->name=name;g->file=file;g->line=line;g->scale=1;
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*scroll=gtk_scrolled_window_new(NULL,NULL);
 g->direction=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g->direction),"Horizontal");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g->direction),"Vertical");gtk_combo_box_set_active(GTK_COMBO_BOX(g->direction),0);
 g->zoom=gtk_spin_button_new_with_range(50,200,25);gtk_spin_button_set_value(GTK_SPIN_BUTTON(g->zoom),100);gtk_widget_set_tooltip_text(g->zoom,"Graph zoom (%)");
 gtk_box_pack_start(GTK_BOX(bar),g->direction,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(bar),g->zoom,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
 g->scroll=scroll;g->canvas=gtk_drawing_area_new();gtk_widget_set_can_focus(g->canvas,TRUE);gtk_widget_set_name(g->canvas,"insight-relation-graph");gtk_widget_add_events(g->canvas,GDK_BUTTON_PRESS_MASK);gtk_container_add(GTK_CONTAINER(scroll),g->canvas);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 g->status=gtk_label_new("");gtk_label_set_line_wrap(GTK_LABEL(g->status),TRUE);gtk_box_pack_start(GTK_BOX(box),g->status,FALSE,FALSE,0);
 g_signal_connect(g->canvas,"draw",G_CALLBACK(draw),g);g_signal_connect(g->canvas,"button-press-event",G_CALLBACK(click),g);g_signal_connect(g->canvas,"key-press-event",G_CALLBACK(key),g);g_signal_connect(g->direction,"changed",G_CALLBACK(controls),g);g_signal_connect(g->zoom,"value-changed",G_CALLBACK(controls),g);
 const gchar *signals[]={"row-inserted","row-deleted","row-changed","rows-reordered"};for(guint i=0;i<G_N_ELEMENTS(signals);i++) g_signal_connect_swapped(g->model,signals[i],G_CALLBACK(update),g);
 g_signal_connect_swapped(query,"changed",G_CALLBACK(update),g);g_signal_connect(gtk_tree_view_get_selection(tree),"changed",G_CALLBACK(selection_changed),g);g_signal_connect(box,"destroy",G_CALLBACK(destroy),g);update(g);return box;
}
