#ifdef GEANY_PRIVATE
#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
#endif
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "insight-relation-graph.h"
#include "../insight/core/relation_graph_layout.h"
#include <pango/pangocairo.h>
typedef struct {
 GtkWidget *canvas,*direction,*zoom,*status,*scroll;GtkTreeView *tree;GtkEntry *query;GtkTreeModel *model;
 gint name,file,line,total;gboolean vertical,hierarchical,panning,pointer_select;double pan_x,pan_y,pan_h,pan_v,wheel_delta;gdouble scale;GPtrArray *paths;
 GtkWidget *menu;InsightRelationGraphQuery query_handler;gpointer query_data;
} RelationGraph;
static gboolean collect_node(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 RelationGraph *g=data;if(gtk_tree_path_get_depth(path)<2) return FALSE;
 gulong line=0;gtk_tree_model_get(model,iter,g->line,&line,-1);if(!line) return FALSE;
 GtkTreePath *parent=gtk_tree_path_copy(path);gboolean shown=TRUE;
 while(gtk_tree_path_up(parent) && gtk_tree_path_get_depth(parent)>0) if(!gtk_tree_view_row_expanded(g->tree,parent)) { shown=FALSE;break; }
 gtk_tree_path_free(parent);if(!shown) return FALSE;
 g->total++;if(g->paths->len<INSIGHT_GRAPH_LIMIT) g_ptr_array_add(g->paths,gtk_tree_path_copy(path));return FALSE;
}
static gint count_nodes(RelationGraph *g) { return g->hierarchical ? (gint)g->paths->len : MIN(g->total,INSIGHT_GRAPH_LIMIT); }
static GtkTreePath *node_path(RelationGraph *g,gint row) { return g->hierarchical ? gtk_tree_path_copy(g_ptr_array_index(g->paths,row)) : gtk_tree_path_new_from_indices(row,-1); }
static gint path_row(RelationGraph *g,GtkTreePath *path) {
 if(!g->hierarchical) return gtk_tree_path_get_depth(path)==1 ? gtk_tree_path_get_indices(path)[0] : -1;
 for(guint i=0;i<g->paths->len;i++) if(!gtk_tree_path_compare(path,g_ptr_array_index(g->paths,i))) return (gint)i;
 return -1;
}
static InsightGraphRect node_rect(RelationGraph *g,gint row) {
 if(!g->hierarchical || row<0) return insight_graph_rect(row,g->vertical);
 GtkTreePath *path=g_ptr_array_index(g->paths,row);return insight_graph_tree_rect(row,gtk_tree_path_get_depth(path)-2,g->vertical);
}
static InsightGraphRect extent(RelationGraph *g) {
 InsightGraphRect r=insight_graph_rect(-1,g->vertical);
 for(gint i=0;i<count_nodes(g);i++) { InsightGraphRect node=node_rect(g,i);r.width=MAX(r.width,node.x+node.width-r.x);r.height=MAX(r.height,node.y+node.height-r.y); }return r;
}
static gint hit_node(RelationGraph *g,double x,double y) {
 for(gint i=0;i<count_nodes(g);i++) { InsightGraphRect r=node_rect(g,i);if(x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height) return i; }return -1;
}
static gboolean branch_node(RelationGraph *g,GtkTreePath *path) { GtkTreeIter iter;return g->hierarchical && gtk_tree_model_get_iter(g->model,&iter,path) && gtk_tree_model_iter_has_child(g->model,&iter); }
static InsightGraphRect toggle_rect(RelationGraph *g,InsightGraphRect node) { InsightGraphRect r={node.x+node.width-16,node.y+node.height/2-6,12,12};if(g->vertical) { r.x=node.x+node.width/2-6;r.y=node.y+node.height-16; }return r; }
static gboolean hit_toggle(RelationGraph *g,InsightGraphRect node,double x,double y) { InsightGraphRect r=toggle_rect(g,node);return x>=r.x-3 && x<r.x+r.width+3 && y>=r.y-3 && y<r.y+r.height+3; }
static void toggle_node(RelationGraph *g,GtkTreePath *path) { if(gtk_tree_view_row_expanded(g->tree,path)) gtk_tree_view_collapse_row(g->tree,path);else gtk_tree_view_expand_row(g->tree,path,FALSE); }

