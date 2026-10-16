#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-diff.h"
#include "insight-directory.h"
#include "insight-help.h"
#include "../insight/core/diff.h"
#include <string.h>
#include <gio/gio.h>

static InsightDiffOpen source_opener;
void insight_diff_set_source_opener(InsightDiffOpen opener) { source_opener=opener; }
typedef struct { gchar *text; gint *positions; } Snapshot;
typedef struct { guint begin,end; } MapBlock;
typedef struct {
 GtkWidget *window,*paths[2],*views[3],*status,*undo,*redo,*take[2],*take_all[2],*external;
 GtkWidget *find_entry,*find_side,*find_case,*find_previous,*find_next,*find_status,*numbers;
 GArray *map_blocks[2];guint map_lines[2];
 GtkWidget *scrollers[3],*maps[2],*zoom,*mono,*vertical,*overview,*list_toggle,*wrap_toggle;GtkCssProvider *font_css;
 GtkWidget *compare_button,*cancel_button,*save_button; GCancellable *cancel; gboolean busy;
 GtkWidget *previous,*next,*list_scroll,*sync,*ignore_space,*ignore_eol; GtkAdjustment *adjustments[2]; gboolean scrolling;
 GtkListStore *list; GtkWidget *hunk_view; GtkTextBuffer *output; GtkTreeSelection *selection;
 gchar *sources[2],*loaded_paths[2],*saved;
 InsightDiff diff; GtkTextMark **marks;
 GPtrArray *history; guint position; gboolean changing,user_action,pending;
} Merge;
static GtkTextBuffer *result(Merge *m) { return m->output; }
static gchar *contents(GtkTextBuffer *b) {
 GtkTextIter a,z; gtk_text_buffer_get_bounds(b,&a,&z); return gtk_text_buffer_get_text(b,&a,&z,TRUE);
}
static void snapshot_free(gpointer data) { Snapshot *s=data;g_free(s->text);g_free(s->positions);g_free(s); }
static void state(Merge *m) {
 gtk_widget_set_sensitive(m->undo,!m->busy && m->position>0);
 gtk_widget_set_sensitive(m->redo,!m->busy && m->position+1<m->history->len);
 gchar *text=contents(result(m));
 gboolean dirty=g_strcmp0(text,m->saved)!=0;g_free(text);
 gtk_window_set_title(GTK_WINDOW(m->window),dirty ? "Diff / Merge — unsaved result" : "Diff / Merge");
}
static void record(Merge *m) {
 while(m->history->len>m->position+1) g_ptr_array_remove_index(m->history,m->history->len-1);
 Snapshot *s=g_new0(Snapshot,1);s->text=contents(result(m));s->positions=g_new(gint,2*m->diff.count);
 for(size_t i=0;i<2*m->diff.count;i++) { GtkTextIter it;gtk_text_buffer_get_iter_at_mark(result(m),&it,m->marks[i]);s->positions[i]=gtk_text_iter_get_offset(&it); }
 g_ptr_array_add(m->history,s);m->position=m->history->len-1;
 /* Bound full-buffer history memory to roughly 32 MiB. */
 size_t bytes=0;
 for(guint i=0;i<m->history->len;i++) bytes+=strlen(((Snapshot*)g_ptr_array_index(m->history,i))->text)+2*m->diff.count*sizeof(gint);
 while(bytes>32*1024*1024 && m->history->len>1) {
  Snapshot *first=g_ptr_array_index(m->history,0);bytes-=strlen(first->text)+2*m->diff.count*sizeof(gint);
  g_ptr_array_remove_index(m->history,0);m->position--;
 }
 state(m);
}
static void changed(GtkTextBuffer *buffer,gpointer data) {
 (void)buffer;Merge *m=data;
 if(m->changing) return;
 if(m->user_action) m->pending=TRUE;else record(m);
}
static void action_begin(GtkTextBuffer *buffer,gpointer data) { (void)buffer;((Merge*)data)->user_action=TRUE; }
static void action_end(GtkTextBuffer *buffer,gpointer data) {
 (void)buffer;Merge *m=data;m->user_action=FALSE;
 if(m->pending) { m->pending=FALSE;if(!m->changing) record(m); }
}
static void restore(Merge *m,guint position) {
 Snapshot *s=g_ptr_array_index(m->history,position);m->changing=TRUE;
 gtk_text_buffer_set_text(result(m),s->text,-1);
 for(size_t i=0;i<2*m->diff.count;i++) { GtkTextIter it;gtk_text_buffer_get_iter_at_offset(result(m),&it,s->positions[i]);gtk_text_buffer_move_mark(result(m),m->marks[i],&it); }
 m->position=position;m->changing=FALSE;state(m);
}
static void undo(GtkWidget *w,gpointer data) { (void)w;Merge *m=data;if(m->position) restore(m,m->position-1); }
static void redo(GtkWidget *w,gpointer data) { (void)w;Merge *m=data;if(m->position+1<m->history->len) restore(m,m->position+1); }
static void navigate(Merge *m,gint direction);
static void take(GtkWidget *,gpointer);
static void take_all(GtkWidget *,gpointer);
static void find_text(Merge *m,gboolean backwards);
static void view_options(GtkWidget *,gpointer);
static void edit_source(Merge *,guint);
static void focus_find(Merge *m) {
 GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(m->window));
 for(int i=0;i<3;i++) if(focus==m->views[i]) gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),i);
 gtk_widget_grab_focus(m->find_entry);gtk_editable_select_region(GTK_EDITABLE(m->find_entry),0,-1);
}
static gboolean key(GtkWidget *w,GdkEventKey *event,gpointer data) {
 (void)w;
 Merge *m=data;
 if(event->keyval==GDK_KEY_F3) { find_text(m,(event->state&GDK_SHIFT_MASK)!=0);return TRUE; }
 if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_f || event->keyval==GDK_KEY_F)) { focus_find(m);return TRUE; }
 if(event->keyval==GDK_KEY_Escape && w==m->find_entry) {
  gtk_widget_grab_focus(m->views[gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side))]);return TRUE;
 }
 if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_q || event->keyval==GDK_KEY_Q)) { view_options(NULL,m);return TRUE; }
 if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_e || event->keyval==GDK_KEY_E)) {
  GtkWidget *focus=w==m->window ? gtk_window_get_focus(GTK_WINDOW(m->window)) : w;
  for(guint i=0;i<2;i++) if(focus==m->views[i]) { edit_source(m,i);return TRUE; }
 }
 if(m->busy) return FALSE;
 if((event->state&GDK_MOD1_MASK) && (event->keyval==GDK_KEY_Left || event->keyval==GDK_KEY_Right)) {
  gint side=event->keyval==GDK_KEY_Right;
  if(event->state&GDK_SHIFT_MASK) take_all(m->take_all[side],m);else take(m->take[side],m);return TRUE;
 }
 if(!(event->state&GDK_CONTROL_MASK)) return FALSE;
 if(event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_Down) {
  navigate(data,event->keyval==GDK_KEY_Up ? -1 : 1);return TRUE;
 }
 if(event->keyval==GDK_KEY_z || event->keyval==GDK_KEY_Z) {
  if(event->state&GDK_SHIFT_MASK) redo(NULL,data);else undo(NULL,data);return TRUE;
 }
 if(event->keyval==GDK_KEY_y || event->keyval==GDK_KEY_Y) { redo(NULL,data);return TRUE; }
 return FALSE;
}
static void message(Merge *m,const gchar *text) { gtk_label_set_text(GTK_LABEL(m->status),text); }
static void find_text(Merge *m,gboolean backwards) {
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(m->find_entry));if(!*query) { gtk_label_set_text(GTK_LABEL(m->find_status),"Enter search text.");return; }
 int side=gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side));
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a,z,start;
 if(gtk_text_buffer_get_selection_bounds(buffer,&a,&z)) start=backwards ? a : z;
 else gtk_text_buffer_get_iter_at_mark(buffer,&start,gtk_text_buffer_get_insert(buffer));
 GtkTextSearchFlags flags=GTK_TEXT_SEARCH_TEXT_ONLY;
 if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_case))) flags|=GTK_TEXT_SEARCH_CASE_INSENSITIVE;
 gboolean found=backwards ? gtk_text_iter_backward_search(&start,query,flags,&a,&z,NULL) : gtk_text_iter_forward_search(&start,query,flags,&a,&z,NULL);
 gboolean wrapped=!found;
 if(!found) {
  if(backwards) gtk_text_buffer_get_end_iter(buffer,&start);else gtk_text_buffer_get_start_iter(buffer,&start);
  found=backwards ? gtk_text_iter_backward_search(&start,query,flags,&a,&z,NULL) : gtk_text_iter_forward_search(&start,query,flags,&a,&z,NULL);
 }
 if(found) {
  gtk_text_buffer_select_range(buffer,&a,&z);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&a,0.1,TRUE,0,0.5);
  gchar *label=g_strdup_printf(wrapped ? "Wrapped: line %d" : "Found: line %d",gtk_text_iter_get_line(&a)+1);gtk_label_set_text(GTK_LABEL(m->find_status),label);g_free(label);
 } else gtk_label_set_text(GTK_LABEL(m->find_status),"Text not found.");
}
static void find_clicked(GtkWidget *w,gpointer data) { Merge *m=data;find_text(m,w==m->find_previous); }
static void find_activate(GtkEntry *entry,gpointer data) { (void)entry;find_text(data,FALSE); }
static void gutter_size(GtkTextBuffer *buffer,gpointer data) {
 Merge *m=data;gint lines=gtk_text_buffer_get_line_count(buffer),digits=1;
 while(lines>=10) { digits++;lines/=10; }
 for(int i=0;i<3;i++) if(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i]))==buffer) {
  PangoLayout *layout=gtk_widget_create_pango_layout(m->views[i],"0");gint width;
  pango_layout_get_pixel_size(layout,&width,NULL);g_object_unref(layout);
  gtk_text_view_set_border_window_size(GTK_TEXT_VIEW(m->views[i]),GTK_TEXT_WINDOW_LEFT,
   gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->numbers)) ? digits*width+14 : 0);
  gtk_widget_queue_draw(m->views[i]);
 }
}
static void number_toggle(GtkToggleButton *button,gpointer data) {
 (void)button;Merge *m=data;for(int i=0;i<3;i++) gutter_size(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i])),m);
}
static gboolean draw_numbers(GtkWidget *widget,cairo_t *cr,gpointer data) {
 Merge *m=data;if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->numbers))) return FALSE;
 GtkTextView *view=GTK_TEXT_VIEW(widget);GdkWindow *window=gtk_text_view_get_window(view,GTK_TEXT_WINDOW_LEFT);
 if(!window || !gtk_cairo_should_draw_window(cr,window)) return FALSE;
 cairo_save(cr);gtk_cairo_transform_to_window(cr,widget,window);
 GtkStyleContext *style=gtk_widget_get_style_context(widget);GdkRGBA color;gtk_style_context_get_color(style,gtk_style_context_get_state(style),&color);
 gint width=gdk_window_get_width(window);gtk_render_background(style,cr,0,0,width,gdk_window_get_height(window));gdk_cairo_set_source_rgba(cr,&color);
 GdkRectangle rect;GtkTextIter it;gtk_text_view_get_visible_rect(view,&rect);gtk_text_view_get_line_at_y(view,&it,rect.y,NULL);
 PangoLayout *layout=gtk_widget_create_pango_layout(widget,NULL);
 do {
  gint y,height,x,wy;gtk_text_view_get_line_yrange(view,&it,&y,&height);if(y>rect.y+rect.height) break;
  gchar label[32];g_snprintf(label,sizeof label,"%d",gtk_text_iter_get_line(&it)+1);pango_layout_set_text(layout,label,-1);
  gint text_width;pango_layout_get_pixel_size(layout,&text_width,NULL);
  gtk_text_view_buffer_to_window_coords(view,GTK_TEXT_WINDOW_LEFT,0,y,&x,&wy);
  cairo_move_to(cr,width-text_width-7,wy);pango_cairo_show_layout(cr,layout);
  gint current=gtk_text_iter_get_line(&it);gtk_text_iter_forward_line(&it);
  if(gtk_text_iter_get_line(&it)==current) break;
 } while(TRUE);
 g_object_unref(layout);cairo_restore(cr);return FALSE;
}
static gint selected(Merge *m);
static void map_redraw(Merge *m) { for(gint i=0;i<2;i++) if(m->maps[i]) gtk_widget_queue_draw(m->maps[i]); }
static guint map_line(const gchar *text,gsize *offset,guint *line,gsize target) {
 while(*offset<target) { gchar c=text[(*offset)++];if(c=='\r') { if(*offset<target && text[*offset]=='\n') (*offset)++;(*line)++; }else if(c=='\n') (*line)++; }
 return *line;
}
static void map_build(Merge *m) {
 for(gint side=0;side<2;side++) {
  g_clear_pointer(&m->map_blocks[side],g_array_unref);m->map_blocks[side]=g_array_new(FALSE,FALSE,sizeof(MapBlock));gsize offset=0;guint line=0;
  for(gsize i=0;i<m->diff.count;i++) { InsightDiffHunk *h=&m->diff.hunks[i];MapBlock block;block.begin=map_line(m->sources[side],&offset,&line,side ? h->b_begin : h->a_begin);block.end=map_line(m->sources[side],&offset,&line,side ? h->b_end : h->a_end);g_array_append_val(m->map_blocks[side],block); }
  m->map_lines[side]=map_line(m->sources[side],&offset,&line,strlen(m->sources[side]))+1;
 }
 map_redraw(m);
}
static gboolean map_draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 Merge *m=data;gint side=widget==m->maps[1],width=gtk_widget_get_allocated_width(widget),height=gtk_widget_get_allocated_height(widget);gdouble total=MAX(m->map_lines[side],1);
 cairo_set_source_rgb(cr,.94,.95,.97);cairo_paint(cr);
 GArray *blocks=m->map_blocks[side];gint current=selected(m);
 for(guint i=0;blocks && i<blocks->len;i++) { MapBlock b=g_array_index(blocks,MapBlock,i);gdouble y=b.begin/total*height,h=MAX(3.,(b.end-b.begin)/total*height);
  if((gint)i==current) cairo_set_source_rgb(cr,.2,.38,.75);else if(side) cairo_set_source_rgb(cr,.3,.65,.4);else cairo_set_source_rgb(cr,.85,.5,.25);
  cairo_rectangle(cr,3,y,MAX(1,width-6),h);cairo_fill(cr);
 }
 GtkAdjustment *a=m->adjustments[side];gdouble extent=gtk_adjustment_get_upper(a)-gtk_adjustment_get_lower(a);
 if(extent>0) { gdouble y=(gtk_adjustment_get_value(a)-gtk_adjustment_get_lower(a))/extent*height,h=MAX(4.,gtk_adjustment_get_page_size(a)/extent*height);cairo_set_source_rgba(cr,.12,.2,.35,.7);cairo_set_line_width(cr,1.);cairo_rectangle(cr,.5,y+.5,MAX(1,width-1),MAX(1,MIN(h,height-y)-1));cairo_stroke(cr); }
 return FALSE;
}
static gboolean map_click(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 Merge *m=data;if(event->button!=1 || m->busy || !m->sources[0]) return FALSE;gint side=widget==m->maps[1];gdouble height=MAX(gtk_widget_get_allocated_height(widget),1),total=MAX(m->map_lines[side],1),y=CLAMP(event->y,0.,height);
 GArray *blocks=m->map_blocks[side];
 for(guint i=0;blocks && i<blocks->len;i++) { MapBlock b=g_array_index(blocks,MapBlock,i);gdouble top=b.begin/total*height,end=MAX(top+3.,b.end/total*height);
  if(y>=top-2 && y<=end+2) { GtkTreePath *path=gtk_tree_path_new_from_indices(i,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view),path,NULL,FALSE);gtk_tree_path_free(path);map_redraw(m);return TRUE; }
 }
 GtkTextIter iter;GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));gtk_text_buffer_get_iter_at_line(buffer,&iter,CLAMP((gint)(y/height*total),0,gtk_text_buffer_get_line_count(buffer)-1));gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&iter,0.,TRUE,0.,.5);return TRUE;
}
static void map_adjustment(GtkAdjustment *adjustment,gpointer data) { (void)adjustment;map_redraw(data); }
static void display_changed(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;gint zoom=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom));
 gchar size[G_ASCII_DTOSTR_BUF_SIZE];g_ascii_dtostr(size,sizeof size,10.0*zoom/100.0);gchar *css=g_strdup_printf("textview { font-size: %spt; }",size);gtk_css_provider_load_from_data(m->font_css,css,-1,NULL);g_free(css);
 for(gint i=0;i<3;i++) {
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(m->views[i]),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->mono)));
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m->scrollers[i]),GTK_POLICY_AUTOMATIC,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->vertical)) ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER);
 }
 number_toggle(NULL,m);
 for(gint i=0;i<2;i++) if(m->maps[i]) gtk_widget_set_visible(m->maps[i],gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->overview)));
}
static gboolean zoom_scroll(GtkWidget *widget,GdkEventScroll *event,gpointer data) {
 (void)widget;if(!(event->state&GDK_CONTROL_MASK)) return FALSE;Merge *m=data;gdouble delta=0;
 if(event->direction==GDK_SCROLL_UP) delta=10;else if(event->direction==GDK_SCROLL_DOWN) delta=-10;else if(event->direction==GDK_SCROLL_SMOOTH) delta=event->delta_y<0 ? 10 : event->delta_y>0 ? -10 : 0;
 if(delta) gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),gtk_spin_button_get_value(GTK_SPIN_BUTTON(m->zoom))+delta);
 return TRUE;
}
static void view_options(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;GtkWidget *window=m->window;g_object_ref(window);
 GtkWidget *dialog=gtk_dialog_new_with_buttons("File Compare Options",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL);gtk_widget_set_name(dialog,"insight-diff-options");
 g_object_ref(dialog);GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_container_set_border_width(GTK_CONTAINER(box),10);
 GtkWidget *sources[]={m->mono,m->numbers,m->list_toggle,m->wrap_toggle,m->sync,m->vertical,m->overview};GtkWidget *checks[G_N_ELEMENTS(sources)];
 for(guint i=0;i<G_N_ELEMENTS(sources);i++) { checks[i]=gtk_check_button_new_with_label(gtk_button_get_label(GTK_BUTTON(sources[i])));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checks[i]),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sources[i])));gtk_box_pack_start(GTK_BOX(box),checks[i],FALSE,FALSE,2);gchar *name=g_strdup_printf("insight-diff-options-%u",i);gtk_widget_set_name(checks[i],name);g_free(name); }
 GtkWidget *scale=gtk_spin_button_new_with_range(50,250,10);gtk_spin_button_set_value(GTK_SPIN_BUTTON(scale),gtk_spin_button_get_value(GTK_SPIN_BUTTON(m->zoom)));gtk_widget_set_name(scale,"insight-diff-options-zoom");gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Scale (%)"),FALSE,FALSE,2);gtk_box_pack_start(GTK_BOX(box),scale,FALSE,FALSE,2);gtk_widget_show_all(dialog);
 gint response=gtk_dialog_run(GTK_DIALOG(dialog));m=g_object_get_data(G_OBJECT(window),"insight-diff-state");
 if(response==GTK_RESPONSE_APPLY && m) { for(guint i=0;i<G_N_ELEMENTS(sources);i++) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sources[i]),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[i])));gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),gtk_spin_button_get_value(GTK_SPIN_BUTTON(scale))); }
 gtk_widget_destroy(dialog);g_object_unref(dialog);g_object_unref(window);
}
static gboolean discard(Merge *m) {
 gchar *text=contents(result(m));gboolean dirty=g_strcmp0(text,m->saved)!=0;g_free(text);
 if(!dirty) return TRUE;
 GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(m->window),GTK_DIALOG_MODAL,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"Discard unsaved merge result?");
 gtk_dialog_add_buttons(GTK_DIALOG(d),"Cancel",GTK_RESPONSE_CANCEL,"Discard",GTK_RESPONSE_ACCEPT,NULL);
 gboolean ok=gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT;gtk_widget_destroy(d);return ok;
}
static gboolean closing(GtkWidget *w,GdkEvent *event,gpointer data) { (void)w;(void)event;return !discard(data); }
static void clear_diff(Merge *m) {
 for(size_t i=0;i<2*m->diff.count;i++) gtk_text_buffer_delete_mark(result(m),m->marks[i]);
 g_clear_pointer(&m->marks,g_free);insight_diff_clear(&m->diff);
 g_ptr_array_set_size(m->history,0);m->position=0;
}
static void destroyed(GtkWidget *w,gpointer data) {
 (void)w;Merge *m=data;
 g_object_set_data(G_OBJECT(m->window),"insight-diff-state",NULL);
 if(m->cancel) g_cancellable_cancel(m->cancel);
 g_clear_object(&m->cancel);
 /* Child text widgets can already be destroyed here; their buffer owns the marks. */
 g_signal_handlers_disconnect_by_data(m->output,m);g_signal_handlers_disconnect_by_data(m->selection,m);
 for(int i=0;i<2;i++) { g_signal_handlers_disconnect_by_data(m->adjustments[i],m);g_object_unref(m->adjustments[i]); }
 g_object_unref(m->output);g_object_unref(m->selection);
 g_free(m->marks);insight_diff_clear(&m->diff);g_ptr_array_free(m->history,TRUE);
 for(int i=0;i<2;i++) { g_free(m->sources[i]);g_free(m->loaded_paths[i]); }
 for(gint i=0;i<2;i++) { g_clear_pointer(&m->map_blocks[i],g_array_unref);g_clear_object(&m->maps[i]); }
 g_clear_object(&m->font_css);g_free(m->saved);g_object_unref(m->list);g_free(m);
}
static gint selected(Merge *m) {
 GtkTreeIter it;GtkTreeModel *model;gint id=-1;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->hunk_view)),&model,&it)) gtk_tree_model_get(model,&it,0,&id,-1);
 return id;
}
static void navigate(Merge *m,gint direction) {
 if(!m->diff.count) return;
 gint id=selected(m);
 id=id<0 ? (direction>0 ? 0 : (gint)m->diff.count-1) : id+direction;
 if(id<0 || (size_t)id>=m->diff.count) return;
 GtkTreePath *path=gtk_tree_path_new_from_indices(id,-1);
 gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view),path,NULL,FALSE);
 gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(m->hunk_view),path,NULL,FALSE,0,0);
 gtk_tree_path_free(path);
}
static void step(GtkWidget *w,gpointer data) { Merge *m=data;navigate(m,w==m->next ? 1 : -1); }
static void show_list(GtkToggleButton *w,gpointer data) { gtk_widget_set_visible(((Merge*)data)->list_scroll,gtk_toggle_button_get_active(w)); }
static void wrap(GtkToggleButton *w,gpointer data) {
 Merge *m=data;
 for(int i=0;i<3;i++) gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(m->views[i]),gtk_toggle_button_get_active(w) ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
}
static void sync_scroll(GtkAdjustment *adjustment,gpointer data) {
 Merge *m=data;
 if(m->scrolling || !m->sources[0] || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->sync))) return;
 m->scrolling=TRUE;
 int side=adjustment==m->adjustments[1];
 GdkRectangle rect;GtkTextIter it;
 gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(m->views[side]),&rect);
 gtk_text_view_get_line_at_y(GTK_TEXT_VIEW(m->views[side]),&it,rect.y,NULL);
 size_t line=insight_diff_map_line(&m->diff,side,m->sources[side],m->sources[1-side],(size_t)gtk_text_iter_get_line(&it));
 GtkTextBuffer *target=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[1-side]));
 gtk_text_buffer_get_iter_at_line(target,&it,(gint)line);
 gint y,height;gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(m->views[1-side]),&it,&y,&height);
 /* Adjustment updates are synchronous: no deferred scroll callback can feed
  * back into the source after the recursion guard has been released. */
 gtk_adjustment_set_value(m->adjustments[1-side],y);
 m->scrolling=FALSE;
}
static gboolean pick_block(GtkWidget *w,GdkEventButton *event,gpointer data) {
 Merge *m=data;if(event->button!=1 || !m->diff.count) return FALSE;
 int side=w==m->views[1];gint x,y;GtkTextIter it;
 gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(w),gtk_text_view_get_window_type(GTK_TEXT_VIEW(w),event->window),(gint)event->x,(gint)event->y,&x,&y);
 gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(w),&it,x,y);
 size_t offset=(size_t)(g_utf8_offset_to_pointer(m->sources[side],gtk_text_iter_get_offset(&it))-m->sources[side]);
 for(size_t i=0;i<m->diff.count;i++) {
  InsightDiffHunk *h=&m->diff.hunks[i];size_t a=side ? h->b_begin : h->a_begin,z=side ? h->b_end : h->a_end;
  if(offset>=a && (offset<z || (a==z && offset==a))) {
   GtkTreePath *path=gtk_tree_path_new_from_indices((gint)i,-1);
   gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view),path,NULL,FALSE);gtk_tree_path_free(path);return TRUE;
  }
 }
 return FALSE;
}
static gint chars(const gchar *s,size_t offset) { return (gint)g_utf8_pointer_to_offset(s,s+offset); }
static void highlight(Merge *m,int side,size_t begin,size_t end) {
 GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a,z;
 gtk_text_buffer_get_iter_at_offset(b,&a,chars(m->sources[side],begin));gtk_text_buffer_get_iter_at_offset(b,&z,chars(m->sources[side],end));
 gtk_text_buffer_apply_tag_by_name(b,"difference",&a,&z);
}
static void selection(GtkTreeSelection *s,gpointer data) {
 (void)s;Merge *m=data;if(m->changing) return;gint id=selected(m);
 for(int side=0;side<2;side++) {
  gtk_widget_set_sensitive(m->take[side],!m->busy && id>=0);
  gtk_widget_set_sensitive(m->take_all[side],!m->busy && m->diff.count>0);
 }
 gtk_widget_set_sensitive(m->previous,m->diff.count && id!=0);
 gtk_widget_set_sensitive(m->next,m->diff.count && (id<0 || (size_t)(id+1)<m->diff.count));
 if(id<0 || (size_t)id>=m->diff.count) return;
 InsightDiffHunk *h=&m->diff.hunks[id];
 m->scrolling=TRUE;
 for(int side=0;side<3;side++) {
  GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a,z;
  if(side==2) { gtk_text_buffer_get_iter_at_mark(b,&a,m->marks[2*id]);gtk_text_buffer_get_iter_at_mark(b,&z,m->marks[2*id+1]); }
  else { gtk_text_buffer_get_iter_at_offset(b,&a,chars(m->sources[side],side ? h->b_begin : h->a_begin));gtk_text_buffer_get_iter_at_offset(b,&z,chars(m->sources[side],side ? h->b_end : h->a_end)); }
  gtk_text_buffer_select_range(b,&a,&z);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&a,0.1,TRUE,0.0,0.5);
 }
 m->scrolling=FALSE;
 gchar *label=g_strdup_printf("Difference %d of %zu",id+1,m->diff.count);message(m,label);g_free(label);
}
static void edit_source(Merge *m,guint side) {
 if(side>1 || m->busy || !source_opener || !m->loaded_paths[side]) return;
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter iter;gtk_text_buffer_get_iter_at_mark(buffer,&iter,gtk_text_buffer_get_insert(buffer));guint line=gtk_text_iter_get_line(&iter)+1;gchar *path=g_strdup(m->loaded_paths[side]);source_opener(path,line);g_free(path);
}
static void copy_difference(Merge *m,guint side) {
 gint id=selected(m);if(side>1 || m->busy || m->changing || id<0 || (gsize)id>=m->diff.count) return;
 InsightDiffHunk *h=&m->diff.hunks[id];gsize begin=side ? h->b_begin : h->a_begin,end=side ? h->b_end : h->a_end;
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),m->sources[side]+begin,(gint)(end-begin));
}
static void source_menu_action(GtkMenuItem *item,gpointer window) {
 Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");if(!m) return;
 gint side=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"side"));gint action=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"action"));
 if(action==0) edit_source(m,side);else if(action==1) copy_difference(m,side);else view_options(GTK_WIDGET(item),m);
}
static void view_menu_toggle(GtkCheckMenuItem *item,gpointer toggle) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle),gtk_check_menu_item_get_active(item)); }
static void source_menu(GtkTextView *view,GtkWidget *menu,gpointer data) {
 Merge *m=data;guint side=GTK_WIDGET(view)==m->views[1] ? 1 : GTK_WIDGET(view)==m->views[0] ? 0 : 2;
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
 const gchar *labels[]={"Edit File — Ctrl+E","Copy selected difference","File Compare Options…"};
 for(guint action=0;action<3;action++) {
  if(side==2 && action<2) continue;
  GtkWidget *item=gtk_menu_item_new_with_label(labels[action]);g_object_set_data(G_OBJECT(item),"side",GUINT_TO_POINTER(side));g_object_set_data(G_OBJECT(item),"action",GUINT_TO_POINTER(action));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect_object(item,"activate",G_CALLBACK(source_menu_action),m->window,0);
  if(action<2) gtk_widget_set_sensitive(item,!m->busy && m->loaded_paths[side] && (action==0 ? source_opener!=NULL : selected(m)>=0));
 }
 GtkWidget *toggles[]={m->mono,m->numbers,m->list_toggle,m->wrap_toggle,m->vertical,m->overview};
 for(guint i=0;i<G_N_ELEMENTS(toggles);i++) { GtkWidget *item=gtk_check_menu_item_new_with_label(gtk_button_get_label(GTK_BUTTON(toggles[i])));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggles[i])));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect_object(item,"toggled",G_CALLBACK(view_menu_toggle),toggles[i],0); }
 gtk_widget_show_all(menu);
}
static void take(GtkWidget *w,gpointer data) {
 Merge *m=data;gint id=selected(m);if(m->busy || id<0 || (size_t)id>=m->diff.count) return;
 int side=w==m->take[1];InsightDiffHunk *h=&m->diff.hunks[id];
 size_t begin=side?h->b_begin:h->a_begin,end=side?h->b_end:h->a_end;
 GtkTextIter a,z;gtk_text_buffer_get_iter_at_mark(result(m),&a,m->marks[2*id]);gtk_text_buffer_get_iter_at_mark(result(m),&z,m->marks[2*id+1]);
 gchar *current=gtk_text_buffer_get_text(result(m),&a,&z,TRUE);
 gboolean same=strlen(current)==end-begin && !memcmp(current,m->sources[side]+begin,end-begin);g_free(current);
 if(same) return;
 m->changing=TRUE;gtk_text_buffer_begin_user_action(result(m));
 gtk_text_buffer_delete(result(m),&a,&z);gtk_text_buffer_insert(result(m),&a,m->sources[side]+begin,(gint)(end-begin));
 gtk_text_buffer_end_user_action(result(m));m->changing=FALSE;record(m);selection(NULL,m);
}
/* Snapshot every range before editing. Reverse replacement leaves earlier
 * offsets stable; touching/collapsed boundaries after manual edits are rejected. */
