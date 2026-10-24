/* SPDX-License-Identifier: GPL-2.0-or-later */
#define INSIGHT_LOCALIZE_FORMATS
#include "insight-compare-ui.h"
#include "insight-directory.h"
#include "../insight/desktop/glib_compat.h"
#include "insight-diff.h"
#include "insight-help.h"
#include "insight-list-style.h"
#include "insight-i18n.h"
#include "../insight/desktop/directory_compare.h"
#include "../insight/desktop/directory_operation.h"
#include "../insight/core/diff.h"
#include <string.h>
static InsightDirectoryTypes types_provider;
static InsightDirectoryOpen open_guard;
void insight_directory_set_open_guard(InsightDirectoryOpen guard) { open_guard=guard; }
void insight_directory_set_types_provider(InsightDirectoryTypes provider) { types_provider=provider; }
enum { DC_ID,DC_PATH,DC_STATUS_TEXT,DC_LEFT,DC_RIGHT,DC_DETAIL,DC_STATUS,DC_PAIRED,DC_COLUMNS };
typedef struct {
 GtkWidget *window,*paths[2],*controls,*scan,*cancel_button,*view,*filter_entry,*different,*status,*open,*previous,*next;
 GtkWidget *actions[4];guint64 epoch;gboolean stale,operating;
 GtkWidget *confirm,*known,*method,*recursive,*hidden,*space,*eol,*include,*exclude,*status_filter;
 GPtrArray *include_patterns,*exclude_patterns;
 GPtrArray *rows,*pending;GtkListStore *store,*building;GtkTreeModel *filtered;
 gchar *roots[2],*pending_roots[2];guint flags,pending_flags,publication,at;GCancellable *cancel;gboolean busy;
} Directory;
typedef struct { gchar **known_patterns;gchar *paths[2];InsightDirectoryOptions options;GPtrArray *rows; } DirectoryJob;
static GKeyFile *saved_options;
static GList *open_windows;
static const gchar *options_group="directory-compare",*style_group="directory-compare-style";
static void copy_options(GKeyFile *from,GKeyFile *to) {
 const gchar *groups[]={options_group,style_group};
 for(guint i=0;i<G_N_ELEMENTS(groups);i++) {
  g_key_file_remove_group(to,groups[i],NULL);gchar **keys=g_key_file_get_keys(from,groups[i],NULL,NULL);
  for(guint n=0;keys && keys[n];n++) { gchar *value=g_key_file_get_value(from,groups[i],keys[n],NULL);if(value && strlen(value)<=65536) g_key_file_set_value(to,groups[i],keys[n],value);g_free(value); }g_strfreev(keys);
 }
}
static void options_write(Directory *d,GKeyFile *key) {
 const gchar *names[]={"left","right","search","include","exclude"};GtkWidget *entries[]={d->paths[0],d->paths[1],d->filter_entry,d->include,d->exclude};
 g_key_file_set_integer(key,options_group,"version",1);
 for(guint i=0;i<G_N_ELEMENTS(names);i++) g_key_file_set_string(key,options_group,names[i],gtk_entry_get_text(GTK_ENTRY(entries[i])));
 const gchar *checks[]={"recursive","hidden","space","eol","known","different","confirm"};GtkWidget *toggles[]={d->recursive,d->hidden,d->space,d->eol,d->known,d->different,d->confirm};
 for(guint i=0;i<G_N_ELEMENTS(checks);i++) g_key_file_set_boolean(key,options_group,checks[i],gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggles[i])));
 g_key_file_set_integer(key,options_group,"method",gtk_combo_box_get_active(GTK_COMBO_BOX(d->method)));g_key_file_set_integer(key,options_group,"status",gtk_combo_box_get_active(GTK_COMBO_BOX(d->status_filter)));
 gint width,height,column;GtkSortType order;gtk_window_get_size(GTK_WINDOW(d->window),&width,&height);g_key_file_set_integer(key,options_group,"width",CLAMP(width,640,4000));g_key_file_set_integer(key,options_group,"height",CLAMP(height,400,2400));
 if(gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(d->view))),&column,&order)) { g_key_file_set_integer(key,options_group,"sort",column);g_key_file_set_integer(key,options_group,"order",order); }
 else g_key_file_remove_key(key,options_group,"sort",NULL);
 for(gint i=0;i<4;i++) { GtkTreeViewColumn *c=gtk_tree_view_get_column(GTK_TREE_VIEW(d->view),i);gchar *name=g_strdup_printf("column-%d",i);g_key_file_set_boolean(key,options_group,name,gtk_tree_view_column_get_visible(c));g_free(name); }
 insight_list_style_write(d->view,key,style_group);
}
static gboolean option_integer(GKeyFile *key,const gchar *name,gint low,gint high,gint *out) {
 GError *error=NULL;gint value=g_key_file_get_integer(key,options_group,name,&error);gboolean valid=!error && value>=low && value<=high;g_clear_error(&error);if(valid) *out=value;return valid;
}
static void options_read(Directory *d,GKeyFile *key) {
 if(!key || g_key_file_get_integer(key,options_group,"version",NULL)!=1) return;
 const gchar *names[]={"left","right","search","include","exclude"};GtkWidget *entries[]={d->paths[0],d->paths[1],d->filter_entry,d->include,d->exclude};
 for(guint i=0;i<G_N_ELEMENTS(names);i++) { gchar *text=g_key_file_get_string(key,options_group,names[i],NULL);if(text && strlen(text)<=(i<2 ? 16384u : 2048u) && g_utf8_validate(text,-1,NULL)) gtk_entry_set_text(GTK_ENTRY(entries[i]),text);g_free(text); }
 const gchar *checks[]={"recursive","hidden","space","eol","known","different","confirm"};GtkWidget *toggles[]={d->recursive,d->hidden,d->space,d->eol,d->known,d->different,d->confirm};
 for(guint i=0;i<G_N_ELEMENTS(checks);i++) { GError *error=NULL;gboolean value=g_key_file_get_boolean(key,options_group,checks[i],&error);if(!error && (i!=4 || types_provider)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggles[i]),value);g_clear_error(&error); }
 gint value;if(option_integer(key,"method",0,2,&value)) gtk_combo_box_set_active(GTK_COMBO_BOX(d->method),value);if(option_integer(key,"status",0,8,&value)) gtk_combo_box_set_active(GTK_COMBO_BOX(d->status_filter),value);
 gint width=INSIGHT_COMPARE_WIDTH,height=INSIGHT_COMPARE_HEIGHT;option_integer(key,"width",640,4000,&width);option_integer(key,"height",400,2400,&height);gtk_window_set_default_size(GTK_WINDOW(d->window),width,height);
 gint column,order;if(option_integer(key,"sort",DC_PATH,DC_STATUS_TEXT,&column) && option_integer(key,"order",GTK_SORT_ASCENDING,GTK_SORT_DESCENDING,&order)) gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(d->view))),column,order);
 for(gint i=1;i<4;i++) { gchar *name=g_strdup_printf("column-%d",i);GError *error=NULL;gboolean show=g_key_file_get_boolean(key,options_group,name,&error);if(!error) gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(d->view),i),show);g_clear_error(&error);g_free(name); }
 insight_list_style_read(d->view,key,style_group);
}
void insight_directory_read(GKeyFile *settings) {
 if(!saved_options) saved_options=g_key_file_new();
 copy_options(settings,saved_options);
}
void insight_directory_write(GKeyFile *settings) {
 if(!saved_options) saved_options=g_key_file_new();
 if(open_windows) options_write(open_windows->data,saved_options);
 copy_options(saved_options,settings);
}
static void job_free(gpointer data) { DirectoryJob *job=data;g_strfreev(job->known_patterns);for(gint i=0;i<2;i++) g_free(job->paths[i]);if(job->rows) g_ptr_array_unref(job->rows);g_free(job); }
static const gchar *status_name(gint status) {
 static const gchar *names[]={"Same","Different","Left only","Right only","Type mismatch","Skipped","Read error","Folder"};
 return status>=0 && status<(gint)G_N_ELEMENTS(names) ? names[status] : "Unknown";
}
static const gchar *entry_status(InsightDirectoryEntry *entry) {
 if(entry->status==INSIGHT_DIR_DIFFERENT && entry->info[0] && entry->info[1]) {
  guint64 a=g_file_info_get_attribute_uint64(entry->info[0],G_FILE_ATTRIBUTE_TIME_MODIFIED),b=g_file_info_get_attribute_uint64(entry->info[1],G_FILE_ATTRIBUTE_TIME_MODIFIED);
  guint32 au=g_file_info_get_attribute_uint32(entry->info[0],G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC),bu=g_file_info_get_attribute_uint32(entry->info[1],G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
  if(a>b || (a==b && au>bu)) return "Different — left newer";
  if(b>a || (a==b && bu>au)) return "Different — right newer";
 }
 return status_name(entry->status);
}
/* Compile on edits, never for every row. Match relative paths and basenames. */
static GPtrArray *patterns(const gchar *text) {
 GPtrArray *out=g_ptr_array_new_with_free_func((GDestroyNotify)g_pattern_spec_free);
 gchar **parts=g_strsplit(text,";",-1);
 for(guint i=0;parts[i];i++) { gchar *part=g_strstrip(parts[i]);if(*part) g_ptr_array_add(out,g_pattern_spec_new(part)); }
 g_strfreev(parts);return out;
}
static gboolean matches(GPtrArray *specs,const gchar *path) {
 gchar *base=g_path_get_basename(path);gboolean found=FALSE;
 for(guint i=0;specs && i<specs->len && !found;i++) {
  GPatternSpec *spec=g_ptr_array_index(specs,i);
#if GLIB_CHECK_VERSION(2,70,0)
  found=insight_pattern_match_string(spec,path) || insight_pattern_match_string(spec,base);
#else
  found=g_pattern_match_string(spec,path) || g_pattern_match_string(spec,base);
#endif
 }
 g_free(base);return found;
}
static gboolean visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 Directory *d=data;gint status;gchar *path;gtk_tree_model_get(model,iter,DC_STATUS,&status,DC_PATH,&path,-1);
 gboolean show=!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->different)) || (status!=INSIGHT_DIR_SAME && status!=INSIGHT_DIR_FOLDER);
 gint wanted=gtk_combo_box_get_active(GTK_COMBO_BOX(d->status_filter))-1;
 if(show && wanted>=0) show=status==wanted;
 if(show && d->include_patterns && d->include_patterns->len) show=matches(d->include_patterns,path);
 if(show && d->exclude_patterns && d->exclude_patterns->len) show=!matches(d->exclude_patterns,path);
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
static gboolean regular_side(InsightDirectoryEntry *entry,guint side) {
 return entry && entry->info[side] && !g_file_info_get_is_symlink(entry->info[side]) && g_file_info_get_file_type(entry->info[side])==G_FILE_TYPE_REGULAR;
}
static gboolean action_available(Directory *d,guint action) {
 InsightDirectoryEntry *entry=selected_entry(d);guint side=action%2;
 return open_guard && !d->busy && !d->stale && regular_side(entry,side) && (action>=INSIGHT_DIR_TRASH_L || !entry->info[1-side] || regular_side(entry,1-side));
}
static void selection_changed(GtkTreeSelection *selection,gpointer data) {
 (void)selection;Directory *d=data;
 for(guint i=0;i<4;i++) if(d->actions[i]) gtk_widget_set_sensitive(d->actions[i],action_available(d,i));
 gtk_widget_set_sensitive(d->open,!d->busy && paired(selected_entry(d)));
 gtk_widget_set_sensitive(d->previous,!d->busy && d->rows && d->rows->len);gtk_widget_set_sensitive(d->next,!d->busy && d->rows && d->rows->len);
}
static void summary(Directory *d) {
 guint counts[8]={0};if(d->rows) for(guint i=0;i<d->rows->len;i++) { InsightDirectoryEntry *e=g_ptr_array_index(d->rows,i);counts[e->status]++; }
 guint shown=(guint)gtk_tree_model_iter_n_children(d->filtered,NULL);
 gchar *label=g_strdup_printf("%u shown / %u entries — %u same, %u different, %u one-sided, %u type, %u skipped, %u errors, %u folders",shown,d->rows ? d->rows->len : 0,counts[0],counts[1],counts[2]+counts[3],counts[4],counts[5],counts[6],counts[7]);
 if(d->stale) { gchar *notice=g_strconcat(insight_tr("Snapshot may be stale; refresh before file operations.")," ",label,NULL);gtk_label_set_text(GTK_LABEL(d->status),notice);g_free(notice); }else gtk_label_set_text(GTK_LABEL(d->status),label);g_free(label);
}
static void refilter(GtkWidget *widget,gpointer data) {
 (void)widget;Directory *d=data;gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(d->filtered));selection_changed(NULL,d);if(!d->busy) summary(d);
}
static void masks_changed(GtkWidget *widget,gpointer data) {
 Directory *d=data;(void)widget;
 g_clear_pointer(&d->include_patterns,g_ptr_array_unref);g_clear_pointer(&d->exclude_patterns,g_ptr_array_unref);
 d->include_patterns=patterns(gtk_entry_get_text(GTK_ENTRY(d->include)));d->exclude_patterns=patterns(gtk_entry_get_text(GTK_ENTRY(d->exclude)));refilter(NULL,d);
}
static void busy(Directory *d,gboolean active) {
 d->busy=active;gtk_widget_set_sensitive(d->scan,!active);gtk_widget_set_sensitive(d->cancel_button,active);gtk_widget_set_sensitive(d->controls,!active);gtk_widget_set_sensitive(gtk_widget_get_parent(d->method),!active);gtk_widget_set_sensitive(gtk_widget_get_parent(d->space),!active);
 selection_changed(NULL,d);
}
static void clear_pending(Directory *d) {
 if(d->publication) { g_source_remove(d->publication);d->publication=0; }
 g_clear_pointer(&d->pending,g_ptr_array_unref);g_clear_object(&d->building);
 for(gint i=0;i<2;i++) g_clear_pointer(&d->pending_roots[i],g_free);
}
static void destroyed(GtkWidget *widget,gpointer data) {
 Directory *d=data;
 if(!saved_options) saved_options=g_key_file_new();
 options_write(d,saved_options);open_windows=g_list_remove(open_windows,d);
 g_object_set_data(G_OBJECT(widget),"insight-directory-state",NULL);
 if(d->cancel) g_cancellable_cancel(d->cancel);
 clear_pending(d);g_clear_object(&d->cancel);g_clear_pointer(&d->rows,g_ptr_array_unref);
 for(gint i=0;i<2;i++) g_free(d->roots[i]);
 g_clear_pointer(&d->include_patterns,g_ptr_array_unref);g_clear_pointer(&d->exclude_patterns,g_ptr_array_unref);
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
 GtkTreeModel *sorted=gtk_tree_model_sort_new_with_model(filtered);gint column;GtkSortType order;
 if(gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(d->view))),&column,&order)) gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(sorted),column,order);
 gtk_tree_view_set_model(GTK_TREE_VIEW(d->view),sorted);g_object_unref(sorted);g_object_unref(d->filtered);d->filtered=filtered;
}
static void restore_selection(Directory *d,const gchar *relative) {
 if(!relative) return;
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(d->view));GtkTreeIter iter;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) {
  guint id;gtk_tree_model_get(model,&iter,DC_ID,&id,-1);
  InsightDirectoryEntry *entry=id<d->rows->len ? g_ptr_array_index(d->rows,id) : NULL;
  if(entry && !strcmp(entry->relative,relative)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gtk_tree_view_set_cursor(GTK_TREE_VIEW(d->view),path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(d->view),path,NULL,FALSE,0,0);gtk_tree_path_free(path);break; }
 }
}
static gboolean publish(gpointer data) {
 Directory *d=data;gint64 until=g_get_monotonic_time()+3000;
 while(d->at<d->pending->len) {
  InsightDirectoryEntry *e=g_ptr_array_index(d->pending,d->at);GtkTreeIter iter;
  gchar *path=g_filename_display_name(e->relative),*left=description(e->info[0]),*right=description(e->info[1]);
  gtk_list_store_append(d->building,&iter);gtk_list_store_set(d->building,&iter,DC_ID,d->at,DC_PATH,path,DC_STATUS_TEXT,insight_tr(entry_status(e)),DC_LEFT,left,DC_RIGHT,right,DC_DETAIL,e->detail ? insight_tr(e->detail) : "",DC_STATUS,e->status,DC_PAIRED,paired(e),-1);
  g_free(path);g_free(left);g_free(right);d->at++;
  if(g_get_monotonic_time()>=until) break;
 }
 if(d->at<d->pending->len) return G_SOURCE_CONTINUE;
 d->publication=0;
 /* Read the old selected ID before replacing its backing snapshot. */
 InsightDirectoryEntry *selected=selected_entry(d);gchar *relative=selected ? g_strdup(selected->relative) : NULL;
 if(d->rows) g_ptr_array_unref(d->rows);
 d->rows=d->pending;d->pending=NULL;g_object_unref(d->store);d->store=d->building;d->building=NULL;
 for(gint i=0;i<2;i++) { g_free(d->roots[i]);d->roots[i]=d->pending_roots[i];d->pending_roots[i]=NULL; }
 d->flags=d->pending_flags;d->stale=FALSE;install_model(d);restore_selection(d,relative);g_free(relative);g_clear_object(&d->cancel);busy(d,FALSE);summary(d);return G_SOURCE_REMOVE;
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
 for(gint i=0;i<2;i++) {
  GError *error=NULL;gchar *native=g_filename_from_utf8(gtk_entry_get_text(GTK_ENTRY(d->paths[i])),-1,NULL,NULL,&error);
  if(!native) { gtk_label_set_text(GTK_LABEL(d->status),error->message);g_clear_error(&error);job_free(job);return; }
  job->paths[i]=insight_canonicalize_filename(native,NULL);g_free(native);
 }
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->known))) {
  job->known_patterns=types_provider ? types_provider() : NULL;
  if(!job->known_patterns) { gtk_label_set_text(GTK_LABEL(d->status),"Known file type definitions are unavailable.");job_free(job);return; }
  job->options.known_patterns=(const gchar *const *)job->known_patterns;
 }
 job->options.method=gtk_combo_box_get_active(GTK_COMBO_BOX(d->method));job->options.recursive=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->recursive));job->options.hidden=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->hidden));
 job->options.text_flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->space)) ? INSIGHT_DIFF_IGNORE_SPACE : 0) | (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->eol)) ? INSIGHT_DIFF_IGNORE_EOL : 0);
 d->epoch++;d->cancel=g_cancellable_new();busy(d,TRUE);gtk_label_set_text(GTK_LABEL(d->status),"Scanning directories… Cancel keeps the previous result.");
 GTask *task=g_task_new(d->window,d->cancel,done,NULL);g_task_set_task_data(task,job,job_free);g_task_run_in_thread(task,worker);g_object_unref(task);
}
static void swap_and_scan(GtkWidget *widget,gpointer data) {
 (void)widget;Directory *d=data;if(d->busy) return;
 gchar *left=g_strdup(gtk_entry_get_text(GTK_ENTRY(d->paths[0])));
 gtk_entry_set_text(GTK_ENTRY(d->paths[0]),gtk_entry_get_text(GTK_ENTRY(d->paths[1])));gtk_entry_set_text(GTK_ENTRY(d->paths[1]),left);g_free(left);start(NULL,d);
}
static void reset_filters(GtkWidget *widget,gpointer data) {
 (void)widget;Directory *d=data;gtk_entry_set_text(GTK_ENTRY(d->filter_entry),"");gtk_entry_set_text(GTK_ENTRY(d->include),"");gtk_entry_set_text(GTK_ENTRY(d->exclude),"");
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->different),FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(d->status_filter),0);
}
static void cancel_scan(GtkWidget *widget,gpointer data) {
 (void)widget;Directory *d=data;if(d->cancel) g_cancellable_cancel(d->cancel);
 if(d->publication) { clear_pending(d);g_clear_object(&d->cancel);busy(d,FALSE);gtk_label_set_text(GTK_LABEL(d->status),"Directory comparison cancelled; previous result kept."); }
}
static void choose(GtkWidget *widget,gpointer data) {
 Directory *d=data;gint side=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"side"));
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Choose comparison directory",GTK_WINDOW(d->window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Cancel",GTK_RESPONSE_CANCEL,"Select",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));GError *error=NULL;gchar *display=g_filename_to_utf8(path,-1,NULL,NULL,&error);
  if(display) gtk_entry_set_text(GTK_ENTRY(d->paths[side]),display);else { gtk_label_set_text(GTK_LABEL(d->status),error->message);g_clear_error(&error); }
  g_free(display);g_free(path); }
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
  if(status!=INSIGHT_DIR_SAME && status!=INSIGHT_DIR_FOLDER) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gtk_tree_view_set_cursor(GTK_TREE_VIEW(d->view),path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(d->view),path,NULL,FALSE,0,0);gtk_tree_path_free(path);return; }
 } while(direction>0 ? gtk_tree_model_iter_next(model,&iter) : gtk_tree_model_iter_previous(model,&iter));
}
static void step(GtkWidget *widget,gpointer data) { Directory *d=data;advance(d,widget==d->next ? 1 : -1); }
static void append_field(GString *text,const gchar *field) {
 for(const gchar *p=field;*p;p++) {
  if(*p=='\t') g_string_append(text,"\\t");else if(*p=='\r') g_string_append(text,"\\r");
  else if(*p=='\n') g_string_append(text,"\\n");else if(*p=='\\') g_string_append(text,"\\\\");else g_string_append_c(text,*p);
 }
}
static void copy_list(GtkWidget *widget,gpointer data) {
 (void)widget;Directory *d=data;GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(d->view));GtkTreeIter iter;GString *text=g_string_new("Relative path\tStatus\tLeft\tRight\n");
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) {
  for(gint column=DC_PATH;column<=DC_RIGHT;column++) { gchar *value;gtk_tree_model_get(model,&iter,column,&value,-1);append_field(text,value);g_free(value);g_string_append_c(text,column==DC_RIGHT ? '\n' : '\t'); }
 }
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text->str,(gint)text->len);g_string_free(text,TRUE);
}
static void copy_path(GtkWidget *widget,gpointer data) {
 (void)widget;Directory *d=data;InsightDirectoryEntry *entry=selected_entry(d);if(!entry) return;
 gchar *path=g_filename_display_name(entry->relative);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),path,-1);g_free(path);
}
static void copy_absolute(GtkWidget *widget,gpointer data) {
 Directory *d=data;InsightDirectoryEntry *entry=selected_entry(d);gint side=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"side"));if(!entry || !entry->info[side] || !d->roots[side]) return;
 gchar *path=g_build_filename(d->roots[side],entry->relative,NULL);GError *error=NULL;gchar *text=g_filename_to_utf8(path,-1,NULL,NULL,&error);
 if(text) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text,-1);else gtk_label_set_text(GTK_LABEL(d->status),error->message);
 g_clear_error(&error);g_free(text);g_free(path);
}
typedef struct { gchar *roots[2],*relative;GFileInfo *info[2];guint action;guint64 epoch; } FileJob;
static void file_job_free(gpointer data) {
 FileJob *job=data;for(gint side=0;side<2;side++) { g_free(job->roots[side]);g_clear_object(&job->info[side]); }g_free(job->relative);g_free(job);
}
static void file_worker(GTask *task,gpointer object,gpointer data,GCancellable *cancel) {
 (void)object;FileJob *job=data;GError *error=NULL;
 if(insight_directory_operation((const gchar *const *)job->roots,job->relative,job->info,job->action,cancel,&error)) g_task_return_boolean(task,TRUE);else g_task_return_error(task,error);
}
static void file_done(GObject *object,GAsyncResult *result,gpointer data) {
 (void)data;GError *error=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);Directory *d=g_object_get_data(object,"insight-directory-state");
 if(d) { gtk_window_set_modal(GTK_WINDOW(d->window),FALSE);d->operating=FALSE;d->stale=TRUE;g_clear_object(&d->cancel);gtk_button_set_label(GTK_BUTTON(d->cancel_button),insight_tr("Cancel scan"));busy(d,FALSE);
  gchar *text=g_strconcat(ok ? insight_tr("File operation completed.") : insight_tr(error ? error->message : "File operation failed.")," ",insight_tr("Snapshot may be stale; refresh before file operations."),NULL);gtk_label_set_text(GTK_LABEL(d->status),text);g_free(text);
 }g_clear_error(&error);
}
static void file_action(GtkWidget *widget,gpointer data) {
 Directory *d=data;guint action=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget),"file-action"));if(!action_available(d,action)) return;
 InsightDirectoryEntry *entry=selected_entry(d);FileJob *job=g_new0(FileJob,1);job->action=action;job->epoch=d->epoch;job->relative=g_strdup(entry->relative);
 for(gint side=0;side<2;side++) { job->roots[side]=g_strdup(d->roots[side]);job->info[side]=entry->info[side] ? g_object_ref(entry->info[side]) : NULL; }
 guint side=action%2;gboolean trash=action>=INSIGHT_DIR_TRASH_L;gchar *source=g_build_filename(job->roots[side],job->relative,NULL),*destination=trash ? NULL : g_build_filename(job->roots[1-side],job->relative,NULL);
 if(open_guard(source) || (destination && open_guard(destination))) { gtk_label_set_text(GTK_LABEL(d->status),insight_tr("Close documents using either operation path before changing files."));file_job_free(job);g_free(source);g_free(destination);return; }
 GtkWidget *window=d->window;g_object_ref(window);
 gint response=GTK_RESPONSE_ACCEPT;
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->confirm))) {
 GtkWidget *dialog=gtk_dialog_new_with_buttons(trash ? "Move comparison file to Trash" : "Copy comparison file",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,trash ? "Move to Trash" : "Copy file",GTK_RESPONSE_ACCEPT,NULL);gtk_widget_set_name(dialog,"insight-directory-operation-dialog");
 GtkWidget *content=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_container_set_border_width(GTK_CONTAINER(content),10);
 gchar *a=g_filename_display_name(source),*b=destination ? g_filename_display_name(destination) : NULL;
 const gchar *notice=trash ? "Move this regular file to the system trash? Permanent deletion is never used." : job->info[1-side] ? "Replace the existing destination with the source file bytes?" : "Create the missing destination from the source file bytes?";
 GtkWidget *label=gtk_label_new(insight_tr(notice));gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);gtk_box_pack_start(GTK_BOX(content),label,FALSE,FALSE,4);
 gtk_box_pack_start(GTK_BOX(content),gtk_label_new(insight_tr("Source file")),FALSE,FALSE,2);
 GtkWidget *from=gtk_label_new(a);gtk_label_set_selectable(GTK_LABEL(from),TRUE);gtk_label_set_line_wrap(GTK_LABEL(from),TRUE);gtk_label_set_max_width_chars(GTK_LABEL(from),90);gtk_widget_set_name(from,"insight-directory-operation-source");gtk_box_pack_start(GTK_BOX(content),from,FALSE,FALSE,4);
 if(b) { gtk_box_pack_start(GTK_BOX(content),gtk_label_new(insight_tr("Destination file")),FALSE,FALSE,2);GtkWidget *to=gtk_label_new(b);gtk_label_set_selectable(GTK_LABEL(to),TRUE);gtk_label_set_line_wrap(GTK_LABEL(to),TRUE);gtk_label_set_max_width_chars(GTK_LABEL(to),90);gtk_widget_set_name(to,"insight-directory-operation-destination");gtk_box_pack_start(GTK_BOX(content),to,FALSE,FALSE,4); }
 gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);insight_i18n_apply(dialog);gtk_widget_show_all(dialog);response=gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);g_free(a);g_free(b);
 }
 d=g_object_get_data(G_OBJECT(window),"insight-directory-state");
 if(response==GTK_RESPONSE_ACCEPT && d) {
  if(d->busy || d->stale || d->epoch!=job->epoch) gtk_label_set_text(GTK_LABEL(d->status),insight_tr("Comparison changed; reopen the file operation."));
  else if(!open_guard || open_guard(source) || (destination && open_guard(destination))) gtk_label_set_text(GTK_LABEL(d->status),insight_tr("Close documents using either operation path before changing files."));
  else { d->epoch++;d->stale=TRUE;d->operating=TRUE;gtk_window_set_modal(GTK_WINDOW(d->window),TRUE);d->cancel=g_cancellable_new();busy(d,TRUE);gtk_button_set_label(GTK_BUTTON(d->cancel_button),insight_tr("Cancel file operation"));gtk_label_set_text(GTK_LABEL(d->status),insight_tr("Changing comparison file…"));
   GTask *task=g_task_new(window,d->cancel,file_done,NULL);g_task_set_task_data(task,job,file_job_free);job=NULL;g_task_run_in_thread(task,file_worker);g_object_unref(task);
  }
 }
 if(job) file_job_free(job);
 g_free(source);g_free(destination);g_object_unref(window);
}
static const gchar *file_labels[]={"Copy left → right…","Copy right → left…","Trash left file…","Trash right file…"};
static GtkWidget *context_menu(Directory *d) {
 GtkWidget *menu=gtk_menu_new();gtk_menu_shell_append(GTK_MENU_SHELL(menu),insight_list_style_menu_item(d->view));const gchar *labels[]={"Compare selected file","Copy relative path","Copy visible list"};GCallback callbacks[]={G_CALLBACK(open_pair),G_CALLBACK(copy_path),G_CALLBACK(copy_list)};
 for(guint i=0;i<G_N_ELEMENTS(labels);i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",callbacks[i],d);gtk_widget_set_sensitive(item,i==0 ? !d->busy && paired(selected_entry(d)) : i==1 ? selected_entry(d)!=NULL : TRUE); }
 for(guint i=0;i<4;i++) { GtkWidget *item=gtk_menu_item_new_with_label(file_labels[i]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_object_set_data(G_OBJECT(item),"file-action",GUINT_TO_POINTER(i));g_signal_connect(item,"activate",G_CALLBACK(file_action),d);gtk_widget_set_sensitive(item,action_available(d,i)); }
 for(gint side=0;side<2;side++) { GtkWidget *item=gtk_menu_item_new_with_label(side ? "Copy right full path" : "Copy left full path");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_object_set_data(G_OBJECT(item),"side",GINT_TO_POINTER(side));g_signal_connect(item,"activate",G_CALLBACK(copy_absolute),d);InsightDirectoryEntry *entry=selected_entry(d);gtk_widget_set_sensitive(item,entry && entry->info[side]); }
 g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);gtk_menu_attach_to_widget(GTK_MENU(menu),d->view,NULL);gtk_widget_show_all(menu);return menu;
}
static gboolean popup(GtkWidget *widget,gpointer data) { (void)widget;gtk_menu_popup_at_widget(GTK_MENU(context_menu(data)),((Directory*)data)->view,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,NULL);return TRUE; }
static gboolean mouse(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 if(event->button!=3) return FALSE;
 GtkTreePath *path=NULL;
 if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)) { gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget),path,NULL,FALSE);gtk_tree_path_free(path); }
 gtk_menu_popup_at_pointer(GTK_MENU(context_menu(data)),(GdkEvent*)event);return TRUE;
}
static void column_toggled(GtkCheckMenuItem *item,gpointer column) { gtk_tree_view_column_set_visible(GTK_TREE_VIEW_COLUMN(column),gtk_check_menu_item_get_active(item)); }
static GtkWidget *columns_menu(Directory *d) {
 GtkWidget *menu=gtk_menu_new();GList *columns=gtk_tree_view_get_columns(GTK_TREE_VIEW(d->view));guint i=0;
 for(GList *p=columns;p;p=p->next,i++) {
  GtkTreeViewColumn *column=p->data;GtkWidget *item=gtk_check_menu_item_new_with_label(gtk_tree_view_column_get_title(column));
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),gtk_tree_view_column_get_visible(column));gtk_widget_set_sensitive(item,i!=0);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"toggled",G_CALLBACK(column_toggled),column);
 }
 g_list_free(columns);g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);gtk_menu_attach_to_widget(GTK_MENU(menu),d->view,NULL);gtk_widget_show_all(menu);return menu;
}
static void show_columns(GtkWidget *widget,gpointer data) { gtk_menu_popup_at_widget(GTK_MENU(columns_menu(data)),widget,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,NULL); }
static void method_changed(GtkComboBox *combo,gpointer data) {
 Directory *d=data;gboolean text=gtk_combo_box_get_active(combo)==INSIGHT_DIR_CONTENT;
 gtk_widget_set_sensitive(d->space,text);gtk_widget_set_sensitive(d->eol,text);
}
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 Directory *d=data;if(event->keyval==GDK_KEY_F5) { start(NULL,d);return TRUE; }
 if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_Down)) { advance(d,event->keyval==GDK_KEY_Up ? -1 : 1);return TRUE; }
 if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_f || event->keyval==GDK_KEY_F)) { insight_compare_ui_reveal(d->filter_entry);gtk_widget_grab_focus(d->filter_entry);return TRUE; }
 if(widget==d->view && (event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter)) { open_pair(NULL,d);return TRUE; }return FALSE;
}
static GtkWidget *button(GtkWidget *box,const gchar *label,GCallback callback,Directory *d) {
 GtkWidget *widget=gtk_button_new_with_label(label);gtk_box_pack_start(GTK_BOX(box),widget,FALSE,FALSE,0);g_signal_connect(widget,"clicked",callback,d);return widget;
}
static GtkWidget *check(GtkWidget *box,const gchar *label,gboolean active) {
 GtkWidget *widget=gtk_check_button_new_with_label(label);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),active);gtk_box_pack_start(GTK_BOX(box),widget,FALSE,FALSE,0);return widget;
}
static Directory *directory_new(GtkWidget *parent) {
 Directory *d=g_new0(Directory,1);d->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(d->window,"insight-directory-window");gtk_window_set_title(GTK_WINDOW(d->window),"Directory Compare");gtk_window_set_default_size(GTK_WINDOW(d->window),INSIGHT_COMPARE_WIDTH,INSIGHT_COMPARE_HEIGHT);
 if(parent) { gtk_window_set_transient_for(GTK_WINDOW(d->window),GTK_WINDOW(parent));gtk_window_set_destroy_with_parent(GTK_WINDOW(d->window),TRUE); }
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(d->window),box);
 d->controls=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_box_pack_start(GTK_BOX(box),d->controls,FALSE,FALSE,0);
 for(gint side=0;side<2;side++) {
  GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(d->controls),row,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),gtk_label_new(side ? "Right directory" : "Left directory"),FALSE,FALSE,0);
  d->paths[side]=gtk_entry_new();gtk_entry_set_max_length(GTK_ENTRY(d->paths[side]),16384);gtk_widget_set_name(d->paths[side],side ? "insight-directory-right" : "insight-directory-left");gtk_box_pack_start(GTK_BOX(row),d->paths[side],TRUE,TRUE,0);GtkWidget *browse=button(row,"Browse…",G_CALLBACK(choose),d);g_object_set_data(G_OBJECT(browse),"side",GINT_TO_POINTER(side));g_signal_connect(d->paths[side],"activate",G_CALLBACK(start),d);
 }
 GtkWidget *options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(d->controls),options,FALSE,FALSE,0);
 d->method=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->method),"Compare contents");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->method),"Compare modification dates");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->method),"Compare sizes and dates");gtk_combo_box_set_active(GTK_COMBO_BOX(d->method),0);gtk_box_pack_start(GTK_BOX(options),d->method,FALSE,FALSE,0);
 button(options,"Swap and scan",G_CALLBACK(swap_and_scan),d);
 d->recursive=check(options,"Include subdirectories",TRUE);d->hidden=check(options,"Include hidden entries",FALSE);
 d->known=check(options,"Show only known file types",FALSE);gtk_widget_set_name(d->known,"insight-directory-known");gtk_widget_set_sensitive(d->known,types_provider!=NULL);
 GtkWidget *text_options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(d->controls),text_options,FALSE,FALSE,0);d->space=check(text_options,"Ignore spaces and tabs",FALSE);d->eol=check(text_options,"Ignore line ending style",FALSE);
 d->confirm=check(text_options,"Confirm file operations",TRUE);gtk_widget_set_name(d->confirm,"insight-directory-confirm");gtk_widget_set_tooltip_text(d->confirm,"Confirm before copying or moving files to Trash.");
 GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
 button(bar,"Copy visible list",G_CALLBACK(copy_list),d);
 d->scan=button(bar,"Scan / Refresh",G_CALLBACK(start),d);gtk_widget_set_name(d->scan,"insight-directory-scan");d->cancel_button=button(bar,"Cancel scan",G_CALLBACK(cancel_scan),d);d->open=button(bar,"Compare selected file",G_CALLBACK(open_pair),d);gtk_widget_set_name(d->open,"insight-directory-open");d->previous=button(bar,"Previous different file",G_CALLBACK(step),d);d->next=button(bar,"Next different file",G_CALLBACK(step),d);
 GtkWidget *operations=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),operations,FALSE,FALSE,0);
 for(guint i=0;i<4;i++) { d->actions[i]=button(operations,file_labels[i],G_CALLBACK(file_action),d);g_object_set_data(G_OBJECT(d->actions[i]),"file-action",GUINT_TO_POINTER(i));gchar *name=g_strdup_printf("insight-directory-operation-%u",i);gtk_widget_set_name(d->actions[i],name);g_free(name); }
 GtkWidget *filters=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),filters,FALSE,FALSE,0);d->filter_entry=gtk_search_entry_new();gtk_entry_set_max_length(GTK_ENTRY(d->filter_entry),2048);gtk_entry_set_placeholder_text(GTK_ENTRY(d->filter_entry),"Filter relative paths — Ctrl+F");gtk_box_pack_start(GTK_BOX(filters),d->filter_entry,TRUE,TRUE,0);d->different=check(filters,"Differences only",FALSE);button(filters,"Columns…",G_CALLBACK(show_columns),d);
 GtkWidget *masks=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),masks,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(masks),gtk_label_new("Include masks"),FALSE,FALSE,0);d->include=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(d->include),"*.c;*.h — empty includes all");gtk_entry_set_max_length(GTK_ENTRY(d->include),2048);gtk_box_pack_start(GTK_BOX(masks),d->include,TRUE,TRUE,0);
 gtk_box_pack_start(GTK_BOX(masks),gtk_label_new("Exclude masks"),FALSE,FALSE,0);d->exclude=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(d->exclude),"*.o;build/*");gtk_entry_set_max_length(GTK_ENTRY(d->exclude),2048);gtk_box_pack_start(GTK_BOX(masks),d->exclude,TRUE,TRUE,0);
 d->status_filter=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->status_filter),"All statuses");for(gint i=0;i<8;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->status_filter),status_name(i));gtk_combo_box_set_active(GTK_COMBO_BOX(d->status_filter),0);gtk_box_pack_start(GTK_BOX(masks),d->status_filter,FALSE,FALSE,0);
 button(masks,"Reset filters",G_CALLBACK(reset_filters),d);
 gtk_widget_set_tooltip_text(masks,"Case-sensitive glob masks separated by semicolons; * and ? match relative paths or filenames. Exclusions win. Filters do not reduce scan limits.");
 d->store=gtk_list_store_new(DC_COLUMNS,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_BOOLEAN);
 d->filtered=gtk_tree_model_filter_new(GTK_TREE_MODEL(d->store),NULL);gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(d->filtered),visible,d,NULL);GtkTreeModel *sorted=gtk_tree_model_sort_new_with_model(d->filtered);d->view=gtk_tree_view_new_with_model(sorted);gtk_widget_set_name(d->view,"insight-directory-files");g_object_unref(sorted);gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(d->view),DC_DETAIL);
 const gchar *labels[]={"Relative path","Status","Left — size / modified","Right — size / modified"};
 for(gint i=0;i<4;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(labels[i],gtk_cell_renderer_text_new(),"text",DC_PATH+i,NULL);gtk_tree_view_column_set_resizable(column,TRUE);if(i<2) gtk_tree_view_column_set_sort_column_id(column,DC_PATH+i);gtk_tree_view_append_column(GTK_TREE_VIEW(d->view),column); }
 GtkWidget *appearance=insight_list_style_button(d->view);gtk_widget_set_name(appearance,"insight-directory-appearance");gtk_box_pack_start(GTK_BOX(filters),appearance,FALSE,FALSE,0);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),d->view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 d->status=gtk_label_new("Choose two directories and scan. Results are a snapshot.");gtk_label_set_xalign(GTK_LABEL(d->status),0);gtk_label_set_line_wrap(GTK_LABEL(d->status),TRUE);gtk_widget_set_margin_end(d->status,24);gtk_box_pack_start(GTK_BOX(box),insight_panel_help_overlay(d->status,"directory-compare.md"),FALSE,FALSE,0);
 GtkWidget *tabs=insight_compare_ui_tabs(box,"insight-directory-toolbar-tabs");
 GtkWidget *files=insight_compare_ui_page(tabs,insight_tr("Files and comparison")),*compare_options=insight_compare_ui_page(tabs,insight_tr("Comparison options")),*view_options=insight_compare_ui_page(tabs,insight_tr("View")),*file_operations=insight_compare_ui_page(tabs,insight_tr("File operations")),*navigation=insight_compare_ui_page(tabs,insight_tr("Navigation"));
 insight_compare_ui_move(options,compare_options);insight_compare_ui_move(text_options,compare_options);insight_compare_ui_move(d->controls,files);insight_compare_ui_move(bar,files);
 insight_compare_ui_move(filters,view_options);insight_compare_ui_move(masks,view_options);insight_compare_ui_move(operations,file_operations);
 GtkWidget *navigation_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_box_pack_start(GTK_BOX(navigation),navigation_row,FALSE,FALSE,0);
 insight_compare_ui_move(d->previous,navigation_row);insight_compare_ui_move(d->next,navigation_row);
 g_signal_connect(d->include,"changed",G_CALLBACK(masks_changed),d);g_signal_connect(d->exclude,"changed",G_CALLBACK(masks_changed),d);g_signal_connect(d->status_filter,"changed",G_CALLBACK(refilter),d);
 g_signal_connect(d->filter_entry,"changed",G_CALLBACK(refilter),d);g_signal_connect(d->different,"toggled",G_CALLBACK(refilter),d);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(d->view)),"changed",G_CALLBACK(selection_changed),d);
 g_signal_connect(d->method,"changed",G_CALLBACK(method_changed),d);method_changed(GTK_COMBO_BOX(d->method),d);
 g_signal_connect(d->view,"popup-menu",G_CALLBACK(popup),d);g_signal_connect(d->view,"button-press-event",G_CALLBACK(mouse),d);
 g_signal_connect(d->view,"row-activated",G_CALLBACK(activated),d);g_signal_connect(d->view,"key-press-event",G_CALLBACK(key),d);g_signal_connect(d->window,"key-press-event",G_CALLBACK(key),d);g_signal_connect(d->window,"destroy",G_CALLBACK(destroyed),d);g_object_set_data(G_OBJECT(d->window),"insight-directory-state",d);busy(d,FALSE);options_read(d,saved_options);open_windows=g_list_prepend(open_windows,d);gtk_widget_show_all(d->window);return d;
}
void insight_directory_show(GtkWidget *parent) { (void)directory_new(parent); }
