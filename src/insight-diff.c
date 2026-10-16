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
 GtkWidget *find_entry,*find_side,*find_case,*find_word,*find_wrap,*find_previous,*find_next,*find_status,*numbers;
 GtkWidget *replace_entry,*replace_selection,*replace_once,*replace_case,*replace_status,*replace_apply;
 GtkWidget *edit_tab_width;
 GtkWidget *visible_space,*visible_eol;
 GtkWidget *line_side,*line_number,*line_select,*line_status;
 GArray *map_blocks[2];guint map_lines[2];
 GtkWidget *scrollers[3],*maps[2],*zoom,*mono,*vertical,*overview,*list_toggle,*wrap_toggle;GtkCssProvider *font_css;
 GtkWidget *compare_button,*cancel_button,*save_button; GCancellable *cancel; gboolean busy;
 GtkWidget *previous,*next,*list_scroll,*sync,*ignore_space,*ignore_eol; GtkAdjustment *adjustments[2]; gboolean scrolling;
 GtkListStore *list; GtkWidget *hunk_view; GtkTextBuffer *output; GtkTreeSelection *selection;
 gchar *sources[2],*loaded_paths[2],*saved;
 InsightDiff diff; GtkTextMark **marks;
 gboolean search_fields[4];
 GPtrArray *history; guint position;guint64 history_serial; gboolean changing,user_action,pending;
} Merge;
static GKeyFile *view_defaults;
static GList *open_compares;
static const gchar *view_group="file-compare";
static const gchar *view_checks[]={"mono","numbers","list","wrap","sync","vertical","overview","ignore-space","ignore-eol","visible-space","visible-eol"};
static void view_controls(Merge *m,GtkWidget **controls) {
 GtkWidget *items[]={m->mono,m->numbers,m->list_toggle,m->wrap_toggle,m->sync,m->vertical,m->overview,m->ignore_space,m->ignore_eol,m->visible_space,m->visible_eol};for(guint i=0;i<G_N_ELEMENTS(items);i++) controls[i]=items[i];
}
static void view_copy(GKeyFile *from,GKeyFile *to) {
 g_key_file_remove_group(to,view_group,NULL);const gchar *extra[]={"version","zoom","width","height","search-file","search-line","search-column","search-text"};
 for(guint i=0;i<G_N_ELEMENTS(view_checks)+G_N_ELEMENTS(extra);i++) { const gchar *name=i<G_N_ELEMENTS(view_checks) ? view_checks[i] : extra[i-G_N_ELEMENTS(view_checks)];gchar *value=g_key_file_get_value(from,view_group,name,NULL);if(value && strlen(value)<=128) g_key_file_set_value(to,view_group,name,value);g_free(value); }
}
static void view_save(Merge *m,GKeyFile *settings) {
 GtkWidget *controls[G_N_ELEMENTS(view_checks)];view_controls(m,controls);g_key_file_set_integer(settings,view_group,"version",1);
 for(guint i=0;i<G_N_ELEMENTS(view_checks);i++) g_key_file_set_boolean(settings,view_group,view_checks[i],gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls[i])));
 const gchar *fields[]={"search-file","search-line","search-column","search-text"};for(guint i=0;i<4;i++) g_key_file_set_boolean(settings,view_group,fields[i],m->search_fields[i]);
 gint width,height;gtk_window_get_size(GTK_WINDOW(m->window),&width,&height);g_key_file_set_integer(settings,view_group,"width",CLAMP(width,640,4000));g_key_file_set_integer(settings,view_group,"height",CLAMP(height,400,2400));g_key_file_set_integer(settings,view_group,"zoom",gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)));
}
static gboolean view_integer(GKeyFile *settings,const gchar *name,gint low,gint high,gint *out) {
 GError *error=NULL;gint value=g_key_file_get_integer(settings,view_group,name,&error);gboolean ok=!error && value>=low && value<=high;g_clear_error(&error);if(ok) *out=value;return ok;
}
static void view_restore(Merge *m,GKeyFile *settings) {
 if(!settings || g_key_file_get_integer(settings,view_group,"version",NULL)!=1) return;
 GtkWidget *controls[G_N_ELEMENTS(view_checks)];view_controls(m,controls);
 for(guint i=0;i<G_N_ELEMENTS(view_checks);i++) { GError *error=NULL;gboolean value=g_key_file_get_boolean(settings,view_group,view_checks[i],&error);if(!error) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls[i]),value);g_clear_error(&error); }
 const gchar *fields[]={"search-file","search-line","search-column","search-text"};for(guint i=0;i<4;i++) { GError *error=NULL;gboolean value=g_key_file_get_boolean(settings,view_group,fields[i],&error);if(!error) m->search_fields[i]=value;g_clear_error(&error); }
 gint value;if(view_integer(settings,"zoom",50,250,&value)) gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),value);gint width=1100,height=700;view_integer(settings,"width",640,4000,&width);view_integer(settings,"height",400,2400,&height);gtk_window_resize(GTK_WINDOW(m->window),width,height);
}
void insight_diff_read(GKeyFile *settings) { if(!view_defaults) view_defaults=g_key_file_new();view_copy(settings,view_defaults); }
void insight_diff_write(GKeyFile *settings) { if(!view_defaults) view_defaults=g_key_file_new();if(open_compares) view_save(open_compares->data,view_defaults);view_copy(view_defaults,settings); }
static GtkTextBuffer *result(Merge *m) { return m->output; }
static gchar *contents(GtkTextBuffer *b) {
 GtkTextIter a,z; gtk_text_buffer_get_bounds(b,&a,&z); return gtk_text_buffer_get_text(b,&a,&z,TRUE);
}
static void snapshot_free(gpointer data) { Snapshot *s=data;g_free(s->text);g_free(s->positions);g_free(s); }
static void state(Merge *m) {
 if(m->replace_apply) gtk_widget_set_sensitive(m->replace_apply,!m->busy);
 gtk_widget_set_sensitive(m->undo,!m->busy && m->position>0);
 gtk_widget_set_sensitive(m->redo,!m->busy && m->position+1<m->history->len);
 gchar *text=contents(result(m));
 gboolean dirty=g_strcmp0(text,m->saved)!=0;g_free(text);
 gtk_window_set_title(GTK_WINDOW(m->window),dirty ? "Diff / Merge — unsaved result" : "Diff / Merge");
}
static void record(Merge *m) {
 m->history_serial++;
 while(m->history->len>m->position+1) g_ptr_array_remove_index(m->history,m->history->len-1);
 Snapshot *s=g_new0(Snapshot,1);s->text=contents(result(m));s->positions=g_new(gint,2*m->diff.count);
 for(size_t i=0;i<2*m->diff.count;i++) { GtkTextIter it;gtk_text_buffer_get_iter_at_mark(result(m),&it,m->marks[i]);s->positions[i]=gtk_text_iter_get_offset(&it); }
 g_ptr_array_add(m->history,s);m->position=m->history->len-1;
 /* Bound full-buffer history memory to roughly 32 MiB. */
 size_t bytes=0;
 for(guint i=0;i<m->history->len;i++) bytes+=sizeof(Snapshot)+strlen(((Snapshot*)g_ptr_array_index(m->history,i))->text)+1+2*m->diff.count*sizeof(gint);
 while((bytes>32*1024*1024 || m->history->len>512) && m->history->len>1) {
  Snapshot *first=g_ptr_array_index(m->history,0);bytes-=sizeof(Snapshot)+strlen(first->text)+1+2*m->diff.count*sizeof(gint);
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
 m->history_serial++;
 Snapshot *s=g_ptr_array_index(m->history,position);m->changing=TRUE;
 gtk_text_buffer_set_text(result(m),s->text,-1);
 for(size_t i=0;i<2*m->diff.count;i++) { GtkTextIter it;gtk_text_buffer_get_iter_at_offset(result(m),&it,s->positions[i]);gtk_text_buffer_move_mark(result(m),m->marks[i],&it); }
 m->position=position;m->changing=FALSE;state(m);
}
static void undo(GtkWidget *w,gpointer data) { (void)w;Merge *m=data;if(!m->busy && m->position) restore(m,m->position-1); }
static void redo(GtkWidget *w,gpointer data) { (void)w;Merge *m=data;if(!m->busy && m->position+1<m->history->len) restore(m,m->position+1); }
static void history_end(Merge *m,gboolean redo_all) {
 if(!m->busy && m->history->len) { guint target=redo_all ? m->history->len-1 : 0;if(target!=m->position) restore(m,target); }
}
static void history_preview(GtkTreeSelection *selection,gpointer view) {
 GtkTreeModel *model;GtkTreeIter iter;if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *text=NULL;gtk_tree_model_get(model,&iter,4,&text,-1);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),text ? text : "",-1);g_free(text);
}
static void history_dialog(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;if(m->busy || !m->history->len) return;
 GtkWidget *window=m->window;g_object_ref(window);guint64 serial=m->history_serial;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Result History",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Restore selected state",GTK_RESPONSE_APPLY,NULL);g_object_ref(dialog);gtk_widget_set_name(dialog,"insight-diff-history");gtk_window_set_default_size(GTK_WINDOW(dialog),720,500);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_container_set_border_width(GTK_CONTAINER(box),8);
 GtkWidget *note=gtk_label_new("Select a retained state to preview. Restore keeps later states available for Redo.");gtk_label_set_line_wrap(GTK_LABEL(note),TRUE);gtk_box_pack_start(GTK_BOX(box),note,FALSE,FALSE,4);
 GtkListStore *model=gtk_list_store_new(5,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);
 for(guint i=0;i<m->history->len;i++) {
  Snapshot *snapshot=g_ptr_array_index(m->history,i);gchar *number=g_strdup_printf("%u",i+1),*bytes=g_strdup_printf("%zu",strlen(snapshot->text));
  const gchar *end=g_utf8_offset_to_pointer(snapshot->text,MIN((glong)80,g_utf8_strlen(snapshot->text,-1)));gchar *summary=g_strndup(snapshot->text,end-snapshot->text);for(gchar *p=summary;*p;p++) if(*p=='\r'||*p=='\n'||*p=='\t') *p=' ';
  gboolean saved=!strcmp(snapshot->text,m->saved);const gchar *status=i==m->position ? (saved ? "Current / Saved" : "Current") : (saved ? "Saved" : "");
  gchar *label=g_strconcat(number," — ",insight_tr(status),NULL);GtkTreeIter iter;gtk_list_store_append(model,&iter);gtk_list_store_set(model,&iter,0,i,1,label,2,bytes,3,summary,4,snapshot->text,-1);g_free(label);g_free(number);g_free(bytes);g_free(summary);
 }
 GtkWidget *tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));g_object_unref(model);gtk_widget_set_name(tree,"insight-diff-history-list");
 const gchar *titles[]={"State","Bytes","Text preview"};for(guint i=0;i<3;i++) gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree),-1,titles[i],gtk_cell_renderer_text_new(),"text",i+1,NULL);
 GtkWidget *list_scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(list_scroll),tree);gtk_box_pack_start(GTK_BOX(box),list_scroll,TRUE,TRUE,4);
 GtkWidget *preview=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(preview),FALSE);gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(preview),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(preview),TRUE);gtk_widget_set_name(preview,"insight-diff-history-preview");
 GtkWidget *preview_scroll=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_size_request(preview_scroll,-1,150);gtk_container_add(GTK_CONTAINER(preview_scroll),preview);gtk_box_pack_start(GTK_BOX(box),preview_scroll,TRUE,TRUE,4);
 GtkTreeSelection *selection=g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)));g_signal_connect_object(selection,"changed",G_CALLBACK(history_preview),preview,0);GtkTreePath *path=gtk_tree_path_new_from_indices(m->position,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree),path,NULL,FALSE,0,0);gtk_tree_path_free(path);gtk_widget_show_all(dialog);
 gint response=gtk_dialog_run(GTK_DIALOG(dialog));m=g_object_get_data(G_OBJECT(window),"insight-diff-state");
 if(response==GTK_RESPONSE_APPLY && m && !m->busy && serial==m->history_serial) { GtkTreeModel *selected_model;GtkTreeIter iter;if(gtk_tree_selection_get_selected(selection,&selected_model,&iter)) { guint index;gtk_tree_model_get(selected_model,&iter,0,&index,-1);if(index<m->history->len && index!=m->position) restore(m,index); } }
 if(response==GTK_RESPONSE_APPLY && m && serial!=m->history_serial) gtk_label_set_text(GTK_LABEL(m->replace_status),"History changed; reopen Result History.");
 gtk_widget_destroy(dialog);g_object_unref(selection);g_object_unref(dialog);g_object_unref(window);
}
static void navigate(Merge *m,gint direction);
static void take(GtkWidget *,gpointer);
static void take_all(GtkWidget *,gpointer);
static void find_text(Merge *m,gboolean backwards);
static void view_options(GtkWidget *,gpointer);
static void edit_source(Merge *,guint);
static void focus_find(Merge *m) {
 GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(m->window));
 for(int i=0;i<3;i++) if(focus==m->views[i]) {
  gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),i);
  GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i]));GtkTextIter a,z;
  if(gtk_text_buffer_get_selection_bounds(buffer,&a,&z) && gtk_text_iter_get_offset(&z)-gtk_text_iter_get_offset(&a)<=4096) {
   gchar *selection=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);
   if(!strchr(selection,'\n') && !strchr(selection,'\r')) gtk_entry_set_text(GTK_ENTRY(m->find_entry),selection);
   g_free(selection);
  }
 }
 gtk_widget_grab_focus(m->find_entry);gtk_editable_select_region(GTK_EDITABLE(m->find_entry),0,-1);
}
static gint focused_pane(Merge *m) {
 GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(m->window));
 for(gint side=0;side<3;side++) if(focus==m->views[side]) return side;
 return CLAMP(gtk_combo_box_get_active(GTK_COMBO_BOX(m->line_side)),0,2);
}
static void focus_line(Merge *m,gint side) {
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter at;
 gtk_text_buffer_get_iter_at_mark(buffer,&at,gtk_text_buffer_get_insert(buffer));
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->line_side),side);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->line_number),gtk_text_iter_get_line(&at)+1);
 gtk_widget_grab_focus(m->line_number);gtk_editable_select_region(GTK_EDITABLE(m->line_number),0,-1);
}
static void go_line(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;if(m->busy) return;
 gint side=CLAMP(gtk_combo_box_get_active(GTK_COMBO_BOX(m->line_side)),0,2);
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter at,end;
 gtk_spin_button_update(GTK_SPIN_BUTTON(m->line_number));
 gint line=MIN(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->line_number)),gtk_text_buffer_get_line_count(buffer));
 gtk_text_buffer_get_iter_at_line(buffer,&at,line-1);end=at;
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->line_select))) {
  if(!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);
  gtk_text_buffer_select_range(buffer,&at,&end);
 } else gtk_text_buffer_place_cursor(buffer,&at);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->line_number),line);
 gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&at,0.1,TRUE,0,0.5);gtk_widget_grab_focus(m->views[side]);
 gchar *label=g_strdup_printf("Line %d of %d",line,gtk_text_buffer_get_line_count(buffer));gtk_label_set_text(GTK_LABEL(m->line_status),label);g_free(label);
}
static void line_begin(GtkWidget *widget,gpointer data) { Merge *m=data;gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->line_number),widget==g_object_get_data(G_OBJECT(m->window),"line-first") ? 1 : G_MAXINT);go_line(widget,m); }
static void selection_search(Merge *,guint,gint);
static void find_all(Merge *,guint);
static gboolean key(GtkWidget *w,GdkEventKey *event,gpointer data) {
 (void)w;
 Merge *m=data;
 if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_g || event->keyval==GDK_KEY_G)) { focus_line(m,focused_pane(m));return TRUE; }
 if(event->keyval==GDK_KEY_F6 && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK))) {
  gint side=(focused_pane(m)+((event->state&GDK_SHIFT_MASK) ? 2 : 1))%3;
  gtk_combo_box_set_active(GTK_COMBO_BOX(m->line_side),side);gtk_widget_grab_focus(m->views[side]);return TRUE;
 }
 if(event->keyval==GDK_KEY_Escape && w==m->line_number) { gtk_widget_grab_focus(m->views[focused_pane(m)]);return TRUE; }
 if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_h || event->keyval==GDK_KEY_H)) { gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),2);gtk_widget_grab_focus(m->replace_entry);gtk_editable_select_region(GTK_EDITABLE(m->replace_entry),0,-1);return TRUE; }
 if((event->state&GDK_CONTROL_MASK) && event->keyval==GDK_KEY_F3) { selection_search(m,focused_pane(m),(event->state&GDK_SHIFT_MASK) ? -1 : 1);return TRUE; }
 if((event->state&(GDK_CONTROL_MASK|GDK_SHIFT_MASK))==(GDK_CONTROL_MASK|GDK_SHIFT_MASK) && (event->keyval==GDK_KEY_f || event->keyval==GDK_KEY_F)) { GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(m->window));guint side=gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side));for(guint i=0;i<3;i++) if(focus==m->views[i]) side=i;find_all(m,side);return TRUE; }
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
/* Identifier boundaries deliberately include underscores and Unicode marks. */
static gboolean identifier_char(gunichar c) {
 return c=='_' || g_unichar_isalnum(c) || g_unichar_ismark(c);
}
static gboolean search_match(GtkTextIter start,const gchar *query,GtkTextSearchFlags flags,
 gboolean backwards,gboolean whole,GtkTextIter *a,GtkTextIter *z) {
 while(backwards ? gtk_text_iter_backward_search(&start,query,flags,a,z,NULL) : gtk_text_iter_forward_search(&start,query,flags,a,z,NULL)) {
  GtkTextIter before=*a;gboolean left=gtk_text_iter_backward_char(&before);
  if(!whole || ((!left || !identifier_char(gtk_text_iter_get_char(&before))) &&
     (gtk_text_iter_is_end(z) || !identifier_char(gtk_text_iter_get_char(z))))) return TRUE;
  start=backwards ? *a : *z;
 }
 return FALSE;
}
static void selection_search(Merge *m,guint side,gint direction) {
 if(m->busy || side>2) return;
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a,z,limit;guint scanned=0;
 gboolean selection=gtk_text_buffer_get_selection_bounds(buffer,&a,&limit);
 if(selection) { while(gtk_text_iter_compare(&a,&limit)<0 && !identifier_char(gtk_text_iter_get_char(&a)) && scanned++<4096) gtk_text_iter_forward_char(&a);if(gtk_text_iter_compare(&a,&limit)>=0) return; }
 else { gtk_text_buffer_get_iter_at_mark(buffer,&a,gtk_text_buffer_get_insert(buffer));if(!identifier_char(gtk_text_iter_get_char(&a))) { GtkTextIter before=a;if(!gtk_text_iter_backward_char(&before) || !identifier_char(gtk_text_iter_get_char(&before))) return;a=before; } }
 if(scanned>=4096 || !identifier_char(gtk_text_iter_get_char(&a))) return;
 z=a;
 for(;;) { GtkTextIter before=a;if(!gtk_text_iter_backward_char(&before) || !identifier_char(gtk_text_iter_get_char(&before))) break;if(scanned++>=4096) return;a=before; }
 while(!gtk_text_iter_is_end(&z) && identifier_char(gtk_text_iter_get_char(&z))) { if(scanned++>=4096) return;gtk_text_iter_forward_char(&z); }
 gchar *query=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);gtk_entry_set_text(GTK_ENTRY(m->find_entry),query);g_free(query);gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),side);
 if(direction) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_word),TRUE);gtk_text_buffer_select_range(buffer,&a,&z);find_text(m,direction<0);gtk_widget_grab_focus(m->views[side]); }
}
static void search_result_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer dialog) { (void)view;(void)path;(void)column;gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_APPLY); }
typedef struct { GtkWidget *dialog,*tree,*filter_entry,*fields[4],*status;GtkTreeModelFilter *filter;gchar *fold,*source;gboolean can_edit,closing; } SearchOutput;
static gboolean search_output_visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 SearchOutput *out=data;if(!*out->fold) return TRUE;gchar *text=NULL;gtk_tree_model_get(model,iter,4,&text,-1);gchar *fold=g_utf8_casefold(text ? text : "",-1);gboolean visible=strstr(fold,out->fold)!=NULL;g_free(fold);g_free(text);return visible;
}
static void search_output_selection(GtkTreeSelection *selection,gpointer data) {
 SearchOutput *out=data;if(out->closing) return;GtkTreeIter iter;GtkTreeModel *model;gboolean selected=gtk_tree_selection_get_selected(selection,&model,&iter);gtk_dialog_set_response_sensitive(GTK_DIALOG(out->dialog),GTK_RESPONSE_APPLY,selected);gtk_dialog_set_response_sensitive(GTK_DIALOG(out->dialog),GTK_RESPONSE_YES,selected && out->can_edit);
}
static void search_output_filter(GtkEditable *entry,gpointer data) {
 SearchOutput *out=data;if(out->closing) return;g_free(out->fold);out->fold=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(entry)),-1);gtk_tree_model_filter_refilter(out->filter);gchar *label=g_strdup_printf("Visible matches: %u",gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(out->tree)),NULL));gtk_label_set_text(GTK_LABEL(out->status),label);g_free(label);
}
static void search_output_columns(GtkToggleButton *button,gpointer data) {
 (void)button;SearchOutput *out=data;if(out->closing) return;for(guint i=0;i<3;i++) gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(out->tree),i),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(out->fields[i+1])));
}
static void tsv_field(GString *out,const gchar *text) {
 for(const gchar *p=text;*p;p++) { if(*p=='\t') g_string_append(out,"\\t");else if(*p=='\r') g_string_append(out,"\\r");else if(*p=='\n') g_string_append(out,"\\n");else if(*p=='\\') g_string_append(out,"\\\\");else g_string_append_c(out,*p); }
}
static void search_output_copy(GtkWidget *button,gpointer data) {
 (void)button;SearchOutput *out=data;if(out->closing) return;gboolean fields[4],any=FALSE;for(guint i=0;i<4;i++) { fields[i]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(out->fields[i]));any|=fields[i]; }
 if(!any) { gtk_label_set_text(GTK_LABEL(out->status),"Select at least one output field.");return; }
 GString *text=g_string_new(NULL);const gchar *headers[]={"File","Line","Column","Text"};gboolean first=TRUE;
 for(guint i=0;i<4;i++) if(fields[i]) { if(!first) g_string_append_c(text,'\t');g_string_append(text,headers[i]);first=FALSE; }
  g_string_append_c(text,'\n');
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(out->tree));GtkTreeIter iter;guint rows=0;gboolean exceeded=FALSE;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) {
  guint line,column;gchar *excerpt=NULL;gtk_tree_model_get(model,&iter,2,&line,3,&column,4,&excerpt,-1);gchar *line_text=g_strdup_printf("%u",line),*column_text=g_strdup_printf("%u",column);const gchar *values[]={out->source,line_text,column_text,excerpt ? excerpt : ""};first=TRUE;
  for(guint i=0;i<4;i++) if(fields[i]) { if(!first) g_string_append_c(text,'\t');tsv_field(text,values[i]);first=FALSE; }
  g_string_append_c(text,'\n');g_free(excerpt);g_free(line_text);g_free(column_text);rows++;
  if(text->len>4*1024*1024) { exceeded=TRUE;break; }
 }
 if(exceeded) gtk_label_set_text(GTK_LABEL(out->status),"Copy limit: 4 MiB. Clipboard unchanged.");
 else { gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text->str,text->len);gchar *label=g_strdup_printf("Copied visible matches: %u",rows);gtk_label_set_text(GTK_LABEL(out->status),label);g_free(label); }
 g_string_free(text,TRUE);
}
static void find_all(Merge *m,guint side) {
 if(m->busy || side>2) return;
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(m->find_entry));if(!*query) { gtk_label_set_text(GTK_LABEL(m->find_status),"Enter search text.");return; }
 GtkWidget *window=m->window;g_object_ref(window);guint64 serial=m->history_serial;
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextSearchFlags flags=GTK_TEXT_SEARCH_TEXT_ONLY;if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_case))) flags|=GTK_TEXT_SEARCH_CASE_INSENSITIVE;gboolean whole=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_word));
 GtkListStore *model=gtk_list_store_new(5,G_TYPE_INT,G_TYPE_INT,G_TYPE_UINT,G_TYPE_UINT,G_TYPE_STRING);GtkTextIter start,a,z;gtk_text_buffer_get_start_iter(buffer,&start);guint count=0;gboolean truncated=FALSE;
 while(search_match(start,query,flags,FALSE,whole,&a,&z)) {
  if(count==5000) { truncated=TRUE;break; }
  GtkTextIter begin=a,end=a;gtk_text_iter_set_line_offset(&begin,0);if(!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);if(gtk_text_iter_get_offset(&end)-gtk_text_iter_get_offset(&begin)>120) { begin=a;end=a;gtk_text_iter_forward_chars(&end,120); }
  gchar *excerpt=gtk_text_buffer_get_text(buffer,&begin,&end,TRUE);for(gchar *p=excerpt;*p;p++) if(*p=='\r'||*p=='\n'||*p=='\t') *p=' ';
  GtkTreeIter iter;gtk_list_store_append(model,&iter);gtk_list_store_set(model,&iter,0,gtk_text_iter_get_offset(&a),1,gtk_text_iter_get_offset(&z),2,gtk_text_iter_get_line(&a)+1,3,gtk_text_iter_get_line_offset(&a)+1,4,excerpt,-1);g_free(excerpt);count++;start=z;
 }
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Pane Search Results",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Close",GTK_RESPONSE_CANCEL,"Go to match",GTK_RESPONSE_APPLY,"Edit source file",GTK_RESPONSE_YES,NULL);g_object_ref(dialog);gtk_widget_set_name(dialog,"insight-diff-search-results");gtk_window_set_default_size(GTK_WINDOW(dialog),720,400);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gchar *heading=g_strdup_printf("%s — %s",insight_tr(side==0 ? "Left" : side==1 ? "Right" : "Result"),query);GtkWidget *pattern=gtk_label_new(heading);g_free(heading);gtk_label_set_ellipsize(GTK_LABEL(pattern),PANGO_ELLIPSIZE_END);gtk_box_pack_start(GTK_BOX(box),pattern,FALSE,FALSE,4);gchar *summary=g_strdup_printf(truncated ? "Showing first %u matches (limit reached)." : "Matches: %u",count);gtk_box_pack_start(GTK_BOX(box),gtk_label_new(summary),FALSE,FALSE,4);g_free(summary);
 SearchOutput *out=g_new0(SearchOutput,1);out->dialog=dialog;out->fold=g_strdup("");gchar *native_source=side<2 && m->loaded_paths[side] ? g_strdup(m->loaded_paths[side]) : NULL;out->source=native_source ? g_filename_display_name(native_source) : g_strdup(side==0 ? "Left" : side==1 ? "Right" : "Result");out->can_edit=native_source && source_opener;
 GtkWidget *controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),controls,FALSE,FALSE,4);out->filter_entry=gtk_search_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(out->filter_entry),"Filter displayed matches");gtk_entry_set_max_length(GTK_ENTRY(out->filter_entry),256);gtk_widget_set_name(out->filter_entry,"insight-diff-search-filter");gtk_box_pack_start(GTK_BOX(controls),out->filter_entry,TRUE,TRUE,0);
 GtkWidget *copy=gtk_button_new_with_label("Copy visible matches");gtk_widget_set_name(copy,"insight-diff-search-copy");gtk_box_pack_start(GTK_BOX(controls),copy,FALSE,FALSE,0);g_signal_connect(copy,"clicked",G_CALLBACK(search_output_copy),out);
 GtkWidget *formats=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),formats,FALSE,FALSE,4);const gchar *field_labels[]={"Copy source path","Line","Column","Text preview"};for(guint i=0;i<4;i++) { out->fields[i]=gtk_check_button_new_with_label(field_labels[i]);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(out->fields[i]),m->search_fields[i]);gtk_box_pack_start(GTK_BOX(formats),out->fields[i],FALSE,FALSE,0);g_object_ref(out->fields[i]);gchar *name=g_strdup_printf("insight-diff-search-field-%u",i);gtk_widget_set_name(out->fields[i],name);g_free(name);g_signal_connect(out->fields[i],"toggled",G_CALLBACK(search_output_columns),out); }
 out->filter=GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(model),NULL));g_object_unref(model);gtk_tree_model_filter_set_visible_func(out->filter,search_output_visible,out,NULL);GtkTreeModel *sorted=gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(out->filter));GtkWidget *tree=gtk_tree_view_new_with_model(sorted);g_object_unref(sorted);out->tree=tree;gtk_widget_set_name(tree,"insight-diff-search-results-list");g_signal_connect_object(tree,"row-activated",G_CALLBACK(search_result_activated),dialog,0);const gchar *titles[]={"Line","Column","Text preview"};for(guint i=0;i<3;i++) { gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree),-1,titles[i],gtk_cell_renderer_text_new(),"text",i+2,NULL);gtk_tree_view_column_set_sort_column_id(gtk_tree_view_get_column(GTK_TREE_VIEW(tree),i),i+2); }search_output_columns(NULL,out);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),tree);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);out->status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(box),out->status,FALSE,FALSE,4);
 GtkTreeSelection *selection=g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)));g_signal_connect(selection,"changed",G_CALLBACK(search_output_selection),out);if(count) { GtkTreePath *path=gtk_tree_path_new_first();gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path); }search_output_selection(selection,out);g_signal_connect(out->filter_entry,"changed",G_CALLBACK(search_output_filter),out);gtk_widget_show_all(dialog);
 gint response=gtk_dialog_run(GTK_DIALOG(dialog));m=g_object_get_data(G_OBJECT(window),"insight-diff-state");
 gchar *open_path=NULL;guint open_line=1;
 if((response==GTK_RESPONSE_APPLY || response==GTK_RESPONSE_YES) && m && !m->busy && m->history_serial==serial) { GtkTreeModel *selected_model;GtkTreeIter iter;if(gtk_tree_selection_get_selected(selection,&selected_model,&iter)) { gint begin,end;gtk_tree_model_get(selected_model,&iter,0,&begin,1,&end,2,&open_line,-1);if(response==GTK_RESPONSE_YES && out->can_edit) open_path=g_strdup(native_source);buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));gtk_text_buffer_get_iter_at_offset(buffer,&a,begin);gtk_text_buffer_get_iter_at_offset(buffer,&z,end);gtk_text_buffer_select_range(buffer,&a,&z);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&a,0.1,TRUE,0,0.5);gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),side);gtk_widget_grab_focus(m->views[side]); } }
 else if((response==GTK_RESPONSE_APPLY || response==GTK_RESPONSE_YES) && m && m->history_serial!=serial) gtk_label_set_text(GTK_LABEL(m->find_status),"Text changed; run Find all again.");
 if(m) for(guint i=0;i<4;i++) m->search_fields[i]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(out->fields[i]));
 out->closing=TRUE;gtk_widget_destroy(dialog);g_object_unref(selection);for(guint i=0;i<4;i++) g_object_unref(out->fields[i]);g_object_unref(out->filter);g_free(out->fold);g_free(out->source);g_free(out);g_free(native_source);g_object_unref(dialog);g_object_unref(window);if(open_path && source_opener) source_opener(open_path,open_line);g_free(open_path);
}
static void find_all_clicked(GtkWidget *widget,gpointer data) { (void)widget;Merge *m=data;find_all(m,gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side))); }
static void find_text(Merge *m,gboolean backwards) {
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(m->find_entry));if(!*query) { gtk_label_set_text(GTK_LABEL(m->find_status),"Enter search text.");return; }
 int side=gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side));
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a,z,start;
 if(gtk_text_buffer_get_selection_bounds(buffer,&a,&z)) start=backwards ? a : z;
 else gtk_text_buffer_get_iter_at_mark(buffer,&start,gtk_text_buffer_get_insert(buffer));
 GtkTextSearchFlags flags=GTK_TEXT_SEARCH_TEXT_ONLY;
 if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_case))) flags|=GTK_TEXT_SEARCH_CASE_INSENSITIVE;
 gboolean whole=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_word));
 gboolean found=search_match(start,query,flags,backwards,whole,&a,&z);
 gboolean wrapped=!found && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_wrap));
 if(wrapped) {
  if(backwards) gtk_text_buffer_get_end_iter(buffer,&start);else gtk_text_buffer_get_start_iter(buffer,&start);
  found=search_match(start,query,flags,backwards,whole,&a,&z);
 }
 if(found) {
  gtk_text_buffer_select_range(buffer,&a,&z);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&a,0.1,TRUE,0,0.5);
  gchar *label=g_strdup_printf(wrapped ? "Wrapped: line %d" : "Found: line %d",gtk_text_iter_get_line(&a)+1);gtk_label_set_text(GTK_LABEL(m->find_status),label);g_free(label);
 } else gtk_label_set_text(GTK_LABEL(m->find_status),"Text not found.");
}
static void find_clicked(GtkWidget *w,gpointer data) { Merge *m=data;find_text(m,w==m->find_previous); }
static void find_activate(GtkEntry *entry,gpointer data) { (void)entry;find_text(data,FALSE); }
/* Recognize uniform and initial-capital case; mixed identifiers stay literal. */
static gchar *preserve_case(const gchar *old,const gchar *replacement) {
 guint upper=0,lower=0,cased=0;gboolean initial=TRUE;
 for(const gchar *p=old;*p;p=g_utf8_next_char(p)) {
  gunichar c=g_utf8_get_char(p);gboolean up=g_unichar_isupper(c)||g_unichar_istitle(c),down=g_unichar_islower(c);
  if(up || down) { if((cased==0 && !up) || (cased>0 && !down)) initial=FALSE;cased++;upper+=up;lower+=down; }
 }
 if(upper && !lower) return g_utf8_strup(replacement,-1);
 if(lower && !upper) return g_utf8_strdown(replacement,-1);
 if(initial && cased) {
  gchar *low=g_utf8_strdown(replacement,-1);
  for(gchar *p=low;*p;p=g_utf8_next_char(p)) if(g_unichar_islower(g_utf8_get_char(p)) || g_unichar_isupper(g_utf8_get_char(p))) {
   gchar *next=g_utf8_next_char(p),*up=g_utf8_strup(p,next-p),*prefix=g_strndup(low,p-low),*text=g_strconcat(prefix,up,next,NULL);g_free(up);g_free(prefix);g_free(low);return text;
  }
  return low;
 }
 return g_strdup(replacement);
}
static gchar *toggle_case(const gchar *text) {
 GString *out=g_string_new(NULL);
 for(const gchar *p=text;*p;) { const gchar *next=g_utf8_next_char(p);gunichar c=g_utf8_get_char(p);
  gchar *part=(g_unichar_isupper(c)||g_unichar_istitle(c)) ? g_utf8_strdown(p,next-p) : g_utf8_strup(p,next-p);g_string_append(out,part);g_free(part);p=next;
 }
 return g_string_free(out,FALSE);
}
static void selection_case(Merge *m,guint action) {
 if(m->busy || m->changing) return;
 GtkTextBuffer *buffer=result(m);GtkTextIter a,z;
 if(!gtk_text_buffer_get_selection_bounds(buffer,&a,&z)) return;
 gchar *old=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);
 gchar *text=action==0 ? g_utf8_strup(old,-1) : action==1 ? g_utf8_strdown(old,-1) : toggle_case(old);
 gchar *all=contents(buffer);guint64 bytes=strlen(all)-strlen(old)+strlen(text);g_free(all);
 if(bytes>1024*1024) gtk_label_set_text(GTK_LABEL(m->replace_status),"Case conversion limit: 1 MiB result. Nothing changed.");
 else if(strcmp(old,text)) {
  gint begin=gtk_text_iter_get_offset(&a);gtk_text_buffer_begin_user_action(buffer);gtk_text_buffer_delete(buffer,&a,&z);gtk_text_buffer_insert(buffer,&a,text,-1);gtk_text_buffer_get_iter_at_offset(buffer,&z,begin);gtk_text_buffer_select_range(buffer,&z,&a);gtk_text_buffer_end_user_action(buffer);
  gtk_label_set_text(GTK_LABEL(m->replace_status),"Selection case changed.");
 }
 g_free(text);g_free(old);
}
typedef struct { gint begin,end;gchar *text; } ReplaceRange;