static void take_all(GtkWidget *widget,gpointer data) {
 Merge *m=data;if(m->busy || !m->diff.count) return;
 gint side=widget==m->take_all[1];gint *ranges=g_new(gint,2*m->diff.count);gboolean any=FALSE;
 for(size_t i=0;i<m->diff.count;i++) {
  GtkTextIter a,z;gtk_text_buffer_get_iter_at_mark(result(m),&a,m->marks[2*i]);gtk_text_buffer_get_iter_at_mark(result(m),&z,m->marks[2*i+1]);
  ranges[2*i]=gtk_text_iter_get_offset(&a);ranges[2*i+1]=gtk_text_iter_get_offset(&z);
  if(ranges[2*i]>ranges[2*i+1] || (i && ranges[2*i]<=ranges[2*i-1])) {
   g_free(ranges);message(m,"Block boundaries overlap or touch after edits; compare again before taking all blocks.");return;
  }
  InsightDiffHunk *h=&m->diff.hunks[i];size_t begin=side ? h->b_begin : h->a_begin,end=side ? h->b_end : h->a_end;
  gchar *text=gtk_text_buffer_get_text(result(m),&a,&z,TRUE);
  if(strlen(text)!=end-begin || memcmp(text,m->sources[side]+begin,end-begin)) any=TRUE;
  g_free(text);
 }
 if(!any) { g_free(ranges);return; }
 m->changing=TRUE;gtk_text_buffer_begin_user_action(result(m));
 for(size_t i=m->diff.count;i-->0;) {
  InsightDiffHunk *h=&m->diff.hunks[i];size_t begin=side ? h->b_begin : h->a_begin,end=side ? h->b_end : h->a_end;
  GtkTextIter a,z;gtk_text_buffer_get_iter_at_offset(result(m),&a,ranges[2*i]);gtk_text_buffer_get_iter_at_offset(result(m),&z,ranges[2*i+1]);
  gtk_text_buffer_delete(result(m),&a,&z);gtk_text_buffer_insert(result(m),&a,m->sources[side]+begin,(gint)(end-begin));
 }
 gtk_text_buffer_end_user_action(result(m));m->changing=FALSE;g_free(ranges);record(m);selection(NULL,m);
}
static void choose(GtkWidget *w,gpointer data) {
 Merge *m=data;int side=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),"side"));
 GtkWidget *d=gtk_file_chooser_dialog_new("Choose source file",GTK_WINDOW(m->window),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT) { gchar *p=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));GError *error=NULL;gchar *text=g_filename_to_utf8(p,-1,NULL,NULL,&error);if(text) gtk_entry_set_text(GTK_ENTRY(m->paths[side]),text);else message(m,error->message);g_clear_error(&error);g_free(text);g_free(p); }
 gtk_widget_destroy(d);
}
typedef struct { gchar *paths[2],*text[2]; unsigned options;InsightDiff diff; } Comparison;
static void comparison_free(gpointer data) {
 Comparison *job=data;for(int i=0;i<2;i++) { g_free(job->paths[i]);g_free(job->text[i]); }
 insight_diff_clear(&job->diff);g_free(job);
}
static int comparison_cancelled(void *data) { return g_cancellable_is_cancelled(data); }
static void comparison_worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;Comparison *job=data;GError *error=NULL;
 for(int i=0;i<2;i++) {
  GFile *file=g_file_new_for_path(job->paths[i]);
  GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NONE,cancel,&error);
  if(!info) { g_object_unref(file);goto failed; }
  gboolean regular=g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR;g_object_unref(info);
  if(!regular) { g_object_unref(file);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Choose regular UTF-8 text files.");goto failed; }
  GFileInputStream *input=g_file_read(file,cancel,&error);g_object_unref(file);if(!input) goto failed;
  const gsize limit=1024*1024;gsize len=0;job->text[i]=g_malloc(limit+2);
  gboolean ok=g_input_stream_read_all(G_INPUT_STREAM(input),job->text[i],limit+1,&len,cancel,&error);
  g_object_unref(input);if(!ok) goto failed;
  if(len>limit || memchr(job->text[i],0,len) || !g_utf8_validate(job->text[i],len,NULL)) {
   g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Supports UTF-8 text files up to 1 MiB, without NUL bytes.");goto failed;
  }
  job->text[i][len]=0;
 }
 int rc=insight_diff_compute_cancellable(job->text[0],job->text[1],job->options,comparison_cancelled,cancel,&job->diff);
 if(rc) {
  g_set_error_literal(&error,G_IO_ERROR,rc==-3 ? G_IO_ERROR_CANCELLED : G_IO_ERROR_FAILED,
   rc==-3 ? "Comparison cancelled." : rc==-2 ? "Too many changed lines (4 million LCS cells)." : "Cannot allocate comparison.");goto failed;
 }
 g_task_return_boolean(task,TRUE);return;