static void update(RelationGraph *g) {
 g_ptr_array_set_size(g->paths,0);g->total=0;
 if(g->hierarchical) gtk_tree_model_foreach(g->model,collect_node,g);else g->total=gtk_tree_model_iter_n_children(g->model,NULL);
 gint count=count_nodes(g);InsightGraphRect bounds=extent(g);
 gtk_widget_set_size_request(g->canvas,(gint)((bounds.x+bounds.width+16)*g->scale),(gint)((bounds.y+bounds.height+16)*g->scale));
 gchar *text=g_strdup_printf("%d of %d results • click: Context • double click: editor",count,g->total);gtk_label_set_text(GTK_LABEL(g->status),text);g_free(text);gtk_widget_queue_draw(g->canvas);
}
static void viewport(RelationGraph *g,GtkAdjustment **h,GtkAdjustment **v) {
 *h=gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(g->scroll));*v=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(g->scroll));
}
static void scroll_to(GtkAdjustment *a,double value) {
 gtk_adjustment_set_value(a,insight_graph_scroll_value(value,gtk_adjustment_get_upper(a),gtk_adjustment_get_page_size(a)));
}
static void zoom_at(RelationGraph *g,gint zoom,double x,double y) {
 GtkAdjustment *h,*v;viewport(g,&h,&v);double old=g->scale,hvalue=gtk_adjustment_get_value(h),vvalue=gtk_adjustment_get_value(v);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(g->zoom),CLAMP(zoom,INSIGHT_GRAPH_MIN_ZOOM,INSIGHT_GRAPH_MAX_ZOOM));
 InsightGraphRect r=extent(g);GtkAdjustment *a[]={h,v};double sizes[]={(r.x+r.width+16)*g->scale,(r.y+r.height+16)*g->scale},values[]={hvalue,vvalue},points[]={x,y};
 for(guint i=0;i<2;i++) { double page=gtk_adjustment_get_page_size(a[i]);gtk_adjustment_set_upper(a[i],MAX(page,sizes[i]));scroll_to(a[i],insight_graph_zoom_scroll(values[i],points[i],old,g->scale,gtk_adjustment_get_upper(a[i]),page)); }
}
static void zoom_center(RelationGraph *g,gint zoom) {
 GtkAdjustment *h,*v;viewport(g,&h,&v);zoom_at(g,zoom,gtk_adjustment_get_page_size(h)/2,gtk_adjustment_get_page_size(v)/2);
}
static void fit_graph(GtkWidget *widget,gpointer data) {
 (void)widget;RelationGraph *g=data;GtkAdjustment *h,*v;viewport(g,&h,&v);
 zoom_center(g,insight_graph_fit_zoom(extent(g),MAX(1,gtk_adjustment_get_page_size(h)-8),MAX(1,gtk_adjustment_get_page_size(v)-8)));scroll_to(h,0);scroll_to(v,0);
}
static void original_zoom(GtkWidget *widget,gpointer data) { (void)widget;zoom_center(data,100); }
static void center_selection(GtkWidget *widget,gpointer data) {
 (void)widget;RelationGraph *g=data;GtkTreeModel *model;GtkTreeIter iter;gint row=-1;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(g->tree),&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);row=path_row(g,path);gtk_tree_path_free(path); }
 InsightGraphRect r=node_rect(g,row>=0 && row<count_nodes(g) ? row : -1);GtkAdjustment *h,*v;viewport(g,&h,&v);
 scroll_to(h,(r.x+r.width/2)*g->scale-gtk_adjustment_get_page_size(h)/2);scroll_to(v,(r.y+r.height/2)*g->scale-gtk_adjustment_get_page_size(v)/2);
}
static void controls(GtkWidget *widget,gpointer data) {
 (void)widget;RelationGraph *g=data;g->vertical=gtk_combo_box_get_active(GTK_COMBO_BOX(g->direction))==1;g->scale=gtk_spin_button_get_value(GTK_SPIN_BUTTON(g->zoom))/100.0;update(g);
}
static void text_at(GtkWidget *widget,cairo_t *cr,const gchar *text,double x,double y,int width) {
 PangoLayout *layout=gtk_widget_create_pango_layout(widget,text ? text : "");pango_layout_set_width(layout,width*PANGO_SCALE);pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_END);pango_layout_set_single_paragraph_mode(layout,TRUE);cairo_move_to(cr,x,y);pango_cairo_show_layout(cr,layout);g_object_unref(layout);
}
static void card(RelationGraph *g,cairo_t *cr,InsightGraphRect r,const gchar *title,const gchar *detail,gboolean selected) {
 cairo_set_source_rgba(cr,0.18,0.18,0.18,0.6);cairo_rectangle(cr,r.x+3,r.y+3,r.width,r.height);cairo_fill(cr);
 if(selected) cairo_set_source_rgb(cr,0.19,0.42,0.77);else cairo_set_source_rgb(cr,1,0.995,0.93);
 cairo_rectangle(cr,r.x,r.y,r.width,r.height);cairo_fill(cr);cairo_set_source_rgb(cr,0.36,0.36,0.34);cairo_set_line_width(cr,selected ? 2 : 1);cairo_rectangle(cr,r.x+0.5,r.y+0.5,r.width-1,r.height-1);cairo_stroke(cr);
 cairo_set_source_rgb(cr,0.05,0.64,0.66);cairo_rectangle(cr,r.x+6,r.y+6,14,14);cairo_fill(cr);cairo_set_source_rgb(cr,0.02,0.36,0.46);cairo_rectangle(cr,r.x+6.5,r.y+6.5,13,13);cairo_stroke(cr);
 cairo_set_source_rgb(cr,0.94,1,1);cairo_move_to(cr,r.x+10,r.y+10);cairo_line_to(cr,r.x+16,r.y+10);cairo_line_to(cr,r.x+11,r.y+16);cairo_line_to(cr,r.x+16,r.y+16);cairo_set_line_width(cr,1.4);cairo_stroke(cr);
 if(selected) cairo_set_source_rgb(cr,1,1,1);else cairo_set_source_rgb(cr,0.10,0.13,0.16);
 text_at(g->canvas,cr,title,r.x+25,r.y+4,g->hierarchical ? 148 : 164);text_at(g->canvas,cr,detail,r.x+8,r.y+25,g->hierarchical ? 168 : 184);
}
static void draw_toggle(RelationGraph *g,cairo_t *cr,InsightGraphRect node,GtkTreePath *path) {
 if(!branch_node(g,path)) return;
 InsightGraphRect r=toggle_rect(g,node);
 cairo_set_source_rgb(cr,0.19,0.5,0.82);cairo_set_line_width(cr,1);cairo_rectangle(cr,r.x+0.5,r.y+0.5,r.width-1,r.height-1);cairo_stroke(cr);
 cairo_move_to(cr,r.x+3,r.y+6);cairo_line_to(cr,r.x+9,r.y+6);if(!gtk_tree_view_row_expanded(g->tree,path)) { cairo_move_to(cr,r.x+6,r.y+3);cairo_line_to(cr,r.x+6,r.y+9); }cairo_stroke(cr);
}
static gboolean draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 (void)widget;RelationGraph *g=data;gint count=count_nodes(g);
 cairo_set_source_rgb(cr,0.74,0.74,0.72);cairo_paint(cr);
 cairo_save(cr);cairo_scale(cr,g->scale,g->scale);InsightGraphRect root=insight_graph_rect(-1,g->vertical);
 GtkTreeSelection *selection=gtk_tree_view_get_selection(g->tree);GtkTreeModel *selected_model=NULL;GtkTreeIter selected;GtkTreePath *selected_path=NULL;
 if(gtk_tree_selection_get_selected(selection,&selected_model,&selected)) selected_path=gtk_tree_model_get_path(selected_model,&selected);
 for(gint i=0;i<count;i++) {
  GtkTreePath *path=node_path(g,i);GtkTreeIter iter;if(!gtk_tree_model_get_iter(g->model,&iter,path)) { gtk_tree_path_free(path);continue; }
  InsightGraphRect r=node_rect(g,i),parent=root;
  if(g->hierarchical) { GtkTreePath *ancestor=gtk_tree_path_copy(path);gtk_tree_path_up(ancestor);gint row=path_row(g,ancestor);if(row>=0) parent=node_rect(g,row);gtk_tree_path_free(ancestor); }
  cairo_set_source_rgb(cr,0.83,0.31,0.33);cairo_set_line_width(cr,1);
  if(g->vertical) { double middle=(parent.y+parent.height+r.y)/2;cairo_move_to(cr,parent.x+100,parent.y+48);cairo_line_to(cr,parent.x+100,middle);cairo_line_to(cr,r.x+100,middle);cairo_line_to(cr,r.x+100,r.y); }
  else { double middle=(parent.x+parent.width+r.x)/2;cairo_move_to(cr,parent.x+200,parent.y+24);cairo_line_to(cr,middle,parent.y+24);cairo_line_to(cr,middle,r.y+24);cairo_line_to(cr,r.x,r.y+24); }cairo_stroke(cr);
  gchar *name=NULL,*file=NULL;gulong line=0;gtk_tree_model_get(g->model,&iter,g->name,&name,g->file,&file,g->line,&line,-1);
  gchar *base=file ? g_path_get_basename(file) : g_strdup("untitled"),*detail=g_strdup_printf("%s:%lu",base,line);
  gboolean active=selected_path && gtk_tree_path_compare(path,selected_path)==0;card(g,cr,r,name,detail,active);if(g->hierarchical) draw_toggle(g,cr,r,path);
  gtk_tree_path_free(path);g_free(name);g_free(file);g_free(base);g_free(detail);
 }
 card(g,cr,root,gtk_entry_get_text(g->query),count ? "Relation query" : "No visible results",FALSE);
 if(g->hierarchical) { GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);draw_toggle(g,cr,root,path);gtk_tree_path_free(path); }
 if(selected_path) gtk_tree_path_free(selected_path);
 cairo_restore(cr);return FALSE;
}
static cairo_surface_t *image(RelationGraph *g) {
 int width=0,height=0;
 if(!insight_graph_extent_image_size(extent(g),g->scale,&width,&height)) { gtk_label_set_text(GTK_LABEL(g->status),"Image is too large. Reduce zoom or filter the results before exporting.");return NULL; }
 cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height);
 if(cairo_surface_status(surface)!=CAIRO_STATUS_SUCCESS) { gtk_label_set_text(GTK_LABEL(g->status),cairo_status_to_string(cairo_surface_status(surface)));cairo_surface_destroy(surface);return NULL; }
 cairo_t *cr=cairo_create(surface);draw(g->canvas,cr,g);
 cairo_status_t status=cairo_status(cr);cairo_destroy(cr);
 if(status!=CAIRO_STATUS_SUCCESS) { gtk_label_set_text(GTK_LABEL(g->status),cairo_status_to_string(status));cairo_surface_destroy(surface);return NULL; }
 return surface;
}
static void copy_image(GtkWidget *widget,gpointer data) {
 (void)widget;RelationGraph *g=data;cairo_surface_t *surface=image(g);if(!surface) return;
 GdkPixbuf *pixbuf=gdk_pixbuf_get_from_surface(surface,0,0,cairo_image_surface_get_width(surface),cairo_image_surface_get_height(surface));cairo_surface_destroy(surface);
 if(pixbuf) { gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),pixbuf);g_object_unref(pixbuf);gtk_label_set_text(GTK_LABEL(g->status),"Graph image copied to clipboard"); }
 else gtk_label_set_text(GTK_LABEL(g->status),"Graph image could not be allocated");
}
static cairo_status_t png_chunk(void *closure,const unsigned char *bytes,unsigned int length) {
 g_byte_array_append(closure,bytes,length);return CAIRO_STATUS_SUCCESS;
}
static void save_image(GtkWidget *widget,gpointer data) {
 (void)widget;RelationGraph *g=data;cairo_surface_t *surface=image(g);if(!surface) return;
 GtkWidget *status_label=g_object_ref(g->status);GtkWidget *parent=gtk_widget_get_toplevel(g->canvas);
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Save relation graph",GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,GTK_FILE_CHOOSER_ACTION_SAVE,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,NULL);
 gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),TRUE);gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),"relations.png");
 GtkFileFilter *filter=gtk_file_filter_new();gtk_file_filter_set_name(filter,"PNG image");gtk_file_filter_add_pattern(filter,"*.png");gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),filter);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));GByteArray *png=g_byte_array_new();GError *error=NULL;cairo_status_t status=cairo_surface_write_to_png_stream(surface,png_chunk,png);
  gboolean saved=status==CAIRO_STATUS_SUCCESS && path && g_file_set_contents(path,(const gchar *)png->data,png->len,&error);g_byte_array_unref(png);
  /* The rendered surface is a snapshot: model updates while the chooser is
   * open do not change the exported image. */
  if(saved) gtk_label_set_text(GTK_LABEL(status_label),"Graph image saved");
  else { GtkWidget *message=gtk_message_dialog_new(GTK_WINDOW(dialog),GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_ERROR,GTK_BUTTONS_CLOSE,"%s",error ? error->message : cairo_status_to_string(status));gtk_dialog_run(GTK_DIALOG(message));gtk_widget_destroy(message); }
  g_clear_error(&error);g_free(path);
 }
 gtk_widget_destroy(dialog);cairo_surface_destroy(surface);g_object_unref(status_label);
}
static void query_action(GtkWidget *item,gpointer data) {
 RelationGraph *g=data;if(g->query_handler) g->query_handler(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"relation-mode")),g->query_data);
}
static void open_node(GtkWidget *item,gpointer data) {
 (void)item;RelationGraph *g=data;GtkTreeModel *model;GtkTreeIter iter;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(g->tree),&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gtk_tree_view_row_activated(g->tree,path,gtk_tree_view_get_column(g->tree,0));gtk_tree_path_free(path); }
}
static GtkWidget *node_menu(RelationGraph *g) {
 if(g->menu) return g->menu;
 g->menu=gtk_menu_new();g_object_ref_sink(g->menu);
 GtkWidget *open=gtk_menu_item_new_with_label("Open in editor");gtk_menu_shell_append(GTK_MENU_SHELL(g->menu),open);g_signal_connect(open,"activate",G_CALLBACK(open_node),g);
 const gchar *labels[]={"Definitions","Call sites","References","Direct members","Base types","Derived types"};const gint modes[]={1,7,0,2,4,5};
 for(guint i=0;i<G_N_ELEMENTS(modes);i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);g_object_set_data(G_OBJECT(item),"relation-mode",GINT_TO_POINTER(modes[i]));gtk_widget_set_sensitive(item,g->query_handler!=NULL);gtk_menu_shell_append(GTK_MENU_SHELL(g->menu),item);g_signal_connect(item,"activate",G_CALLBACK(query_action),g); }
 gtk_menu_shell_append(GTK_MENU_SHELL(g->menu),gtk_separator_menu_item_new());
 GtkWidget *copy=gtk_menu_item_new_with_label("Copy graph image"),*save=gtk_menu_item_new_with_label("Save graph PNG…");gtk_menu_shell_append(GTK_MENU_SHELL(g->menu),copy);gtk_menu_shell_append(GTK_MENU_SHELL(g->menu),save);g_signal_connect(copy,"activate",G_CALLBACK(copy_image),g);g_signal_connect(save,"activate",G_CALLBACK(save_image),g);gtk_widget_show_all(g->menu);return g->menu;
}
static void end_pan(RelationGraph *g) {
 if(!g->panning) return;g->panning=FALSE;if(gtk_widget_has_grab(g->canvas)) gtk_grab_remove(g->canvas);
 GdkWindow *window=gtk_widget_get_window(g->canvas);if(window) gdk_window_set_cursor(window,NULL);
}
static gboolean pan_motion(GtkWidget *widget,GdkEventMotion *event,gpointer data) {
 (void)widget;RelationGraph *g=data;if(!g->panning) return FALSE;GtkAdjustment *h,*v;viewport(g,&h,&v);scroll_to(h,g->pan_h+g->pan_x-event->x_root);scroll_to(v,g->pan_v+g->pan_y-event->y_root);return TRUE;
}
static gboolean pan_release(GtkWidget *widget,GdkEventButton *event,gpointer data) { (void)widget;RelationGraph *g=data;if(event->button!=2 || !g->panning) return FALSE;end_pan(g);return TRUE; }
static void pan_unmap(GtkWidget *widget,gpointer data) { (void)widget;end_pan(data); }
static gboolean pan_broken(GtkWidget *widget,GdkEventGrabBroken *event,gpointer data) { (void)widget;(void)event;end_pan(data);return FALSE; }
static gboolean wheel(GtkWidget *widget,GdkEventScroll *event,gpointer data) {
 (void)widget;RelationGraph *g=data;if(!(event->state&GDK_CONTROL_MASK)) return FALSE;
 double dy=0,dx=0;if(!gdk_event_get_scroll_deltas((GdkEvent *)event,&dx,&dy)) dy=event->direction==GDK_SCROLL_UP ? -1 : event->direction==GDK_SCROLL_DOWN ? 1 : 0;
 if(!dy || !(dy>=-1000 && dy<=1000)) return FALSE;
 if(event->direction!=GDK_SCROLL_SMOOTH) g->wheel_delta=0;
 g->wheel_delta=CLAMP(g->wheel_delta+dy,-4,4);gint steps=(gint)g->wheel_delta;if(!steps) return TRUE;g->wheel_delta-=steps;
 GtkAdjustment *h,*v;viewport(g,&h,&v);gint value=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g->zoom));
 zoom_at(g,value-steps*10,event->x-gtk_adjustment_get_value(h),event->y-gtk_adjustment_get_value(v));return TRUE;
}
static gboolean tooltip(GtkWidget *widget,gint x,gint y,gboolean keyboard,GtkTooltip *tip,gpointer data) {
 (void)widget;RelationGraph *g=data;gint row=-1;GtkTreeModel *model;GtkTreeIter iter;
 if(keyboard) { if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(g->tree),&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);row=path_row(g,path);gtk_tree_path_free(path); } }
 else row=hit_node(g,x/g->scale,y/g->scale);
 if(row<0 || row>=count_nodes(g)) return FALSE;
 GtkTreePath *path=node_path(g,row);if(!gtk_tree_model_get_iter(g->model,&iter,path)) { gtk_tree_path_free(path);return FALSE; }gtk_tree_path_free(path);
 gchar *name=NULL,*file=NULL;gulong line=0;gtk_tree_model_get(g->model,&iter,g->name,&name,g->file,&file,g->line,&line,-1);gchar *text=g_strdup_printf("%s\n%s:%lu",name ? name : "",file ? file : "untitled",line);gtk_tooltip_set_text(tip,text);g_free(text);g_free(name);g_free(file);return TRUE;
}
static gboolean click(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)widget;RelationGraph *g=data;
 if(event->button==2 && event->type==GDK_BUTTON_PRESS) { if(g->panning) return TRUE;GtkAdjustment *h,*v;viewport(g,&h,&v);g->panning=TRUE;g->pan_x=event->x_root;g->pan_y=event->y_root;g->pan_h=gtk_adjustment_get_value(h);g->pan_v=gtk_adjustment_get_value(v);gtk_grab_add(g->canvas);gtk_widget_grab_focus(g->canvas);GdkWindow *window=gtk_widget_get_window(g->canvas);if(window) { GdkCursor *cursor=gdk_cursor_new_from_name(gtk_widget_get_display(g->canvas),"grabbing");gdk_window_set_cursor(window,cursor);if(cursor) g_object_unref(cursor); }return TRUE; }
 if((event->button!=1 && event->button!=3) || (event->type!=GDK_BUTTON_PRESS && event->type!=GDK_2BUTTON_PRESS)) return FALSE;
 double x=event->x/g->scale,y=event->y/g->scale;
 if(g->hierarchical && event->button==1 && event->type==GDK_BUTTON_PRESS && hit_toggle(g,insight_graph_rect(-1,g->vertical),x,y)) {
  GtkTreePath *root=gtk_tree_path_new_from_indices(0,-1);if(branch_node(g,root)) toggle_node(g,root);gtk_tree_path_free(root);return TRUE;
 }
 gint row=hit_node(g,x,y);if(row<0) return FALSE;
 gtk_widget_grab_focus(g->canvas);GtkTreePath *path=node_path(g,row);
 if(g->hierarchical && event->button==1 && event->type==GDK_BUTTON_PRESS && branch_node(g,path) && hit_toggle(g,node_rect(g,row),x,y)) { toggle_node(g,path);gtk_tree_path_free(path);return TRUE; }
 g->pointer_select=TRUE;gtk_tree_view_set_cursor(g->tree,path,NULL,FALSE);g->pointer_select=FALSE;
 if(event->button==3) gtk_menu_popup_at_pointer(GTK_MENU(node_menu(g)),(GdkEvent *)event);
 else if(event->type==GDK_2BUTTON_PRESS) gtk_tree_view_row_activated(g->tree,path,gtk_tree_view_get_column(g->tree,0));
 gtk_tree_path_free(path);gtk_widget_queue_draw(g->canvas);return TRUE;
}
static void reveal(RelationGraph *g,gint row) {
 InsightGraphRect r=node_rect(g,row);GtkAdjustment *adjustments[]={gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(g->scroll)),gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(g->scroll))};
 double starts[]={r.x*g->scale,r.y*g->scale},ends[]={(r.x+r.width)*g->scale,(r.y+r.height)*g->scale};
 for(guint i=0;i<2;i++) { GtkAdjustment *a=adjustments[i];double value=gtk_adjustment_get_value(a),page=gtk_adjustment_get_page_size(a);
  if(starts[i]<value) value=starts[i];else if(ends[i]>value+page) value=ends[i]-page;
  gtk_adjustment_set_value(a,CLAMP(value,gtk_adjustment_get_lower(a),MAX(gtk_adjustment_get_lower(a),gtk_adjustment_get_upper(a)-page)));
 }
}
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;RelationGraph *g=data;
 if(event->keyval==GDK_KEY_Escape && g->panning) { end_pan(g);return TRUE; }
 if((event->state&GDK_CONTROL_MASK) && !(event->state&GDK_MOD1_MASK)) {
  if(event->keyval==GDK_KEY_0 || event->keyval==GDK_KEY_KP_0) { zoom_center(g,100);return TRUE; }
  if(event->keyval==GDK_KEY_plus || event->keyval==GDK_KEY_equal || event->keyval==GDK_KEY_KP_Add) { zoom_center(g,gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g->zoom))+10);return TRUE; }
  if(event->keyval==GDK_KEY_minus || event->keyval==GDK_KEY_KP_Subtract) { zoom_center(g,gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g->zoom))-10);return TRUE; }
 }
 if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_c || event->keyval==GDK_KEY_C)) { copy_image(NULL,g);return TRUE; }
 if(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK)) return FALSE;
 if(event->keyval==GDK_KEY_f || event->keyval==GDK_KEY_F) { fit_graph(NULL,g);return TRUE; }
 if(event->keyval==GDK_KEY_space) { center_selection(NULL,g);return TRUE; }
 gint count=count_nodes(g);if(!count) {
  if(g->hierarchical && (event->keyval==GDK_KEY_Right || event->keyval==GDK_KEY_plus || event->keyval==GDK_KEY_KP_Add)) { GtkTreePath *root=gtk_tree_path_new_from_indices(0,-1);if(branch_node(g,root)) gtk_tree_view_expand_row(g->tree,root,FALSE);gtk_tree_path_free(root);return TRUE; }return FALSE;
 }
 GtkTreeModel *model;GtkTreeIter iter;gint row=-1;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(g->tree),&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);row=path_row(g,path);gtk_tree_path_free(path); }
 if(row>=0 && row<count && g->query_handler) {
  gint mode=-1;
  if(event->keyval==GDK_KEY_d || event->keyval==GDK_KEY_D) mode=1;
  else if(event->keyval==GDK_KEY_c || event->keyval==GDK_KEY_C) mode=7;
  else if(event->keyval==GDK_KEY_r || event->keyval==GDK_KEY_R) mode=0;
  if(mode>=0) { g->query_handler(mode,g->query_data);return TRUE; }
 }
 if(event->keyval==GDK_KEY_Menu || (event->keyval==GDK_KEY_F10 && (event->state&GDK_SHIFT_MASK))) {
  if(row>=0 && row<count && gtk_widget_get_window(g->canvas)) {
   InsightGraphRect r=node_rect(g,row);GdkRectangle anchor={(gint)(r.x*g->scale),(gint)(r.y*g->scale),(gint)(r.width*g->scale),(gint)(r.height*g->scale)};
   gtk_menu_popup_at_rect(GTK_MENU(node_menu(g)),gtk_widget_get_window(g->canvas),&anchor,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,(GdkEvent *)event);
  }
  return TRUE;
 }
 if(event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter) {
  if(row>=0 && row<count) { GtkTreePath *path=node_path(g,row);gtk_tree_view_row_activated(g->tree,path,gtk_tree_view_get_column(g->tree,0));gtk_tree_path_free(path); }return TRUE;
 }
 if(g->hierarchical && row>=0 && (event->keyval==GDK_KEY_Right || event->keyval==GDK_KEY_Left || event->keyval==GDK_KEY_plus || event->keyval==GDK_KEY_minus || event->keyval==GDK_KEY_KP_Add || event->keyval==GDK_KEY_KP_Subtract)) {
  GtkTreePath *path=node_path(g,row);gboolean open=event->keyval==GDK_KEY_Right || event->keyval==GDK_KEY_plus || event->keyval==GDK_KEY_KP_Add;
  if(open) gtk_tree_view_expand_row(g->tree,path,FALSE);else if(gtk_tree_view_row_expanded(g->tree,path)) gtk_tree_view_collapse_row(g->tree,path);else if(gtk_tree_path_up(path) && gtk_tree_path_get_depth(path)>1) gtk_tree_view_set_cursor(g->tree,path,NULL,FALSE);
  gtk_tree_path_free(path);return TRUE;
 }
 if(event->keyval==GDK_KEY_Down || event->keyval==GDK_KEY_Right) row=row<0 ? 0 : MIN(row+1,count-1);
 else if(event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_Left) row=row<0 ? count-1 : MAX(0,MIN(row-1,count-1));
 else if(event->keyval==GDK_KEY_Home) row=0;else if(event->keyval==GDK_KEY_End) row=count-1;else return FALSE;
 GtkTreePath *path=node_path(g,row);gtk_tree_view_set_cursor(g->tree,path,NULL,FALSE);gtk_tree_path_free(path);reveal(g,row);return TRUE;
}
static void selection_changed(GtkTreeSelection *selection,gpointer data) {
 RelationGraph *g=data;GtkTreeModel *model;GtkTreeIter iter;
 if(!g->pointer_select && gtk_widget_get_mapped(g->canvas) && gtk_tree_selection_get_selected(selection,&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gint row=path_row(g,path);gtk_tree_path_free(path);if(row>=0 && row<count_nodes(g)) reveal(g,row); }
 gtk_widget_queue_draw(g->canvas);
}
static void destroy(GtkWidget *widget,gpointer data) {
 g_object_set_data(G_OBJECT(widget),"insight-graph",NULL);RelationGraph *g=data;end_pan(g);g_signal_handlers_disconnect_by_data(g->model,g);g_signal_handlers_disconnect_by_data(g->query,g);g_signal_handlers_disconnect_by_data(g->tree,g);GtkTreeSelection *selection=gtk_tree_view_get_selection(g->tree);if(selection) g_signal_handlers_disconnect_by_data(selection,g);
 if(g->menu) { gtk_widget_destroy(g->menu);g_object_unref(g->menu); }
 g_object_unref(g->tree);g_object_unref(g->model);g_object_unref(g->query);g_ptr_array_unref(g->paths);g_free(g);
}
static GtkWidget *graph_new(GtkTreeView *tree,GtkEntry *query,gint name,gint file,gint line,gboolean hierarchical) {
 RelationGraph *g=g_new0(RelationGraph,1);g->tree=g_object_ref(tree);g->model=g_object_ref(gtk_tree_view_get_model(tree));g->query=g_object_ref(query);g->name=name;g->file=file;g->line=line;g->scale=1;g->hierarchical=hierarchical;g->paths=g_ptr_array_new_with_free_func((GDestroyNotify)gtk_tree_path_free);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*scroll=gtk_scrolled_window_new(NULL,NULL);
 g->direction=gtk_combo_box_text_new();gtk_widget_set_name(g->direction,hierarchical ? "insight-relation-tree-graph-direction" : "insight-relation-graph-direction");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g->direction),"Horizontal");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g->direction),"Vertical");gtk_combo_box_set_active(GTK_COMBO_BOX(g->direction),0);
 g->zoom=gtk_spin_button_new_with_range(INSIGHT_GRAPH_MIN_ZOOM,INSIGHT_GRAPH_MAX_ZOOM,5);gtk_widget_set_name(g->zoom,hierarchical ? "insight-relation-tree-graph-zoom" : "insight-relation-graph-zoom");gtk_spin_button_set_value(GTK_SPIN_BUTTON(g->zoom),100);gtk_widget_set_tooltip_text(g->zoom,"Graph zoom (%)");
 gtk_box_pack_start(GTK_BOX(bar),g->direction,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(bar),g->zoom,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
 g->scroll=scroll;g->canvas=gtk_drawing_area_new();gtk_widget_set_can_focus(g->canvas,TRUE);gtk_widget_set_name(g->canvas,hierarchical ? "insight-relation-tree-graph" : "insight-relation-graph");gtk_widget_add_events(g->canvas,GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK|GDK_SCROLL_MASK|GDK_SMOOTH_SCROLL_MASK);gtk_widget_set_has_tooltip(g->canvas,TRUE);gtk_container_add(GTK_CONTAINER(scroll),g->canvas);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 g->status=gtk_label_new("");gtk_label_set_line_wrap(GTK_LABEL(g->status),TRUE);gtk_box_pack_start(GTK_BOX(box),g->status,FALSE,FALSE,0);
 g_signal_connect(g->canvas,"unmap",G_CALLBACK(pan_unmap),g);g_signal_connect(g->canvas,"motion-notify-event",G_CALLBACK(pan_motion),g);g_signal_connect(g->canvas,"button-release-event",G_CALLBACK(pan_release),g);g_signal_connect(g->canvas,"grab-broken-event",G_CALLBACK(pan_broken),g);g_signal_connect(g->canvas,"scroll-event",G_CALLBACK(wheel),g);g_signal_connect(g->canvas,"query-tooltip",G_CALLBACK(tooltip),g);
 g_signal_connect(g->canvas,"draw",G_CALLBACK(draw),g);g_signal_connect(g->canvas,"button-press-event",G_CALLBACK(click),g);g_signal_connect(g->canvas,"key-press-event",G_CALLBACK(key),g);g_signal_connect(g->direction,"changed",G_CALLBACK(controls),g);g_signal_connect(g->zoom,"value-changed",G_CALLBACK(controls),g);
 const gchar *signals[]={"row-inserted","row-deleted","row-changed","rows-reordered"};for(guint i=0;i<G_N_ELEMENTS(signals);i++) g_signal_connect_swapped(g->model,signals[i],G_CALLBACK(update),g);
 if(hierarchical) { g_signal_connect_swapped(tree,"row-expanded",G_CALLBACK(update),g);g_signal_connect_swapped(tree,"row-collapsed",G_CALLBACK(update),g); }
 g_signal_connect_swapped(query,"changed",G_CALLBACK(update),g);g_signal_connect(gtk_tree_view_get_selection(tree),"changed",G_CALLBACK(selection_changed),g);g_signal_connect(box,"destroy",G_CALLBACK(destroy),g);g_object_set_data(G_OBJECT(box),"insight-graph",g);
 const gchar *icons[]={"zoom-fit-best","find-location","zoom-original"},*tips[]={"Fit graph (F)","Center selected node (Space)","Actual size (Ctrl+0)"},*names[]={"fit","center","actual"};GCallback actions[]={G_CALLBACK(fit_graph),G_CALLBACK(center_selection),G_CALLBACK(original_zoom)};
 for(guint i=0;i<3;i++) { GtkWidget *button=gtk_button_new_from_icon_name(icons[i],GTK_ICON_SIZE_MENU);gchar *name=g_strdup_printf("insight-relation-%sgraph-%s",hierarchical ? "tree-" : "",names[i]);gtk_widget_set_name(button,name);g_free(name);gtk_widget_set_tooltip_text(button,tips[i]);gtk_box_pack_start(GTK_BOX(bar),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",actions[i],g); }
 GtkWidget *copy=gtk_button_new_with_label("Copy image"),*save=gtk_button_new_with_label("Save PNG…");gtk_box_pack_start(GTK_BOX(bar),copy,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(bar),save,FALSE,FALSE,0);g_signal_connect(copy,"clicked",G_CALLBACK(copy_image),g);g_signal_connect(save,"clicked",G_CALLBACK(save_image),g);update(g);return box;
}

GtkWidget *insight_relation_graph_new(GtkTreeView *tree,GtkEntry *query,gint name,gint file,gint line) { return graph_new(tree,query,name,file,line,FALSE); }
GtkWidget *insight_relation_graph_new_tree(GtkTreeView *tree,GtkEntry *query,gint name,gint file,gint line) { return graph_new(tree,query,name,file,line,TRUE); }
void insight_relation_graph_get_options(GtkWidget *widget,gboolean *vertical,gint *zoom) {
 RelationGraph *g=g_object_get_data(G_OBJECT(widget),"insight-graph");if(!g) return;
 if(vertical) *vertical=g->vertical;
 if(zoom) *zoom=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g->zoom));
}
void insight_relation_graph_set_options(GtkWidget *widget,gboolean vertical,gint zoom) {
 RelationGraph *g=g_object_get_data(G_OBJECT(widget),"insight-graph");if(!g) return;
 gtk_combo_box_set_active(GTK_COMBO_BOX(g->direction),vertical ? 1 : 0);gtk_spin_button_set_value(GTK_SPIN_BUTTON(g->zoom),CLAMP(zoom,INSIGHT_GRAPH_MIN_ZOOM,INSIGHT_GRAPH_MAX_ZOOM));
}

void insight_relation_graph_set_query_handler(GtkWidget *widget,InsightRelationGraphQuery handler,gpointer data) {
 RelationGraph *g=g_object_get_data(G_OBJECT(widget),"insight-graph");if(!g) return;
 /* Rebuild a cached menu if the embedding application replaces its handler. */
 if(g->menu) { gtk_widget_destroy(g->menu);g_object_unref(g->menu);g->menu=NULL; }
 g->query_handler=handler;g->query_data=data;
}