static gboolean line_range_add(GArray *ranges,gint begin,gint end,const gchar *text,guint64 removed,guint64 *bytes) {
 ReplaceRange range={begin,end,g_strdup(text)};g_array_append_val(ranges,range);*bytes=*bytes-removed+strlen(text);
 return ranges->len<=100000 && *bytes<=1024*1024;
}
static void edit_lines(Merge *m,guint action) {
 if(m->busy || m->changing || action>3) return;
 GtkTextBuffer *buffer=result(m);GtkTextIter a,z;
 gboolean selection=gtk_text_buffer_get_selection_bounds(buffer,&a,&z);
 if(!selection) gtk_text_buffer_get_iter_at_mark(buffer,&a,gtk_text_buffer_get_insert(buffer));
 gint first=gtk_text_iter_get_line(&a),last=selection ? gtk_text_iter_get_line(&z) : first;
 if(selection && last>first && gtk_text_iter_get_line_offset(&z)==0) last--;
 gtk_spin_button_update(GTK_SPIN_BUTTON(m->edit_tab_width));
 guint width=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->edit_tab_width));
 gchar *all=contents(buffer);guint64 bytes=strlen(all);g_free(all);
 GArray *ranges=g_array_new(FALSE,FALSE,sizeof(ReplaceRange));gboolean ok=TRUE;
 for(gint line=first;line<=last && ok;line++) {
  gtk_text_buffer_get_iter_at_line(buffer,&a,line);z=a;if(!gtk_text_iter_ends_line(&z)) gtk_text_iter_forward_to_line_end(&z);
  gchar *text=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);gint begin=gtk_text_iter_get_offset(&a);
  if(action==0 && *text && *text!='#') { gchar *spaces=g_strnfill(width,' ');ok=line_range_add(ranges,begin,begin,spaces,0,&bytes);g_free(spaces); }
  else if(action==1) {
   guint count=0;if(*text=='\t') count=1;else while(text[count]==' ' && count<width) count++;
   if(count) ok=line_range_add(ranges,begin,begin+count,"",count,&bytes);
  } else if(action==2) {
   guint column=0;gint offset=begin;
   for(const gchar *p=text;*p && ok;p=g_utf8_next_char(p),offset++) {
    if(*p=='\t') { guint spaces=width-column%width;gchar *fill=g_strnfill(spaces,' ');ok=line_range_add(ranges,offset,offset+1,fill,1,&bytes);g_free(fill);column+=spaces; }
    else column++;
   }
  } else if(action==3) {
   gsize length=strlen(text),end=length;while(end && (text[end-1]==' ' || text[end-1]=='\t')) end--;
   if(end<length) { gint offset=begin+g_utf8_strlen(text,end);ok=line_range_add(ranges,offset,gtk_text_iter_get_offset(&z),"",length-end,&bytes); }
  }
  g_free(text);
 }
 if(!ok) gtk_label_set_text(GTK_LABEL(m->replace_status),"Line edit limit: 100000 changes and 1 MiB result. Nothing changed.");
 else if(ranges->len) {
  gtk_text_buffer_begin_user_action(buffer);
  for(guint i=ranges->len;i>0;i--) { ReplaceRange range=g_array_index(ranges,ReplaceRange,i-1);gtk_text_buffer_get_iter_at_offset(buffer,&a,range.begin);gtk_text_buffer_get_iter_at_offset(buffer,&z,range.end);gtk_text_buffer_delete(buffer,&a,&z);gtk_text_buffer_insert(buffer,&a,range.text,-1); }
  gtk_text_buffer_end_user_action(buffer);gtk_label_set_text(GTK_LABEL(m->replace_status),"Result lines edited.");
 } else gtk_label_set_text(GTK_LABEL(m->replace_status),"No changes.");
 for(guint i=0;i<ranges->len;i++) g_free(g_array_index(ranges,ReplaceRange,i).text);
 g_array_unref(ranges);
}
static void replace_result(GtkWidget *widget,gpointer data) {
 Merge *m=data;if(m->busy || m->changing) return;
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(m->find_entry));
 if(!*query) { gtk_label_set_text(GTK_LABEL(m->replace_status),"Enter search text.");return; }
 GtkTextBuffer *buffer=result(m);GtkTextIter start,limit,a,z;
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->replace_selection))) {
  if(!gtk_text_buffer_get_selection_bounds(buffer,&start,&limit)) { gtk_label_set_text(GTK_LABEL(m->replace_status),"Select result text first.");return; }
 } else gtk_text_buffer_get_bounds(buffer,&start,&limit);
 GtkTextSearchFlags flags=GTK_TEXT_SEARCH_TEXT_ONLY;
 if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_case))) flags|=GTK_TEXT_SEARCH_CASE_INSENSITIVE;
 gboolean whole=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_word));
 gboolean once=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->replace_once));
 const gchar *replacement=gtk_entry_get_text(GTK_ENTRY(m->replace_entry));
 gboolean keep_case=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->replace_case));
 gchar *original=contents(buffer);guint64 bytes=strlen(original);g_free(original);
 GArray *ranges=g_array_new(FALSE,FALSE,sizeof(ReplaceRange));gint last_line=-1;gboolean exceeded=FALSE;
 while(search_match(start,query,flags,FALSE,whole,&a,&z) && gtk_text_iter_compare(&z,&limit)<=0) {
  if(!once || gtk_text_iter_get_line(&a)!=last_line) {
   gchar *old=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);gsize old_bytes=strlen(old);
   /* A no-op must not create history or consume the bounded match budget. */
   gchar *text=keep_case ? preserve_case(old,replacement) : g_strdup(replacement);gsize replacement_bytes=strlen(text);
   gboolean same=!strcmp(old,text);g_free(old);
   last_line=gtk_text_iter_get_line(&a);
   if(!same) { ReplaceRange range={gtk_text_iter_get_offset(&a),gtk_text_iter_get_offset(&z),text};g_array_append_val(ranges,range);bytes=bytes-old_bytes+replacement_bytes;
    if(ranges->len>100000 || bytes>1024*1024) { exceeded=TRUE;break; }
   } else g_free(text);
  }
  start=z;
 }
 if(exceeded) gtk_label_set_text(GTK_LABEL(m->replace_status),"Replacement limit: 100000 changes and 1 MiB result. Nothing changed.");
 else if(widget!=m->replace_apply) { gchar *label=g_strdup_printf("Preview: %u replacements",ranges->len);gtk_label_set_text(GTK_LABEL(m->replace_status),label);g_free(label); }
 else if(ranges->len) {
  gtk_text_buffer_begin_user_action(buffer);
  /* Reverse edits preserve offsets and tracked merge marks. */
  for(guint i=ranges->len;i>0;i--) { ReplaceRange range=g_array_index(ranges,ReplaceRange,i-1);gtk_text_buffer_get_iter_at_offset(buffer,&a,range.begin);gtk_text_buffer_get_iter_at_offset(buffer,&z,range.end);gtk_text_buffer_delete(buffer,&a,&z);gtk_text_buffer_insert(buffer,&a,range.text,-1); }
  gtk_text_buffer_end_user_action(buffer);
  gchar *label=g_strdup_printf("Replaced: %u",ranges->len);gtk_label_set_text(GTK_LABEL(m->replace_status),label);g_free(label);
 } else gtk_label_set_text(GTK_LABEL(m->replace_status),"No changes.");
 for(guint i=0;i<ranges->len;i++) g_free(g_array_index(ranges,ReplaceRange,i).text);
 g_array_unref(ranges);
}
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
static const gchar *whitespace_symbol(GtkTextIter at,gboolean spaces,gboolean endings) {
 gunichar c=gtk_text_iter_get_char(&at);
 if(spaces && c==' ') return "·";
 if(spaces && c=='\t') return "→";
 if(endings && c=='\r') return "¶";
 if(endings && c=='\n') { GtkTextIter before=at;if(gtk_text_iter_backward_char(&before) && gtk_text_iter_get_char(&before)=='\r') return NULL;return "¶"; }
 return NULL;
}
static gboolean draw_whitespace(GtkWidget *widget,cairo_t *cr,gpointer data) {
 Merge *m=data;gboolean spaces=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_space)),endings=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_eol));if(!spaces && !endings) return FALSE;
 GtkTextView *view=GTK_TEXT_VIEW(widget);GdkWindow *window=gtk_text_view_get_window(view,GTK_TEXT_WINDOW_TEXT);if(!window || !gtk_cairo_should_draw_window(cr,window)) return FALSE;
 cairo_save(cr);gtk_cairo_transform_to_window(cr,widget,window);cairo_rectangle(cr,0,0,gdk_window_get_width(window),gdk_window_get_height(window));cairo_clip(cr);
 GdkRGBA color;GtkStyleContext *style=gtk_widget_get_style_context(widget);gtk_style_context_get_color(style,gtk_style_context_get_state(style),&color);color.alpha*=0.55;gdk_cairo_set_source_rgba(cr,&color);
 GdkRectangle visible;gtk_text_view_get_visible_rect(view,&visible);GtkTextIter at;gtk_text_view_get_iter_at_location(view,&at,visible.x,visible.y);
 PangoLayout *layout=gtk_widget_create_pango_layout(widget,NULL);
 /* Bound work even for pathological unwrapped lines. Glyphs never enter buffers. */
 for(guint scanned=0;scanned<20000 && !gtk_text_iter_is_end(&at);scanned++) {
  GdkRectangle location;gtk_text_view_get_iter_location(view,&at,&location);if(location.y>visible.y+visible.height) break;
  const gchar *symbol=whitespace_symbol(at,spaces,endings);
  if(symbol && location.x+MAX(location.width,8)>=visible.x && location.x<visible.x+visible.width && location.y+location.height>=visible.y) {
   gint x,y,width,height;gtk_text_view_buffer_to_window_coords(view,GTK_TEXT_WINDOW_TEXT,location.x,location.y,&x,&y);pango_layout_set_text(layout,symbol,-1);pango_layout_get_pixel_size(layout,&width,&height);
   cairo_move_to(cr,x+MAX(0,(location.width-width)/2),y+MAX(0,(location.height-height)/2));pango_cairo_show_layout(cr,layout);
  }
  if(!gtk_text_iter_forward_char(&at)) break;
 }
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
  for(gsize i=0;i<m->diff.count;i++) { InsightDiffHunk *h=&m->diff.hunks[i];MapBlock block;block.begin=map_line(m->sources[side],&offset,&line,side ? h->b_begin : h->a_begin);block.end=map_line(m->sources[side],&offset,&line,side ? h->b_end : h->a_end);gsize begin=side ? h->b_begin : h->a_begin,end=side ? h->b_end : h->a_end;if(end>begin && m->sources[side][end-1]!='\r' && m->sources[side][end-1]!='\n') block.end++;g_array_append_val(m->map_blocks[side],block); }
  m->map_lines[side]=map_line(m->sources[side],&offset,&line,strlen(m->sources[side]))+1;
 }
 map_redraw(m);
}
static void map_extent(MapBlock block,guint lines,gdouble height,gdouble *top,gdouble *span) {
 gdouble total=MAX(lines,1);height=MAX(height,1.);
 *top=CLAMP(block.begin/total*height,0.,MAX(0.,height-3.));*span=MIN(height-*top,MAX(3.,(block.end-block.begin)/total*height));
}
static gboolean map_draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 Merge *m=data;gint side=widget==m->maps[1],width=gtk_widget_get_allocated_width(widget),height=gtk_widget_get_allocated_height(widget);
 cairo_set_source_rgb(cr,.94,.95,.97);cairo_paint(cr);
 GArray *blocks=m->map_blocks[side];gint current=selected(m);
 for(guint pass=0;pass<2;pass++) for(guint i=0;blocks && i<blocks->len;i++) { if(((gint)i==current)!=(pass==1)) continue;MapBlock b=g_array_index(blocks,MapBlock,i);gdouble y,h;map_extent(b,m->map_lines[side],height,&y,&h);
  if((gint)i==current) cairo_set_source_rgb(cr,.2,.38,.75);else if(side) cairo_set_source_rgb(cr,.3,.65,.4);else cairo_set_source_rgb(cr,.85,.5,.25);
  cairo_rectangle(cr,3,y,MAX(1,width-6),h);cairo_fill(cr);
 }
 GtkAdjustment *a=m->adjustments[side];gdouble extent=gtk_adjustment_get_upper(a)-gtk_adjustment_get_lower(a);
 if(extent>0) { gdouble y=(gtk_adjustment_get_value(a)-gtk_adjustment_get_lower(a))/extent*height,h=MAX(4.,gtk_adjustment_get_page_size(a)/extent*height);cairo_set_source_rgba(cr,.12,.2,.35,.7);cairo_set_line_width(cr,1.);cairo_rectangle(cr,.5,y+.5,MAX(1,width-1),MAX(1,MIN(h,height-y)-1));cairo_stroke(cr); }
 return FALSE;
}
static gboolean map_click(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 Merge *m=data;if(event->button!=1 || m->busy || !m->sources[0]) return FALSE;gint side=widget==m->maps[1];gdouble height=MAX(gtk_widget_get_allocated_height(widget),1),total=MAX(m->map_lines[side],1),y=CLAMP(event->y,0.,height);
 GArray *blocks=m->map_blocks[side];gint best=-1;gdouble nearest=G_MAXDOUBLE;
 for(guint i=0;blocks && i<blocks->len;i++) { MapBlock b=g_array_index(blocks,MapBlock,i);gdouble top,span;map_extent(b,m->map_lines[side],height,&top,&span);gdouble end=top+span,distance=ABS(y-(top+end)/2);
  if(y>=top-2 && y<=end+2 && distance<nearest) { best=(gint)i;nearest=distance; }
 }
 if(best>=0) { GtkTreePath *path=gtk_tree_path_new_from_indices(best,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view),path,NULL,FALSE);gtk_tree_path_free(path);map_redraw(m);return TRUE; }
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
 GtkWidget *sources[]={m->mono,m->numbers,m->list_toggle,m->wrap_toggle,m->sync,m->vertical,m->overview,m->visible_space,m->visible_eol};GtkWidget *checks[G_N_ELEMENTS(sources)];
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
 if(!view_defaults) view_defaults=g_key_file_new();
 view_save(m,view_defaults);open_compares=g_list_remove(open_compares,m);
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
 GtkWidget *controls[G_N_ELEMENTS(view_checks)];view_controls(m,controls);for(guint i=0;i<G_N_ELEMENTS(view_checks);i++) g_object_unref(controls[i]);g_object_unref(m->zoom);
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
static void write_result(Merge *,guint);
static void source_menu_action(GtkMenuItem *item,gpointer window) {
 Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");if(!m) return;
 gint side=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"side"));gint action=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"action"));
 if(action>=16) { if(action==19) find_all(m,side);else selection_search(m,side,action==16 ? 0 : action==17 ? 1 : -1); }else if(action==0) edit_source(m,side);else if(action==1) copy_difference(m,side);else if(action==2) view_options(GTK_WIDGET(item),m);else if(action==3) focus_line(m,side);else if(side==2 && action<7) selection_case(m,action-4);else if(side==2 && action<11) edit_lines(m,action-7);else if(side==2 && action<13) history_end(m,action==12);else if(side==2 && action==13) history_dialog(NULL,m);else if(side==2) write_result(m,action-13);
}
static void view_menu_toggle(GtkCheckMenuItem *item,gpointer toggle) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle),gtk_check_menu_item_get_active(item)); }
static void source_menu(GtkTextView *view,GtkWidget *menu,gpointer data) {
 Merge *m=data;guint side=GTK_WIDGET(view)==m->views[1] ? 1 : GTK_WIDGET(view)==m->views[0] ? 0 : 2;
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
 const gchar *labels[]={"Edit File — Ctrl+E","Copy selected difference","File Compare Options…","Go to Line — Ctrl+G","Uppercase selection","Lowercase selection","Toggle selection case","Indent result lines","Outdent result lines","Tabs to spaces in result lines","Trim trailing spaces in result lines","Undo All retained changes","Redo All retained changes","Result History…","Save a copy of Result…","Save Result selection…","Load Search String","Search identifier forward — Ctrl+F3","Search identifier backward — Ctrl+Shift+F3","Find all matches in pane — Ctrl+Shift+F"};
 for(guint action=0;action<G_N_ELEMENTS(labels);action++) {
  if((side==2 && action<2) || (side!=2 && action>=4 && action<16)) continue;
  GtkWidget *item=gtk_menu_item_new_with_label(labels[action]);g_object_set_data(G_OBJECT(item),"side",GUINT_TO_POINTER(side));g_object_set_data(G_OBJECT(item),"action",GUINT_TO_POINTER(action));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect_object(item,"activate",G_CALLBACK(source_menu_action),m->window,0);
  if(action>=4) gtk_widget_set_sensitive(item,!m->busy && (action>=7 || gtk_text_buffer_get_has_selection(result(m))));
  if(action>=11) gtk_widget_set_sensitive(item,!m->busy && (action==11 ? m->position>0 : action==12 ? m->position+1<m->history->len : m->history->len>0));
  if(action>=14) gtk_widget_set_sensitive(item,!m->busy && (action==14 || gtk_text_buffer_get_has_selection(result(m))));
  if(action>=16) gtk_widget_set_sensitive(item,!m->busy);
  if(action<2) gtk_widget_set_sensitive(item,!m->busy && m->loaded_paths[side] && (action==0 ? source_opener!=NULL : selected(m)>=0));
 }
 GtkWidget *toggles[]={m->mono,m->numbers,m->list_toggle,m->wrap_toggle,m->vertical,m->overview,m->visible_space,m->visible_eol};
 for(guint i=0;i<G_N_ELEMENTS(toggles);i++) { GtkWidget *item=gtk_check_menu_item_new_with_label(gtk_button_get_label(GTK_BUTTON(toggles[i])));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggles[i])));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect_object(item,"toggled",G_CALLBACK(view_menu_toggle),toggles[i],0); }
 gtk_widget_show_all(menu);
}
static void result_menu_show(GtkWidget *menu,gpointer window) {
 Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");if(!m) return;
 GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data);g_list_free(children);
 source_menu(GTK_TEXT_VIEW(m->views[2]),menu,m);
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
/* Capture before the modal chooser; never write stale text after a nested loop. */
static void write_result(Merge *m,guint mode) {
 if(m->busy || m->changing || mode>2) return;
 gchar *text=NULL;
 if(mode==2) { GtkTextIter a,z;if(!gtk_text_buffer_get_selection_bounds(result(m),&a,&z)) { message(m,"Select result text first.");return; }text=gtk_text_buffer_get_text(result(m),&a,&z,TRUE); }
 else text=contents(result(m));
 GtkWidget *window=m->window;g_object_ref(window);guint64 serial=m->history_serial;
 const gchar *titles[]={"Save merge result","Save a copy of Result","Save Result selection"};const gchar *names[]={"merged.txt","merged-copy.txt","selection.txt"};
 GtkWidget *dialog=gtk_file_chooser_dialog_new(titles[mode],GTK_WINDOW(window),GTK_FILE_CHOOSER_ACTION_SAVE,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,NULL);g_object_ref(dialog);gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),TRUE);gtk_widget_set_name(dialog,"insight-diff-save-dialog");
 const gchar *notes[]={"Save the full merge result.","Save a separate copy; keep the current result's saved or modified status.","Save the exact selection; keep the current result's saved or modified status."};GtkWidget *note=gtk_label_new(notes[mode]);gtk_label_set_line_wrap(GTK_LABEL(note),TRUE);gtk_label_set_xalign(GTK_LABEL(note),0);gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog),note);gtk_widget_show(note);
 gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),names[mode]);
 gchar *open_path=NULL;gint response=gtk_dialog_run(GTK_DIALOG(dialog));m=g_object_get_data(G_OBJECT(window),"insight-diff-state");
 if(response==GTK_RESPONSE_ACCEPT && m) {
  if(m->busy || serial!=m->history_serial) message(m,"Result changed; reopen the save command. Nothing written.");
  else {
   gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));GError *error=NULL;
   if(path && g_file_set_contents(path,text,-1,&error)) {
    if(mode==0) { g_free(m->saved);m->saved=g_strdup(text);message(m,"Result saved.");state(m); }
    else { message(m,mode==1 ? "Result copy saved; save state unchanged." : "Result selection saved; save state unchanged.");if(mode==2) open_path=g_strdup(path); }
   } else { message(m,error ? error->message : "Choose a local destination filename.");g_clear_error(&error); }
   g_free(path);
  }
 }
 gtk_widget_destroy(dialog);g_object_unref(dialog);g_object_unref(window);g_free(text);
 if(open_path && source_opener) source_opener(open_path,1);
 g_free(open_path);
}
static void save(GtkWidget *w,gpointer data) { (void)w;write_result(data,0); }
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
 Merge *m=g_new0(Merge,1);for(guint i=0;i<4;i++) m->search_fields[i]=TRUE;m->history=g_ptr_array_new_with_free_func(snapshot_free);m->saved=g_strdup("");
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
 GtkWidget *history_button=button(display,"Result History…",G_CALLBACK(history_dialog),m);gtk_widget_set_name(history_button,"insight-diff-history-button");
 GtkWidget *whitespace=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),whitespace,FALSE,FALSE,0);
 m->visible_space=gtk_check_button_new_with_label("Visible tabs and spaces");gtk_box_pack_start(GTK_BOX(whitespace),m->visible_space,FALSE,FALSE,0);
 m->visible_eol=gtk_check_button_new_with_label("Visible line endings");gtk_box_pack_start(GTK_BOX(whitespace),m->visible_eol,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(whitespace),gtk_label_new("· space   → tab   ¶ line ending"),FALSE,FALSE,4);
 GtkWidget *find=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),find,FALSE,FALSE,0);
 m->find_side=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->find_side),"Left");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->find_side),"Right");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->find_side),"Result");gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),0);gtk_box_pack_start(GTK_BOX(find),m->find_side,FALSE,FALSE,0);
 m->find_entry=gtk_search_entry_new();gtk_widget_set_name(m->find_entry,"insight-diff-search");gtk_entry_set_placeholder_text(GTK_ENTRY(m->find_entry),"Find text — Ctrl+F, F3 / Shift+F3");gtk_box_pack_start(GTK_BOX(find),m->find_entry,TRUE,TRUE,0);
 m->find_case=gtk_check_button_new_with_label("Match case");gtk_box_pack_start(GTK_BOX(find),m->find_case,FALSE,FALSE,0);
 m->find_word=gtk_check_button_new_with_label("Whole words only");gtk_box_pack_start(GTK_BOX(find),m->find_word,FALSE,FALSE,0);
 m->find_wrap=gtk_check_button_new_with_label("Wrap around");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_wrap),TRUE);gtk_box_pack_start(GTK_BOX(find),m->find_wrap,FALSE,FALSE,0);
 m->find_previous=button(find,"Find previous",G_CALLBACK(find_clicked),m);m->find_next=button(find,"Find next",G_CALLBACK(find_clicked),m);
 button(find,"Find all…",G_CALLBACK(find_all_clicked),m);
 m->find_status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(find),m->find_status,FALSE,FALSE,0);
 g_signal_connect(m->find_entry,"activate",G_CALLBACK(find_activate),m);g_signal_connect(m->find_entry,"key-press-event",G_CALLBACK(key),m);
 GtkWidget *replace=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),replace,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(replace),gtk_label_new("Replace in Result — Ctrl+H"),FALSE,FALSE,0);m->replace_entry=gtk_entry_new();gtk_widget_set_name(m->replace_entry,"insight-diff-replacement");gtk_entry_set_placeholder_text(GTK_ENTRY(m->replace_entry),"Replacement text (empty deletes matches)");gtk_box_pack_start(GTK_BOX(replace),m->replace_entry,TRUE,TRUE,0);
 GtkWidget *replace_options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),replace_options,FALSE,FALSE,0);
 m->replace_selection=gtk_check_button_new_with_label("Result selection only");gtk_box_pack_start(GTK_BOX(replace_options),m->replace_selection,FALSE,FALSE,0);
 m->replace_once=gtk_check_button_new_with_label("One occurrence per line");gtk_box_pack_start(GTK_BOX(replace_options),m->replace_once,FALSE,FALSE,0);
 button(replace,"Preview count",G_CALLBACK(replace_result),m);m->replace_apply=button(replace,"Replace all in Result",G_CALLBACK(replace_result),m);gtk_widget_set_name(m->replace_apply,"insight-diff-replace-all");
 m->replace_case=gtk_check_button_new_with_label("Preserve old case");gtk_box_pack_start(GTK_BOX(replace_options),m->replace_case,FALSE,FALSE,0);gtk_widget_set_name(m->replace_case,"insight-diff-preserve-case");
 m->replace_status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(replace_options),m->replace_status,FALSE,FALSE,0);
 gtk_widget_set_tooltip_text(replace,"Uses search text, case and whole-word options above; searches the entire result or its selection without wrapping.");
 GtkWidget *line=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),line,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(line),gtk_label_new("Go to Line — Ctrl+G"),FALSE,FALSE,0);
 m->line_side=gtk_combo_box_text_new();for(guint i=0;i<3;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->line_side),i==0 ? "Left" : i==1 ? "Right" : "Result");gtk_combo_box_set_active(GTK_COMBO_BOX(m->line_side),0);gtk_box_pack_start(GTK_BOX(line),m->line_side,FALSE,FALSE,0);
 m->line_number=gtk_spin_button_new_with_range(1,G_MAXINT,1);gtk_entry_set_width_chars(GTK_ENTRY(m->line_number),10);gtk_widget_set_name(m->line_number,"insight-diff-line");gtk_box_pack_start(GTK_BOX(line),m->line_number,FALSE,FALSE,0);
 m->line_select=gtk_check_button_new_with_label("Select line text");gtk_box_pack_start(GTK_BOX(line),m->line_select,FALSE,FALSE,0);
 button(line,"Go",G_CALLBACK(go_line),m);GtkWidget *first=button(line,"First line",G_CALLBACK(line_begin),m);g_object_set_data(G_OBJECT(m->window),"line-first",first);button(line,"Last line",G_CALLBACK(line_begin),m);
 GtkWidget *edit_menu=gtk_menu_new(),*edit_button=gtk_menu_button_new();gtk_button_set_label(GTK_BUTTON(edit_button),"Result menu");gtk_widget_set_name(edit_button,"insight-diff-result-menu");gtk_menu_button_set_popup(GTK_MENU_BUTTON(edit_button),edit_menu);gtk_box_pack_start(GTK_BOX(line),edit_button,FALSE,FALSE,0);g_signal_connect_object(edit_menu,"show",G_CALLBACK(result_menu_show),m->window,0);
 gtk_box_pack_start(GTK_BOX(line),gtk_label_new("Edit tab width"),FALSE,FALSE,0);m->edit_tab_width=gtk_spin_button_new_with_range(1,16,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->edit_tab_width),4);gtk_entry_set_width_chars(GTK_ENTRY(m->edit_tab_width),2);gtk_box_pack_start(GTK_BOX(line),m->edit_tab_width,FALSE,FALSE,0);
 m->line_status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(line),m->line_status,FALSE,FALSE,0);
 gtk_widget_set_tooltip_text(m->line_number,"Logical line number; values beyond the end go to the last line.");
 g_signal_connect(m->line_number,"activate",G_CALLBACK(go_line),m);g_signal_connect(m->line_number,"key-press-event",G_CALLBACK(key),m);
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
  g_signal_connect_after(m->views[i],"draw",G_CALLBACK(draw_numbers),m);g_signal_connect_after(m->views[i],"draw",G_CALLBACK(draw_whitespace),m);
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
 g_signal_connect(m->visible_space,"toggled",G_CALLBACK(display_changed),m);g_signal_connect(m->visible_eol,"toggled",G_CALLBACK(display_changed),m);
 g_signal_connect(m->zoom,"value-changed",G_CALLBACK(display_changed),m);g_signal_connect(m->mono,"toggled",G_CALLBACK(display_changed),m);g_signal_connect(m->vertical,"toggled",G_CALLBACK(display_changed),m);g_signal_connect(m->overview,"toggled",G_CALLBACK(display_changed),m);
 GtkWidget *controls[G_N_ELEMENTS(view_checks)];view_controls(m,controls);for(guint i=0;i<G_N_ELEMENTS(view_checks);i++) { g_object_ref(controls[i]);gchar *name=g_strconcat("insight-diff-",view_checks[i],NULL);gtk_widget_set_name(controls[i],name);g_free(name); }g_object_ref(m->zoom);
 record(m);selection(NULL,m);gtk_widget_show_all(m->window);view_restore(m,view_defaults);display_changed(NULL,m);open_compares=g_list_prepend(open_compares,m);return m;
}

void insight_diff_show(GtkWidget *parent) { (void)merge_new(parent); }

void insight_diff_show_files(GtkWidget *parent,const gchar *left,const gchar *right,guint text_flags) {
 Merge *m=merge_new(parent);const gchar *paths[]={left,right};
 for(guint side=0;side<2;side++) { GError *error=NULL;gchar *text=g_filename_to_utf8(paths[side],-1,NULL,NULL,&error);if(!text) { message(m,error->message);g_clear_error(&error);return; }gtk_entry_set_text(GTK_ENTRY(m->paths[side]),text);g_free(text); }
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_space),(text_flags&INSIGHT_DIFF_IGNORE_SPACE)!=0);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_eol),(text_flags&INSIGHT_DIFF_IGNORE_EOL)!=0);compare(NULL,m);
}