failed:
 g_task_return_error(task,error);
}
static void busy(Merge *m,gboolean active) {
 m->busy=active;gtk_widget_set_sensitive(m->save_button,!active);gtk_widget_set_sensitive(m->compare_button,!active);gtk_widget_set_sensitive(m->cancel_button,active);
 gtk_widget_set_sensitive(m->views[2],!active);
 for(int i=0;i<2;i++) gtk_widget_set_sensitive(gtk_widget_get_parent(m->paths[i]),!active);
 gtk_widget_set_sensitive(m->ignore_space,!active);gtk_widget_set_sensitive(m->ignore_eol,!active);
 state(m);selection(NULL,m);
}
static void comparison_done(GObject *source,GAsyncResult *async,gpointer data) {
 (void)data;GTask *task=G_TASK(async);GError *error=NULL;
 gboolean ok=g_task_propagate_boolean(task,&error);
 Merge *m=g_object_get_data(source,"insight-diff-state");
 if(!m) { g_clear_error(&error);return; }
 g_clear_object(&m->cancel);busy(m,FALSE);
 if(!ok) { message(m,error->message);g_clear_error(&error);return; }
 Comparison *job=g_task_get_task_data(task);
 m->changing=TRUE;m->scrolling=TRUE;clear_diff(m);m->diff=job->diff;job->diff=(InsightDiff){0};
 for(int i=0;i<2;i++) {
  g_free(m->sources[i]);m->sources[i]=job->text[i];job->text[i]=NULL;
  g_free(m->loaded_paths[i]);m->loaded_paths[i]=g_strdup(job->paths[i]);
  gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i])),m->sources[i],-1);
 }
 gtk_text_buffer_set_text(result(m),m->sources[0],-1);g_free(m->saved);m->saved=g_strdup(m->sources[0]);
 m->marks=g_new0(GtkTextMark*,2*m->diff.count);gtk_list_store_clear(m->list);
 for(size_t i=0;i<m->diff.count;i++) {
  InsightDiffHunk *h=&m->diff.hunks[i];GtkTextIter a,z;
  gtk_text_buffer_get_iter_at_offset(result(m),&a,chars(m->sources[0],h->a_begin));gtk_text_buffer_get_iter_at_offset(result(m),&z,chars(m->sources[0],h->a_end));
  m->marks[2*i]=gtk_text_buffer_create_mark(result(m),NULL,&a,TRUE);m->marks[2*i+1]=gtk_text_buffer_create_mark(result(m),NULL,&z,FALSE);
  /* Compute the right line independently of UTF-8 byte width. */
  GtkTextIter right;gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[1])),&right,chars(m->sources[1],h->b_begin));
  gchar *label=g_strdup_printf("%zu: Left line %d / Right line %d",i+1,gtk_text_iter_get_line(&a)+1,gtk_text_iter_get_line(&right)+1);
  GtkTreeIter it;gtk_list_store_append(m->list,&it);gtk_list_store_set(m->list,&it,0,(gint)i,1,label,-1);g_free(label);
  highlight(m,0,h->a_begin,h->a_end);highlight(m,1,h->b_begin,h->b_end);
 }
 map_build(m);m->changing=FALSE;m->scrolling=FALSE;record(m);selection(NULL,m);
 gchar *label=g_strdup_printf("%zu differences. Result starts with Left; select a block and take Left or Right, or edit the result.",m->diff.count);message(m,label);g_free(label);
 gtk_widget_set_sensitive(m->external,TRUE);
 if(m->diff.count) { GtkTreePath *p=gtk_tree_path_new_first();gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view),p,NULL,FALSE);gtk_tree_path_free(p); }
}
static void compare(GtkWidget *w,gpointer data) {
 (void)w;Merge *m=data;if(m->busy || !discard(m)) return;
 Comparison *job=g_new0(Comparison,1);
 for(int i=0;i<2;i++) {
  GError *error=NULL;gchar *native=g_filename_from_utf8(gtk_entry_get_text(GTK_ENTRY(m->paths[i])),-1,NULL,NULL,&error);
  if(!native || !*native) { message(m,error ? error->message : "Choose two source files before comparing.");g_clear_error(&error);g_free(native);comparison_free(job);return; }
  job->paths[i]=g_canonicalize_filename(native,NULL);g_free(native);
 }
 job->options=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->ignore_space)) ? INSIGHT_DIFF_IGNORE_SPACE : 0) |
  (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->ignore_eol)) ? INSIGHT_DIFF_IGNORE_EOL : 0);
 m->cancel=g_cancellable_new();busy(m,TRUE);message(m,"Comparing… Cancel keeps the previous result.");
 GTask *task=g_task_new(m->window,m->cancel,comparison_done,NULL);
 g_task_set_task_data(task,job,comparison_free);g_task_run_in_thread(task,comparison_worker);g_object_unref(task);
}
static void cancel_compare(GtkWidget *w,gpointer data) {
 (void)w;Merge *m=data;if(m->cancel) g_cancellable_cancel(m->cancel);
}
static void save(GtkWidget *w,gpointer data) {
 (void)w;Merge *m=data;GtkWidget *d=gtk_file_chooser_dialog_new("Save merge result",GTK_WINDOW(m->window),GTK_FILE_CHOOSER_ACTION_SAVE,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,NULL);
 gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d),TRUE);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d),"merged.txt");
 if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d)),*text=contents(result(m));GError *error=NULL;
  if(g_file_set_contents(path,text,-1,&error)) { g_free(m->saved);m->saved=g_strdup(text);message(m,"Result saved.");state(m); }
  else { message(m,error->message);g_clear_error(&error); }
  g_free(path);g_free(text);
 }
 gtk_widget_destroy(d);
}
static void external(GtkWidget *w,gpointer data) {
 (void)w;Merge *m=data;gchar *program=g_find_program_in_path("kdiff3");
 if(!program) { message(m,"KDiff3 is not installed. Internal comparison and merge remain available.");return; }
 gchar *paths[2];
 for(int i=0;i<2;i++) {
  const gchar *entry=gtk_entry_get_text(GTK_ENTRY(m->paths[i]));
  if(!*entry || !g_file_test(entry,G_FILE_TEST_IS_REGULAR)) { message(m,"Choose two existing files before launching KDiff3.");g_free(program);return; }
 }
 for(int i=0;i<2;i++) paths[i]=g_canonicalize_filename(gtk_entry_get_text(GTK_ENTRY(m->paths[i])),NULL);
 gchar *argv[]={program,paths[0],paths[1],NULL};GError *error=NULL;
 if(!g_spawn_async(NULL,argv,NULL,0,NULL,NULL,NULL,&error)) { message(m,error->message);g_clear_error(&error); }
 g_free(paths[0]);g_free(paths[1]);g_free(program);
}
static GtkWidget *button(GtkWidget *box,const gchar *label,GCallback callback,Merge *m) {
 GtkWidget *w=gtk_button_new_with_label(label);gtk_box_pack_start(GTK_BOX(box),w,FALSE,FALSE,0);g_signal_connect(w,"clicked",callback,m);return w;
}
static GtkWidget *scroll(GtkWidget *child) {
 GtkWidget *w=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(w),child);return w;
}
static void show_directory(GtkWidget *widget,gpointer data) { (void)widget;Merge *m=data;insight_directory_show(m->window); }
static Merge *merge_new(GtkWidget *parent) {
 Merge *m=g_new0(Merge,1);m->history=g_ptr_array_new_with_free_func(snapshot_free);m->saved=g_strdup("");
 m->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(m->window,"insight-diff-window");gtk_window_set_transient_for(GTK_WINDOW(m->window),GTK_WINDOW(parent));gtk_window_set_destroy_with_parent(GTK_WINDOW(m->window),TRUE);gtk_window_set_default_size(GTK_WINDOW(m->window),1100,700);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(m->window),box);insight_panel_help_attach(box,"internal-diff.md");
 for(int i=0;i<2;i++) {
  GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(row),gtk_label_new(i ? "Right" : "Left"),FALSE,FALSE,0);
  m->paths[i]=gtk_entry_new();gtk_box_pack_start(GTK_BOX(row),m->paths[i],TRUE,TRUE,0);
  GtkWidget *browse=button(row,"Browse…",G_CALLBACK(choose),m);g_object_set_data(G_OBJECT(browse),"side",GINT_TO_POINTER(i));
 }
 GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
 m->compare_button=button(bar,"Compare",G_CALLBACK(compare),m);gtk_widget_set_name(m->compare_button,"insight-diff-compare");m->cancel_button=button(bar,"Cancel comparison",G_CALLBACK(cancel_compare),m);gtk_widget_set_sensitive(m->cancel_button,FALSE);m->take[0]=button(bar,"Take Left",G_CALLBACK(take),m);m->take[1]=button(bar,"Take Right",G_CALLBACK(take),m);
 m->take_all[0]=button(bar,"All Left",G_CALLBACK(take_all),m);m->take_all[1]=button(bar,"All Right",G_CALLBACK(take_all),m);
 gtk_widget_set_tooltip_text(m->take_all[0],"Take all difference blocks from Left — one Undo");gtk_widget_set_tooltip_text(m->take_all[1],"Take all difference blocks from Right — one Undo");
 m->undo=button(bar,"Undo",G_CALLBACK(undo),m);m->redo=button(bar,"Redo",G_CALLBACK(redo),m);m->save_button=button(bar,"Save result…",G_CALLBACK(save),m);
 m->external=button(bar,"External KDiff3 (optional)",G_CALLBACK(external),m);
 GtkWidget *options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),options,FALSE,FALSE,0);
 m->previous=button(options,"Previous difference",G_CALLBACK(step),m);m->next=button(options,"Next difference",G_CALLBACK(step),m);
 m->list_toggle=gtk_check_button_new_with_label("Difference list");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->list_toggle),TRUE);gtk_box_pack_start(GTK_BOX(options),m->list_toggle,FALSE,FALSE,0);
 m->wrap_toggle=gtk_check_button_new_with_label("Wrap lines");gtk_box_pack_start(GTK_BOX(options),m->wrap_toggle,FALSE,FALSE,0);
 m->sync=gtk_check_button_new_with_label("Scroll sources together");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->sync),TRUE);gtk_box_pack_start(GTK_BOX(options),m->sync,FALSE,FALSE,0);
 GtkWidget *comparison=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),comparison,FALSE,FALSE,0);
 m->ignore_space=gtk_check_button_new_with_label("Ignore spaces and tabs");gtk_box_pack_start(GTK_BOX(comparison),m->ignore_space,FALSE,FALSE,0);
 m->ignore_eol=gtk_check_button_new_with_label("Ignore line ending style");gtk_box_pack_start(GTK_BOX(comparison),m->ignore_eol,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(comparison),gtk_label_new("Apply with Compare; ignored text stays from Left."),FALSE,FALSE,0);
 button(comparison,"Compare directories…",G_CALLBACK(show_directory),m);
 m->numbers=gtk_check_button_new_with_label("Line numbers");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),TRUE);gtk_box_pack_start(GTK_BOX(options),m->numbers,FALSE,FALSE,0);
 GtkWidget *display=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),display,FALSE,FALSE,0);
 m->mono=gtk_check_button_new_with_label("Mono Font View");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mono),TRUE);gtk_box_pack_start(GTK_BOX(display),m->mono,FALSE,FALSE,0);
 m->vertical=gtk_check_button_new_with_label("Vertical scrollbars");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->vertical),TRUE);gtk_box_pack_start(GTK_BOX(display),m->vertical,FALSE,FALSE,0);
 m->overview=gtk_check_button_new_with_label("Overview");gtk_box_pack_start(GTK_BOX(display),m->overview,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(display),gtk_label_new("Scale (%)"),FALSE,FALSE,0);m->zoom=gtk_spin_button_new_with_range(50,250,10);gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),100);gtk_widget_set_name(m->zoom,"insight-diff-zoom");gtk_box_pack_start(GTK_BOX(display),m->zoom,FALSE,FALSE,0);GtkWidget *settings=button(display,"Options…",G_CALLBACK(view_options),m);gtk_widget_set_name(settings,"insight-diff-options-button");m->font_css=gtk_css_provider_new();
 GtkWidget *find=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),find,FALSE,FALSE,0);
 m->find_side=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->find_side),"Left");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->find_side),"Right");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->find_side),"Result");gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),0);gtk_box_pack_start(GTK_BOX(find),m->find_side,FALSE,FALSE,0);
 m->find_entry=gtk_search_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(m->find_entry),"Find text — Ctrl+F, F3 / Shift+F3");gtk_box_pack_start(GTK_BOX(find),m->find_entry,TRUE,TRUE,0);
 m->find_case=gtk_check_button_new_with_label("Match case");gtk_box_pack_start(GTK_BOX(find),m->find_case,FALSE,FALSE,0);
 m->find_previous=button(find,"Find previous",G_CALLBACK(find_clicked),m);m->find_next=button(find,"Find next",G_CALLBACK(find_clicked),m);
 m->find_status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(find),m->find_status,FALSE,FALSE,0);
 g_signal_connect(m->find_entry,"activate",G_CALLBACK(find_activate),m);g_signal_connect(m->find_entry,"key-press-event",G_CALLBACK(key),m);
 GtkWidget *panes=gtk_paned_new(GTK_ORIENTATION_VERTICAL);gtk_box_pack_start(GTK_BOX(box),panes,TRUE,TRUE,0);
 m->list=gtk_list_store_new(2,G_TYPE_INT,G_TYPE_STRING);m->hunk_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(m->list));
 gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m->hunk_view),-1,"Differences",gtk_cell_renderer_text_new(),"text",1,NULL);
 m->selection=g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->hunk_view)));
 g_signal_connect(m->selection,"changed",G_CALLBACK(selection),m);g_signal_connect_swapped(m->selection,"changed",G_CALLBACK(map_redraw),m);
 m->list_scroll=scroll(m->hunk_view);gtk_widget_set_size_request(m->list_scroll,-1,110);gtk_paned_pack1(GTK_PANED(panes),m->list_scroll,FALSE,FALSE);
 GtkWidget *columns=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_set_homogeneous(GTK_BOX(columns),TRUE);gtk_paned_pack2(GTK_PANED(panes),columns,TRUE,FALSE);
 for(int i=0;i<3;i++) {
  GtkWidget *column=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_box_pack_start(GTK_BOX(columns),column,TRUE,TRUE,0);insight_panel_help_attach(column,i==2 ? "panel-merge-result.md" : "panel-diff-input.md");
  gtk_box_pack_start(GTK_BOX(column),gtk_label_new(i==0 ? "Left (read only)" : i==1 ? "Right (read only)" : "Result (editable)"),FALSE,FALSE,0);
  m->views[i]=gtk_text_view_new();gtk_widget_set_name(m->views[i],i==0 ? "insight-diff-left" : i==1 ? "insight-diff-right" : "insight-diff-result");gtk_text_view_set_monospace(GTK_TEXT_VIEW(m->views[i]),TRUE);gtk_text_view_set_editable(GTK_TEXT_VIEW(m->views[i]),i==2);
  GtkWidget *body=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_box_pack_start(GTK_BOX(column),body,TRUE,TRUE,0);m->scrollers[i]=scroll(m->views[i]);gtk_box_pack_start(GTK_BOX(body),m->scrollers[i],TRUE,TRUE,0);
  if(i<2) { m->maps[i]=gtk_drawing_area_new();gtk_widget_set_size_request(m->maps[i],18,-1);gtk_widget_set_no_show_all(m->maps[i],TRUE);gtk_box_pack_start(GTK_BOX(body),m->maps[i],FALSE,FALSE,0);g_object_ref(m->maps[i]);gtk_widget_add_events(m->maps[i],GDK_BUTTON_PRESS_MASK);gtk_widget_set_tooltip_text(m->maps[i],"Overview: click a marker to select its difference; click elsewhere to scroll.");g_signal_connect(m->maps[i],"draw",G_CALLBACK(map_draw),m);g_signal_connect(m->maps[i],"button-press-event",G_CALLBACK(map_click),m); }

  gtk_style_context_add_provider(gtk_widget_get_style_context(m->views[i]),GTK_STYLE_PROVIDER(m->font_css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);gtk_widget_add_events(m->views[i],GDK_SCROLL_MASK|GDK_SMOOTH_SCROLL_MASK);g_signal_connect(m->views[i],"scroll-event",G_CALLBACK(zoom_scroll),m);
  g_signal_connect(m->views[i],"key-press-event",G_CALLBACK(key),m);g_signal_connect(m->views[i],"populate-popup",G_CALLBACK(source_menu),m);
  g_signal_connect_after(m->views[i],"draw",G_CALLBACK(draw_numbers),m);
  g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i])),"changed",G_CALLBACK(gutter_size),m);
  if(i<2) gtk_text_buffer_create_tag(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i])),"difference","background",i ? "#d5f5d5" : "#ffe0d0","foreground","#202020",NULL);
 }
 for(int i=0;i<2;i++) {
  g_signal_connect(m->views[i],"button-press-event",G_CALLBACK(pick_block),m);
  m->adjustments[i]=g_object_ref(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(m->views[i])));
  g_signal_connect(m->adjustments[i],"value-changed",G_CALLBACK(sync_scroll),m);
 }
 g_signal_connect(m->list_toggle,"toggled",G_CALLBACK(show_list),m);g_signal_connect(m->wrap_toggle,"toggled",G_CALLBACK(wrap),m);
 m->output=g_object_ref(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[2])));
 m->status=gtk_label_new("Choose two UTF-8 files and press Compare.");gtk_label_set_xalign(GTK_LABEL(m->status),0);gtk_label_set_line_wrap(GTK_LABEL(m->status),TRUE);gtk_box_pack_start(GTK_BOX(box),m->status,FALSE,FALSE,0);
 g_signal_connect(result(m),"begin-user-action",G_CALLBACK(action_begin),m);
 g_signal_connect(result(m),"end-user-action",G_CALLBACK(action_end),m);
 g_signal_connect(result(m),"changed",G_CALLBACK(changed),m);g_signal_connect(m->window,"key-press-event",G_CALLBACK(key),m);
 g_signal_connect(m->window,"delete-event",G_CALLBACK(closing),m);g_signal_connect(m->window,"destroy",G_CALLBACK(destroyed),m);
 for(gint i=0;i<2;i++) { g_signal_connect(m->adjustments[i],"value-changed",G_CALLBACK(map_adjustment),m);g_signal_connect(m->adjustments[i],"changed",G_CALLBACK(map_adjustment),m); }
 g_signal_connect(m->numbers,"toggled",G_CALLBACK(number_toggle),m);number_toggle(NULL,m);
 g_object_set_data(G_OBJECT(m->window),"insight-diff-state",m);
 g_signal_connect(m->zoom,"value-changed",G_CALLBACK(display_changed),m);g_signal_connect(m->mono,"toggled",G_CALLBACK(display_changed),m);g_signal_connect(m->vertical,"toggled",G_CALLBACK(display_changed),m);g_signal_connect(m->overview,"toggled",G_CALLBACK(display_changed),m);
 record(m);selection(NULL,m);gtk_widget_show_all(m->window);display_changed(NULL,m);return m;
}

void insight_diff_show(GtkWidget *parent) { (void)merge_new(parent); }

void insight_diff_show_files(GtkWidget *parent,const gchar *left,const gchar *right,guint text_flags) {
 Merge *m=merge_new(parent);const gchar *paths[]={left,right};
 for(guint side=0;side<2;side++) { GError *error=NULL;gchar *text=g_filename_to_utf8(paths[side],-1,NULL,NULL,&error);if(!text) { message(m,error->message);g_clear_error(&error);return; }gtk_entry_set_text(GTK_ENTRY(m->paths[side]),text);g_free(text); }
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_space),(text_flags&INSIGHT_DIFF_IGNORE_SPACE)!=0);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_eol),(text_flags&INSIGHT_DIFF_IGNORE_EOL)!=0);compare(NULL,m);
}
