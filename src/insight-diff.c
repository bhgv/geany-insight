/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-diff.h"
#include "../insight/core/diff.h"
#include <string.h>

typedef struct { gchar *text; gint *positions; } Snapshot;
typedef struct {
 GtkWidget *window,*paths[2],*views[3],*status,*undo,*redo,*take[2],*external;
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
 gtk_widget_set_sensitive(m->undo,m->position>0);
 gtk_widget_set_sensitive(m->redo,m->position+1<m->history->len);
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
static gboolean key(GtkWidget *w,GdkEventKey *event,gpointer data) {
 (void)w;
 if(!(event->state&GDK_CONTROL_MASK)) return FALSE;
 if(event->keyval==GDK_KEY_z || event->keyval==GDK_KEY_Z) {
  if(event->state&GDK_SHIFT_MASK) redo(NULL,data);else undo(NULL,data);return TRUE;
 }
 if(event->keyval==GDK_KEY_y || event->keyval==GDK_KEY_Y) { redo(NULL,data);return TRUE; }
 return FALSE;
}
static void message(Merge *m,const gchar *text) { gtk_label_set_text(GTK_LABEL(m->status),text); }
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
 /* Child text widgets can already be destroyed here; their buffer owns the marks. */
 g_signal_handlers_disconnect_by_data(m->output,m);g_signal_handlers_disconnect_by_data(m->selection,m);
 g_object_unref(m->output);g_object_unref(m->selection);
 g_free(m->marks);insight_diff_clear(&m->diff);g_ptr_array_free(m->history,TRUE);
 for(int i=0;i<2;i++) { g_free(m->sources[i]);g_free(m->loaded_paths[i]); }
 g_free(m->saved);g_object_unref(m->list);g_free(m);
}
static gint selected(Merge *m) {
 GtkTreeIter it;GtkTreeModel *model;gint id=-1;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->hunk_view)),&model,&it)) gtk_tree_model_get(model,&it,0,&id,-1);
 return id;
}
static gint chars(const gchar *s,size_t offset) { return (gint)g_utf8_pointer_to_offset(s,s+offset); }
static void highlight(Merge *m,int side,size_t begin,size_t end) {
 GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a,z;
 gtk_text_buffer_get_iter_at_offset(b,&a,chars(m->sources[side],begin));gtk_text_buffer_get_iter_at_offset(b,&z,chars(m->sources[side],end));
 gtk_text_buffer_apply_tag_by_name(b,"difference",&a,&z);
}
static void selection(GtkTreeSelection *s,gpointer data) {
 (void)s;Merge *m=data;gint id=selected(m);
 for(int side=0;side<2;side++) gtk_widget_set_sensitive(m->take[side],id>=0);
 if(id<0 || (size_t)id>=m->diff.count) return;
 InsightDiffHunk *h=&m->diff.hunks[id];
 for(int side=0;side<3;side++) {
  GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a,z;
  if(side==2) { gtk_text_buffer_get_iter_at_mark(b,&a,m->marks[2*id]);gtk_text_buffer_get_iter_at_mark(b,&z,m->marks[2*id+1]); }
  else { gtk_text_buffer_get_iter_at_offset(b,&a,chars(m->sources[side],side ? h->b_begin : h->a_begin));gtk_text_buffer_get_iter_at_offset(b,&z,chars(m->sources[side],side ? h->b_end : h->a_end)); }
  gtk_text_buffer_select_range(b,&a,&z);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&a,0.1,TRUE,0.0,0.5);
 }
}
static void take(GtkWidget *w,gpointer data) {
 Merge *m=data;gint id=selected(m);if(id<0 || (size_t)id>=m->diff.count) return;
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
static void choose(GtkWidget *w,gpointer data) {
 Merge *m=data;int side=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),"side"));
 GtkWidget *d=gtk_file_chooser_dialog_new("Choose source file",GTK_WINDOW(m->window),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT) { gchar *p=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));gtk_entry_set_text(GTK_ENTRY(m->paths[side]),p);g_free(p); }
 gtk_widget_destroy(d);
}
static void compare(GtkWidget *w,gpointer data) {
 (void)w;Merge *m=data;gchar *text[2]={NULL,NULL};GError *error=NULL;InsightDiff diff={0};
 for(int i=0;i<2;i++) {
  gsize len=0;
  if(!g_file_get_contents(gtk_entry_get_text(GTK_ENTRY(m->paths[i])),&text[i],&len,&error)) goto failed;
  if(len>1024*1024 || memchr(text[i],0,len) || !g_utf8_validate(text[i],len,NULL)) {
   message(m,"First version supports UTF-8 text files up to 1 MiB, without NUL bytes.");goto done;
  }
 }
 int rc=insight_diff_compute(text[0],text[1],&diff);
 if(rc) { message(m,rc==-2 ? "Too many changed lines for the initial diff engine (4 million LCS cells)." : "Cannot allocate comparison.");goto done; }
 if(!discard(m)) goto done;
 m->changing=TRUE;clear_diff(m);m->diff=diff;diff=(InsightDiff){0};
 for(int i=0;i<2;i++) {
  g_free(m->sources[i]);m->sources[i]=text[i];text[i]=NULL;
  g_free(m->loaded_paths[i]);m->loaded_paths[i]=g_strdup(gtk_entry_get_text(GTK_ENTRY(m->paths[i])));
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
 m->changing=FALSE;record(m);
 gchar *label=g_strdup_printf("%zu differences. Result starts with Left; select a block and take Left or Right, or edit the result.",m->diff.count);message(m,label);g_free(label);
 gtk_widget_set_sensitive(m->external,TRUE);
 if(m->diff.count) { GtkTreePath *p=gtk_tree_path_new_first();gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view),p,NULL,FALSE);gtk_tree_path_free(p); }
 goto done;
