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
 gint name,file,line;gboolean vertical;gdouble scale;
 GtkWidget *menu;InsightRelationGraphQuery query_handler;gpointer query_data;
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
static cairo_surface_t *image(RelationGraph *g) {
 int width=0,height=0;
 if(!insight_graph_image_size(gtk_tree_model_iter_n_children(g->model,NULL),g->vertical,g->scale,&width,&height)) { gtk_label_set_text(GTK_LABEL(g->status),"Image is too large. Reduce zoom or filter the results before exporting.");return NULL; }
 cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height);
 if(cairo_surface_status(surface)!=CAIRO_STATUS_SUCCESS) { gtk_label_set_text(GTK_LABEL(g->status),cairo_status_to_string(cairo_surface_status(surface)));cairo_surface_destroy(surface);return NULL; }
 cairo_t *cr=cairo_create(surface);GdkRGBA fg;gtk_style_context_get_color(gtk_widget_get_style_context(g->canvas),GTK_STATE_FLAG_NORMAL,&fg);
 double background=fg.red+fg.green+fg.blue>1.5 ? 0.12 : 1.0;cairo_set_source_rgb(cr,background,background,background);cairo_paint(cr);draw(g->canvas,cr,g);
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
static gboolean click(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)widget;RelationGraph *g=data;if((event->button!=1 && event->button!=3) || (event->type!=GDK_BUTTON_PRESS && event->type!=GDK_2BUTTON_PRESS)) return FALSE;
 gint row=insight_graph_hit(event->x/g->scale,event->y/g->scale,gtk_tree_model_iter_n_children(g->model,NULL),g->vertical);if(row<0) return FALSE;
 gtk_widget_grab_focus(g->canvas);GtkTreePath *path=gtk_tree_path_new_from_indices(row,-1);gtk_tree_view_set_cursor(g->tree,path,NULL,FALSE);
 if(event->button==3) gtk_menu_popup_at_pointer(GTK_MENU(node_menu(g)),(GdkEvent *)event);
 else if(event->type==GDK_2BUTTON_PRESS) gtk_tree_view_row_activated(g->tree,path,gtk_tree_view_get_column(g->tree,0));
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
 (void)widget;RelationGraph *g=data;if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_c || event->keyval==GDK_KEY_C)) { copy_image(NULL,g);return TRUE; }
 if(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK)) return FALSE;
 gint count=MIN(gtk_tree_model_iter_n_children(g->model,NULL),INSIGHT_GRAPH_LIMIT);if(!count) return FALSE;
 GtkTreeModel *model;GtkTreeIter iter;gint row=-1;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(g->tree),&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);if(gtk_tree_path_get_depth(path)==1) row=gtk_tree_path_get_indices(path)[0];gtk_tree_path_free(path); }
 if(row>=0 && row<count && g->query_handler) {
  gint mode=-1;
  if(event->keyval==GDK_KEY_d || event->keyval==GDK_KEY_D) mode=1;
  else if(event->keyval==GDK_KEY_c || event->keyval==GDK_KEY_C) mode=7;
  else if(event->keyval==GDK_KEY_r || event->keyval==GDK_KEY_R) mode=0;
  if(mode>=0) { g->query_handler(mode,g->query_data);return TRUE; }
 }
 if(event->keyval==GDK_KEY_Menu || (event->keyval==GDK_KEY_F10 && (event->state&GDK_SHIFT_MASK))) {
  if(row>=0 && row<count && gtk_widget_get_window(g->canvas)) {
   InsightGraphRect r=insight_graph_rect(row,g->vertical);GdkRectangle anchor={(gint)(r.x*g->scale),(gint)(r.y*g->scale),(gint)(r.width*g->scale),(gint)(r.height*g->scale)};
   gtk_menu_popup_at_rect(GTK_MENU(node_menu(g)),gtk_widget_get_window(g->canvas),&anchor,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,(GdkEvent *)event);
  }
  return TRUE;
 }
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
 g_object_set_data(G_OBJECT(widget),"insight-graph",NULL);RelationGraph *g=data;g_signal_handlers_disconnect_by_data(g->model,g);g_signal_handlers_disconnect_by_data(g->query,g);GtkTreeSelection *selection=gtk_tree_view_get_selection(g->tree);if(selection) g_signal_handlers_disconnect_by_data(selection,g);
 if(g->menu) { gtk_widget_destroy(g->menu);g_object_unref(g->menu); }
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
 g_signal_connect_swapped(query,"changed",G_CALLBACK(update),g);g_signal_connect(gtk_tree_view_get_selection(tree),"changed",G_CALLBACK(selection_changed),g);g_signal_connect(box,"destroy",G_CALLBACK(destroy),g);g_object_set_data(G_OBJECT(box),"insight-graph",g);
 GtkWidget *copy=gtk_button_new_with_label("Copy image"),*save=gtk_button_new_with_label("Save PNG…");gtk_box_pack_start(GTK_BOX(bar),copy,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(bar),save,FALSE,FALSE,0);g_signal_connect(copy,"clicked",G_CALLBACK(copy_image),g);g_signal_connect(save,"clicked",G_CALLBACK(save_image),g);update(g);return box;
}

void insight_relation_graph_get_options(GtkWidget *widget,gboolean *vertical,gint *zoom) {
 RelationGraph *g=g_object_get_data(G_OBJECT(widget),"insight-graph");if(!g) return;
 if(vertical) *vertical=g->vertical;
 if(zoom) *zoom=(gint)(g->scale*100);
}
void insight_relation_graph_set_options(GtkWidget *widget,gboolean vertical,gint zoom) {
 RelationGraph *g=g_object_get_data(G_OBJECT(widget),"insight-graph");if(!g) return;
 gtk_combo_box_set_active(GTK_COMBO_BOX(g->direction),vertical ? 1 : 0);gtk_spin_button_set_value(GTK_SPIN_BUTTON(g->zoom),CLAMP(zoom,50,200));
}

void insight_relation_graph_set_query_handler(GtkWidget *widget,InsightRelationGraphQuery handler,gpointer data) {
 RelationGraph *g=g_object_get_data(G_OBJECT(widget),"insight-graph");if(!g) return;
 /* Rebuild a cached menu if the embedding application replaces its handler. */
 if(g->menu) { gtk_widget_destroy(g->menu);g_object_unref(g->menu);g->menu=NULL; }
 g->query_handler=handler;g->query_data=data;
}
