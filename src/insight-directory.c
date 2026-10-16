/* SPDX-License-Identifier: GPL-2.0-or-later */
#define INSIGHT_LOCALIZE_FORMATS
#include "insight-directory.h"
#include "insight-diff.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include "../insight/desktop/directory_compare.h"
#include "../insight/core/diff.h"
#include <string.h>
enum { DC_ID,DC_PATH,DC_STATUS_TEXT,DC_LEFT,DC_RIGHT,DC_DETAIL,DC_STATUS,DC_PAIRED,DC_COLUMNS };
typedef struct {
 GtkWidget *window,*paths[2],*controls,*scan,*cancel_button,*view,*filter_entry,*different,*status,*open,*previous,*next;
 GtkWidget *method,*recursive,*hidden,*space,*eol;
 GPtrArray *rows,*pending;GtkListStore *store,*building;GtkTreeModel *filtered;
 gchar *roots[2],*pending_roots[2];guint flags,pending_flags,publication,at;GCancellable *cancel;gboolean busy;
} Directory;
typedef struct { gchar *paths[2];InsightDirectoryOptions options;GPtrArray *rows; } DirectoryJob;
static void job_free(gpointer data) { DirectoryJob *job=data;for(gint i=0;i<2;i++) g_free(job->paths[i]);if(job->rows) g_ptr_array_unref(job->rows);g_free(job); }
static const gchar *status_name(gint status) {
 static const gchar *names[]={"Same","Different","Left only","Right only","Type mismatch","Skipped","Read error"};
 return status>=0 && status<(gint)G_N_ELEMENTS(names) ? names[status] : "Unknown";
}
static gboolean visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 Directory *d=data;gint status;gchar *path;gtk_tree_model_get(model,iter,DC_STATUS,&status,DC_PATH,&path,-1);
 gboolean show=!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->different)) || status!=INSIGHT_DIR_SAME;
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(d->filter_entry));
 if(show && *query) { gchar *a=g_utf8_casefold(path,-1),*b=g_utf8_casefold(query,-1);show=strstr(a,b)!=NULL;g_free(a);g_free(b); }
 g_free(path);return show;
}
static InsightDirectoryEntry *selected_entry(Directory *d) {
 GtkTreeModel *model;GtkTreeIter iter;guint id;
 if(!d->rows || !gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(d->view)),&model,&iter)) return NULL;
 gtk_tree_model_get(model,&iter,DC_ID,&id,-1);return id<d->rows->len ? g_ptr_array_index(d->rows,id) : NULL;
}
static gboolean paired(InsightDirectoryEntry *e) {
 return e && e->info[0] && e->info[1] && !g_file_info_get_is_symlink(e->info[0]) && !g_file_info_get_is_symlink(e->info[1]) &&
  g_file_info_get_file_type(e->info[0])==G_FILE_TYPE_REGULAR && g_file_info_get_file_type(e->info[1])==G_FILE_TYPE_REGULAR;
}
static void selection_changed(GtkTreeSelection *selection,gpointer data) {
 (void)selection;Directory *d=data;gtk_widget_set_sensitive(d->open,!d->busy && paired(selected_entry(d)));
}
static void summary(Directory *d) {
 guint counts[7]={0};if(d->rows) for(guint i=0;i<d->rows->len;i++) { InsightDirectoryEntry *e=g_ptr_array_index(d->rows,i);counts[e->status]++; }
 guint shown=(guint)gtk_tree_model_iter_n_children(d->filtered,NULL);
 gchar *label=g_strdup_printf("%u shown / %u entries — %u same, %u different, %u one-sided, %u type, %u skipped, %u errors",shown,d->rows ? d->rows->len : 0,counts[0],counts[1],counts[2]+counts[3],counts[4],counts[5],counts[6]);
 gtk_label_set_text(GTK_LABEL(d->status),label);g_free(label);
}
static void refilter(GtkWidget *widget,gpointer data) {
 (void)widget;Directory *d=data;gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(d->filtered));selection_changed(NULL,d);if(!d->busy) summary(d);
}
static void busy(Directory *d,gboolean active) {
 d->busy=active;gtk_widget_set_sensitive(d->scan,!active);gtk_widget_set_sensitive(d->cancel_button,active);gtk_widget_set_sensitive(d->controls,!active);
 selection_changed(NULL,d);
}
static void clear_pending(Directory *d) {
 if(d->publication) { g_source_remove(d->publication);d->publication=0; }
 g_clear_pointer(&d->pending,g_ptr_array_unref);g_clear_object(&d->building);
 for(gint i=0;i<2;i++) g_clear_pointer(&d->pending_roots[i],g_free);
}
static void destroyed(GtkWidget *widget,gpointer data) {
 Directory *d=data;g_object_set_data(G_OBJECT(widget),"insight-directory-state",NULL);
 if(d->cancel) g_cancellable_cancel(d->cancel);
 clear_pending(d);g_clear_object(&d->cancel);g_clear_pointer(&d->rows,g_ptr_array_unref);
 for(gint i=0;i<2;i++) g_free(d->roots[i]);
 g_object_unref(d->store);g_object_unref(d->filtered);g_free(d);
}
static gchar *description(GFileInfo *info) {
 if(!info) return g_strdup("—");
 if(g_file_info_get_is_symlink(info)) return g_strdup(insight_tr("Link (not followed)"));
 if(g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY) return g_strdup(insight_tr("Folder"));
 if(g_file_info_get_file_type(info)!=G_FILE_TYPE_REGULAR) return g_strdup(insight_tr("Special file"));
 GDateTime *time=g_date_time_new_from_unix_local((gint64)g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_TIME_MODIFIED));
 gchar *date=time ? g_date_time_format(time,"%Y-%m-%d %H:%M:%S") : g_strdup("—"),*size=g_format_size((guint64)g_file_info_get_size(info));
 gchar *value=g_strdup_printf("%s · %s",size,date);g_free(date);g_free(size);if(time) g_date_time_unref(time);return value;
}
static void install_model(Directory *d) {
 GtkTreeModel *filtered=gtk_tree_model_filter_new(GTK_TREE_MODEL(d->store),NULL);gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtered),visible,d,NULL);
 GtkTreeModel *sorted=gtk_tree_model_sort_new_with_model(filtered);gtk_tree_view_set_model(GTK_TREE_VIEW(d->view),sorted);g_object_unref(sorted);g_object_unref(d->filtered);d->filtered=filtered;
}
static gboolean publish(gpointer data) {
 Directory *d=data;gint64 until=g_get_monotonic_time()+3000;
 while(d->at<d->pending->len) {
  InsightDirectoryEntry *e=g_ptr_array_index(d->pending,d->at);GtkTreeIter iter;
  gchar *path=g_filename_display_name(e->relative),*left=description(e->info[0]),*right=description(e->info[1]);
  gtk_list_store_append(d->building,&iter);gtk_list_store_set(d->building,&iter,DC_ID,d->at,DC_PATH,path,DC_STATUS_TEXT,insight_tr(status_name(e->status)),DC_LEFT,left,DC_RIGHT,right,DC_DETAIL,e->detail ? insight_tr(e->detail) : "",DC_STATUS,e->status,DC_PAIRED,paired(e),-1);
  g_free(path);g_free(left);g_free(right);d->at++;
  if(g_get_monotonic_time()>=until) break;
 }
 if(d->at<d->pending->len) return G_SOURCE_CONTINUE;
 d->publication=0;
 if(d->rows) g_ptr_array_unref(d->rows);
 d->rows=d->pending;d->pending=NULL;g_object_unref(d->store);d->store=d->building;d->building=NULL;
 for(gint i=0;i<2;i++) { g_free(d->roots[i]);d->roots[i]=d->pending_roots[i];d->pending_roots[i]=NULL; }
 d->flags=d->pending_flags;install_model(d);g_clear_object(&d->cancel);busy(d,FALSE);summary(d);return G_SOURCE_REMOVE;
}
static void worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;DirectoryJob *job=data;GError *error=NULL;
 job->rows=insight_directory_compare(job->paths[0],job->paths[1],&job->options,cancel,&error);
 if(!job->rows) g_task_return_error(task,error);else g_task_return_boolean(task,TRUE);
}
static void done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)data;GTask *task=G_TASK(result);GError *error=NULL;gboolean ok=g_task_propagate_boolean(task,&error);
 Directory *d=g_object_get_data(source,"insight-directory-state");if(!d) { g_clear_error(&error);return; }
 if(!ok) { g_clear_object(&d->cancel);busy(d,FALSE);gtk_label_set_text(GTK_LABEL(d->status),error->message);g_clear_error(&error);return; }
 DirectoryJob *job=g_task_get_task_data(task);d->pending=job->rows;job->rows=NULL;d->at=0;d->pending_flags=job->options.method==INSIGHT_DIR_CONTENT ? job->options.text_flags : 0;
 for(gint i=0;i<2;i++) d->pending_roots[i]=g_strdup(job->paths[i]);
 d->building=gtk_list_store_new(DC_COLUMNS,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_BOOLEAN);
 d->publication=g_idle_add(publish,d);
}
static void start(GtkWidget *widget,gpointer data) {
 (void)widget;Directory *d=data;if(d->busy) return;
 if(!*gtk_entry_get_text(GTK_ENTRY(d->paths[0])) || !*gtk_entry_get_text(GTK_ENTRY(d->paths[1]))) { gtk_label_set_text(GTK_LABEL(d->status),"Choose two directory paths before scanning.");return; }
 DirectoryJob *job=g_new0(DirectoryJob,1);job->options=insight_directory_options();
 for(gint i=0;i<2;i++) job->paths[i]=g_canonicalize_filename(gtk_entry_get_text(GTK_ENTRY(d->paths[i])),NULL);
 job->options.method=gtk_combo_box_get_active(GTK_COMBO_BOX(d->method));job->options.recursive=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->recursive));job->options.hidden=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->hidden));
 job->options.text_flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->space)) ? INSIGHT_DIFF_IGNORE_SPACE : 0) | (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->eol)) ? INSIGHT_DIFF_IGNORE_EOL : 0);
 d->cancel=g_cancellable_new();busy(d,TRUE);gtk_label_set_text(GTK_LABEL(d->status),"Scanning directories… Cancel keeps the previous result.");
 GTask *task=g_task_new(d->window,d->cancel,done,NULL);g_task_set_task_data(task,job,job_free);g_task_run_in_thread(task,worker);g_object_unref(task);
}
static void cancel_scan(GtkWidget *widget,gpointer data) {
 (void)widget;Directory *d=data;if(d->cancel) g_cancellable_cancel(d->cancel);
 if(d->publication) { clear_pending(d);g_clear_object(&d->cancel);busy(d,FALSE);gtk_label_set_text(GTK_LABEL(d->status),"Directory comparison cancelled; previous result kept."); }
}
static void choose(GtkWidget *widget,gpointer data) {
 Directory *d=data;gint side=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"side"));
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Choose comparison directory",GTK_WINDOW(d->window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Cancel",GTK_RESPONSE_CANCEL,"Select",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));gtk_entry_set_text(GTK_ENTRY(d->paths[side]),path);g_free(path); }
 gtk_widget_destroy(dialog);
}
static void open_pair(GtkWidget *widget,gpointer data) {
 (void)widget;Directory *d=data;InsightDirectoryEntry *e=selected_entry(d);if(d->busy || !paired(e)) return;
 gchar *left=g_build_filename(d->roots[0],e->relative,NULL),*right=g_build_filename(d->roots[1],e->relative,NULL);
 insight_diff_show_files(NULL,left,right,d->flags);g_free(left);g_free(right);
}
static void activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column;gtk_tree_view_set_cursor(view,path,NULL,FALSE);open_pair(NULL,data);
}
static void advance(Directory *d,gint direction) {
 GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(d->view));GtkTreeIter iter;GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(d->view));
 gboolean valid=gtk_tree_selection_get_selected(selection,NULL,&iter);
 if(!valid) { gint count=gtk_tree_model_iter_n_children(model,NULL);if(!count) return;gtk_tree_model_iter_nth_child(model,&iter,NULL,direction>0 ? 0 : count-1); }
 else valid=direction>0 ? gtk_tree_model_iter_next(model,&iter) : gtk_tree_model_iter_previous(model,&iter);
 if(!valid && gtk_tree_selection_count_selected_rows(selection)) return;
 do {
  gint status;gtk_tree_model_get(model,&iter,DC_STATUS,&status,-1);
  if(status!=INSIGHT_DIR_SAME) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gtk_tree_view_set_cursor(GTK_TREE_VIEW(d->view),path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(d->view),path,NULL,FALSE,0,0);gtk_tree_path_free(path);return; }
 } while(direction>0 ? gtk_tree_model_iter_next(model,&iter) : gtk_tree_model_iter_previous(model,&iter));
}
static void step(GtkWidget *widget,gpointer data) { Directory *d=data;advance(d,widget==d->next ? 1 : -1); }
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 Directory *d=data;if(event->keyval==GDK_KEY_F5) { start(NULL,d);return TRUE; }
 if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_Down)) { advance(d,event->keyval==GDK_KEY_Up ? -1 : 1);return TRUE; }
 if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_f || event->keyval==GDK_KEY_F)) { gtk_widget_grab_focus(d->filter_entry);return TRUE; }
 if(widget==d->view && (event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter)) { open_pair(NULL,d);return TRUE; }return FALSE;
}
static GtkWidget *button(GtkWidget *box,const gchar *label,GCallback callback,Directory *d) {
 GtkWidget *widget=gtk_button_new_with_label(label);gtk_box_pack_start(GTK_BOX(box),widget,FALSE,FALSE,0);g_signal_connect(widget,"clicked",callback,d);return widget;
}
static GtkWidget *check(GtkWidget *box,const gchar *label,gboolean active) {
 GtkWidget *widget=gtk_check_button_new_with_label(label);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),active);gtk_box_pack_start(GTK_BOX(box),widget,FALSE,FALSE,0);return widget;
}
static Directory *directory_new(GtkWidget *parent) {
 Directory *d=g_new0(Directory,1);d->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(d->window),"Directory Compare");gtk_window_set_default_size(GTK_WINDOW(d->window),1100,650);
 if(parent) { gtk_window_set_transient_for(GTK_WINDOW(d->window),GTK_WINDOW(parent));gtk_window_set_destroy_with_parent(GTK_WINDOW(d->window),TRUE); }
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(d->window),box);insight_panel_help_attach(box,"directory-compare.md");
 d->controls=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_box_pack_start(GTK_BOX(box),d->controls,FALSE,FALSE,0);
 for(gint side=0;side<2;side++) {
  GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(d->controls),row,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),gtk_label_new(side ? "Right directory" : "Left directory"),FALSE,FALSE,0);
  d->paths[side]=gtk_entry_new();gtk_box_pack_start(GTK_BOX(row),d->paths[side],TRUE,TRUE,0);GtkWidget *browse=button(row,"Browse…",G_CALLBACK(choose),d);g_object_set_data(G_OBJECT(browse),"side",GINT_TO_POINTER(side));g_signal_connect(d->paths[side],"activate",G_CALLBACK(start),d);
 }
 GtkWidget *options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(d->controls),options,FALSE,FALSE,0);
 d->method=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->method),"Compare contents");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->method),"Compare modification dates");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->method),"Compare sizes and dates");gtk_combo_box_set_active(GTK_COMBO_BOX(d->method),0);gtk_box_pack_start(GTK_BOX(options),d->method,FALSE,FALSE,0);
 d->recursive=check(options,"Include subdirectories",TRUE);d->hidden=check(options,"Include hidden entries",FALSE);
 GtkWidget *text_options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(d->controls),text_options,FALSE,FALSE,0);d->space=check(text_options,"Ignore spaces and tabs",FALSE);d->eol=check(text_options,"Ignore line ending style",FALSE);
 GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
 d->scan=button(bar,"Scan / Refresh",G_CALLBACK(start),d);d->cancel_button=button(bar,"Cancel scan",G_CALLBACK(cancel_scan),d);d->open=button(bar,"Compare selected file",G_CALLBACK(open_pair),d);d->previous=button(bar,"Previous different file",G_CALLBACK(step),d);d->next=button(bar,"Next different file",G_CALLBACK(step),d);
 GtkWidget *filters=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),filters,FALSE,FALSE,0);d->filter_entry=gtk_search_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(d->filter_entry),"Filter relative paths — Ctrl+F");gtk_box_pack_start(GTK_BOX(filters),d->filter_entry,TRUE,TRUE,0);d->different=check(filters,"Differences only",FALSE);
 d->store=gtk_list_store_new(DC_COLUMNS,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_BOOLEAN);
 d->filtered=gtk_tree_model_filter_new(GTK_TREE_MODEL(d->store),NULL);gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(d->filtered),visible,d,NULL);GtkTreeModel *sorted=gtk_tree_model_sort_new_with_model(d->filtered);d->view=gtk_tree_view_new_with_model(sorted);g_object_unref(sorted);gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(d->view),DC_DETAIL);
 const gchar *labels[]={"Relative path","Status","Left — size / modified","Right — size / modified"};
 for(gint i=0;i<4;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(labels[i],gtk_cell_renderer_text_new(),"text",DC_PATH+i,NULL);gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_column_set_sort_column_id(column,DC_PATH+i);gtk_tree_view_append_column(GTK_TREE_VIEW(d->view),column); }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),d->view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 d->status=gtk_label_new("Choose two directories and scan. Results are a snapshot.");gtk_label_set_xalign(GTK_LABEL(d->status),0);gtk_label_set_line_wrap(GTK_LABEL(d->status),TRUE);gtk_box_pack_start(GTK_BOX(box),d->status,FALSE,FALSE,0);
 g_signal_connect(d->filter_entry,"changed",G_CALLBACK(refilter),d);g_signal_connect(d->different,"toggled",G_CALLBACK(refilter),d);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(d->view)),"changed",G_CALLBACK(selection_changed),d);
 g_signal_connect(d->view,"row-activated",G_CALLBACK(activated),d);g_signal_connect(d->view,"key-press-event",G_CALLBACK(key),d);g_signal_connect(d->window,"key-press-event",G_CALLBACK(key),d);g_signal_connect(d->window,"destroy",G_CALLBACK(destroyed),d);g_object_set_data(G_OBJECT(d->window),"insight-directory-state",d);busy(d,FALSE);gtk_widget_show_all(d->window);return d;
}
void insight_directory_show(GtkWidget *parent) { (void)directory_new(parent); }