failed:
 message(m,error->message);g_clear_error(&error);
done:
 g_free(text[0]);g_free(text[1]);insight_diff_clear(&diff);
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
void insight_diff_show(GtkWidget *parent) {
 Merge *m=g_new0(Merge,1);m->history=g_ptr_array_new_with_free_func(snapshot_free);m->saved=g_strdup("");
 m->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_transient_for(GTK_WINDOW(m->window),GTK_WINDOW(parent));gtk_window_set_destroy_with_parent(GTK_WINDOW(m->window),TRUE);gtk_window_set_default_size(GTK_WINDOW(m->window),1100,700);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(m->window),box);
 for(int i=0;i<2;i++) {
  GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(row),gtk_label_new(i ? "Right" : "Left"),FALSE,FALSE,0);
  m->paths[i]=gtk_entry_new();gtk_box_pack_start(GTK_BOX(row),m->paths[i],TRUE,TRUE,0);
  GtkWidget *browse=button(row,"Browse…",G_CALLBACK(choose),m);g_object_set_data(G_OBJECT(browse),"side",GINT_TO_POINTER(i));
 }
 GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
 button(bar,"Compare",G_CALLBACK(compare),m);m->take[0]=button(bar,"Take Left",G_CALLBACK(take),m);m->take[1]=button(bar,"Take Right",G_CALLBACK(take),m);
 m->undo=button(bar,"Undo",G_CALLBACK(undo),m);m->redo=button(bar,"Redo",G_CALLBACK(redo),m);button(bar,"Save result…",G_CALLBACK(save),m);
 m->external=button(bar,"External KDiff3 (optional)",G_CALLBACK(external),m);
 GtkWidget *panes=gtk_paned_new(GTK_ORIENTATION_VERTICAL);gtk_box_pack_start(GTK_BOX(box),panes,TRUE,TRUE,0);
 m->list=gtk_list_store_new(2,G_TYPE_INT,G_TYPE_STRING);m->hunk_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(m->list));
 gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(m->hunk_view),-1,"Differences",gtk_cell_renderer_text_new(),"text",1,NULL);
 m->selection=g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->hunk_view)));
 g_signal_connect(m->selection,"changed",G_CALLBACK(selection),m);
 GtkWidget *list_scroll=scroll(m->hunk_view);gtk_widget_set_size_request(list_scroll,-1,110);gtk_paned_pack1(GTK_PANED(panes),list_scroll,FALSE,FALSE);
 GtkWidget *columns=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_set_homogeneous(GTK_BOX(columns),TRUE);gtk_paned_pack2(GTK_PANED(panes),columns,TRUE,FALSE);
 for(int i=0;i<3;i++) {
  GtkWidget *column=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_box_pack_start(GTK_BOX(columns),column,TRUE,TRUE,0);
  gtk_box_pack_start(GTK_BOX(column),gtk_label_new(i==0 ? "Left (read only)" : i==1 ? "Right (read only)" : "Result (editable)"),FALSE,FALSE,0);
  m->views[i]=gtk_text_view_new();gtk_text_view_set_monospace(GTK_TEXT_VIEW(m->views[i]),TRUE);gtk_text_view_set_editable(GTK_TEXT_VIEW(m->views[i]),i==2);
  gtk_box_pack_start(GTK_BOX(column),scroll(m->views[i]),TRUE,TRUE,0);
  if(i<2) gtk_text_buffer_create_tag(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i])),"difference","background",i ? "#d5f5d5" : "#ffe0d0",NULL);
 }
 m->output=g_object_ref(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[2])));
 m->status=gtk_label_new("Choose two UTF-8 files and press Compare.");gtk_label_set_xalign(GTK_LABEL(m->status),0);gtk_label_set_line_wrap(GTK_LABEL(m->status),TRUE);gtk_box_pack_start(GTK_BOX(box),m->status,FALSE,FALSE,0);
 g_signal_connect(result(m),"begin-user-action",G_CALLBACK(action_begin),m);
 g_signal_connect(result(m),"end-user-action",G_CALLBACK(action_end),m);
 g_signal_connect(result(m),"changed",G_CALLBACK(changed),m);g_signal_connect(m->window,"key-press-event",G_CALLBACK(key),m);
 g_signal_connect(m->window,"delete-event",G_CALLBACK(closing),m);g_signal_connect(m->window,"destroy",G_CALLBACK(destroyed),m);
 record(m);selection(NULL,m);gtk_widget_show_all(m->window);
}
