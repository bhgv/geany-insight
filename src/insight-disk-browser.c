#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "keybindings.h"
#include "document.h"
#include "../insight/desktop/disk_operation.h"
#include "../insight/desktop/folder_layout.h"
#include "insight-disk-browser.h"
#include "insight-list-style.h"
#include "../insight/desktop/lookup_filter.h"
#include <gio/gio.h>
#include <string.h>
#define DISK_LIMIT 2048u
enum { NAME,PATH,DIRECTORY,LINK,SIZE,MODIFIED,FILE_ID,ETAG,COLUMNS };
enum { DB_PREVIEW,DB_OPEN,DB_PATH,DB_NAME,DB_UP,DB_REFRESH,DB_CREATE,DB_STYLE,DB_OPTIONS,DB_RENAME,DB_MOVE,DB_TRASH,DB_ACTIONS };
enum { FD_OPEN,FD_PATH,FD_NAME,FD_UP,FD_REFRESH,FD_STYLE,FD_OPTIONS,FD_ACTIONS };
typedef struct { gchar *name,*path,*file_id,*etag;gboolean directory,link;gint64 size,modified; } DiskEntry;
typedef struct {
 GtkWidget *split,*folders,*folder_popup,*folder_actions[FD_ACTIONS];GtkTreeStore *folder_store;gboolean adaptive,vertical,split_setting,folder_hidden;gint split_h,split_v;guint folder_reveal;
 GtkWidget *box,*path,*query,*view,*status,*hidden,*follow,*kind_toggle,*size_toggle,*date_toggle,*create,*popup,*actions[DB_ACTIONS];GtkTreeViewColumn *name_column,*kind_column,*size_column,*date_column;GtkListStore *store;GtkTreeModelFilter *model;
 InsightLookupFilter *filter;GCancellable *cancel,*mutation;guint64 generation;gboolean alive,setting;
 gchar *root,*failure,*valid_query,*current_file;guint count;gboolean loading,limited,cancelled,active,creating,mutation_error,operating,operation_error;InsightDiskOpen open;gpointer data;
} DiskBrowser;
typedef struct { GWeakRef box;guint64 generation;gchar *root,*current_name;gboolean hidden,limited; } DiskJob;
static void entry_free(gpointer data) { DiskEntry *e=data;g_free(e->name);g_free(e->path);g_free(e->file_id);g_free(e->etag);g_free(e); }
static void job_free(gpointer data) { DiskJob *j=data;g_weak_ref_clear(&j->box);g_free(j->root);g_free(j->current_name);g_free(j); }
static gboolean same_path(const gchar *a,const gchar *b) { if(!a || !b) return FALSE;GFile *x=g_file_new_for_path(a),*y=g_file_new_for_path(b);gboolean same=g_file_equal(x,y);g_object_unref(x);g_object_unref(y);return same; }
static gboolean following(DiskBrowser *b) { return b->active && b->current_file && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->follow)); }
static gboolean select_current(DiskBrowser *b) {
 if(!following(b)) return FALSE;
 GtkTreeModel *model=GTK_TREE_MODEL(b->model);GtkTreeIter iter;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) {
  gchar *path=NULL;gtk_tree_model_get(model,&iter,PATH,&path,-1);gboolean match=same_path(path,b->current_file);g_free(path);if(!match) continue;
  GtkTreePath *row=gtk_tree_model_get_path(model,&iter);gboolean setting=b->setting;b->setting=TRUE;gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(b->view)),&iter);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(b->view),row,NULL,FALSE,0,0);b->setting=setting;gtk_tree_path_free(row);return TRUE;
 }
 return FALSE;
}
static gint compare(gconstpointer a,gconstpointer b) { const DiskEntry *x=*(DiskEntry *const *)a,*y=*(DiskEntry *const *)b;if(x->directory!=y->directory) return x->directory ? -1 : 1;return g_utf8_collate(x->name,y->name); }
static void read_directory(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;DiskJob *j=data;GError *error=NULL;GFile *folder=g_file_new_for_path(j->root);
 GFileEnumerator *reader=g_file_enumerate_children(folder,"standard::name,standard::type,standard::is-symlink,standard::is-hidden,standard::size,time::modified," INSIGHT_DISK_IDENTITY_ATTRIBUTES,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,&error);g_object_unref(folder);
 GPtrArray *entries=g_ptr_array_new_with_free_func(entry_free);guint visited=0;gboolean current_found=FALSE;
 if(reader) {
  while(!g_cancellable_is_cancelled(cancel)) {
   GFileInfo *info=g_file_enumerator_next_file(reader,cancel,&error);if(!info) break;
   if(++visited>100000u) { j->limited=TRUE;g_object_unref(info);break; }
   const gchar *name=g_file_info_get_name(info);GFileType type=g_file_info_get_file_type(info);
   gboolean current=name && j->current_name && !strcmp(name,j->current_name);
   if(name && g_utf8_validate(name,-1,NULL) && (current || j->hidden || (!g_file_info_get_is_hidden(info) && name[0]!='.')) &&
      (type==G_FILE_TYPE_DIRECTORY || type==G_FILE_TYPE_REGULAR || type==G_FILE_TYPE_SYMBOLIC_LINK)) {
    if(entries->len==DISK_LIMIT) { j->limited=TRUE;if(current) g_ptr_array_remove_index(entries,entries->len-1);else { g_object_unref(info);if(!j->current_name || current_found) break;continue; } }
    DiskEntry *e=g_new0(DiskEntry,1);e->name=g_strdup(name);e->file_id=g_strdup(g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE));e->etag=g_strdup(g_file_info_get_etag(info));e->path=g_build_filename(j->root,name,NULL);e->link=g_file_info_get_is_symlink(info);e->directory=type==G_FILE_TYPE_DIRECTORY && !e->link;e->size=type==G_FILE_TYPE_REGULAR && !e->link && g_file_info_has_attribute(info,G_FILE_ATTRIBUTE_STANDARD_SIZE) ? g_file_info_get_size(info) : -1;
    guint64 modified=g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_TIME_MODIFIED);e->modified=g_file_info_has_attribute(info,G_FILE_ATTRIBUTE_TIME_MODIFIED) && modified<=G_MAXINT64 ? (gint64)modified : -1;g_ptr_array_add(entries,e);current_found|=current;
   }
   g_object_unref(info);
  }
  g_object_unref(reader);
 }
 if(g_cancellable_is_cancelled(cancel) && !error) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Operation cancelled");
 if(error) { g_ptr_array_unref(entries);g_task_return_error(task,error);return; }
 g_ptr_array_sort(entries,compare);g_task_return_pointer(task,entries,(GDestroyNotify)g_ptr_array_unref);
}
static gboolean visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 DiskBrowser *b=data;gboolean directory=FALSE;gchar *path=NULL;gtk_tree_model_get(model,iter,PATH,&path,DIRECTORY,&directory,-1);
 gboolean match=directory || (following(b) && same_path(path,b->current_file)) || insight_lookup_filter_match(b->filter,NULL,NULL,path,NULL);g_free(path);return match;
}
static void status(DiskBrowser *b) {
 gchar *label=b->operating ? g_strdup(insight_tr("Changing disk entry…")) : (b->creating ? g_strdup(insight_tr("Creating folder…")) : (b->cancelled ? g_strdup(insight_tr("Directory read cancelled")) : (b->loading ? g_strdup(insight_tr("Reading directory…")) : (b->failure ? insight_i18n_printf(b->operation_error ? "Cannot change disk entry: %s" : (b->mutation_error ? "Cannot create folder: %s" : "Cannot browse directory: %s"),b->failure) : insight_i18n_printf("Disk entries: %u%s",b->count,b->limited ? insight_tr(" (directory limit reached)") : "")))));gtk_label_set_text(GTK_LABEL(b->status),label);g_free(label);
}
void insight_disk_browser_cancel(GtkWidget *box) {
 DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(!b || !b->alive || !b->cancel) return;
 b->generation++;g_cancellable_cancel(b->cancel);g_clear_object(&b->cancel);b->loading=FALSE;b->cancelled=TRUE;status(b);
}
void insight_disk_browser_locale_changed(GtkWidget *box) { DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(!b || !b->alive) return;if(b->root) status(b);if(b->kind_toggle) insight_i18n_apply(gtk_widget_get_parent(b->kind_toggle));if(b->popup) insight_i18n_apply(b->popup);if(b->folder_popup) insight_i18n_apply(b->folder_popup);gtk_widget_queue_draw(b->view);gtk_widget_queue_draw(b->folders); }
static void load(DiskBrowser *b,const gchar *path);
static gboolean folder_reveal_current(gpointer data) {
 GtkWidget *box=data;DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(!b) return G_SOURCE_REMOVE;b->folder_reveal=0;if(!b->alive) return G_SOURCE_REMOVE;
 GtkTreeModel *model;GtkTreeIter iter;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(b->folders)),&model,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(b->folders),path,NULL,TRUE,0.5f,0);gtk_tree_path_free(path); }return G_SOURCE_REMOVE;
}
static void folder_request_reveal(DiskBrowser *b) {
 if(b->alive && !b->folder_reveal) b->folder_reveal=g_timeout_add_full(G_PRIORITY_LOW,25,folder_reveal_current,g_object_ref(b->box),g_object_unref);
}
static void folder_allocated(GtkWidget *widget,GtkAllocation *allocation,gpointer data) { (void)widget;(void)allocation;folder_request_reveal(data); }
static gboolean folder_count_node(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 (void)model;(void)path;(void)iter;guint *count=data;return ++*count>4096;
}
static gboolean folder_find_child(DiskBrowser *b,GtkTreeIter *parent,const gchar *path,GtkTreeIter *found) {
 GtkTreeModel *model=GTK_TREE_MODEL(b->folder_store);GtkTreeIter iter;
 for(gboolean valid=gtk_tree_model_iter_children(model,&iter,parent);valid;valid=gtk_tree_model_iter_next(model,&iter)) {
  gchar *value=NULL;gtk_tree_model_get(model,&iter,1,&value,-1);gboolean match=same_path(path,value);g_free(value);if(match) { *found=iter;return TRUE; }
 }
 return FALSE;
}
static void folders_rebuild(DiskBrowser *b,GPtrArray *entries,gboolean replace) {
 gboolean setting=b->setting;b->setting=TRUE;GPtrArray *ancestors=insight_folder_ancestry(b->root,64);
 GtkTreeModel *model=GTK_TREE_MODEL(b->folder_store);guint count=0;gtk_tree_model_foreach(model,folder_count_node,&count);
 gboolean hidden=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->hidden));
 if(!ancestors || hidden!=b->folder_hidden || count+64+(entries ? entries->len : 0)>4096) gtk_tree_store_clear(b->folder_store);
 b->folder_hidden=hidden;
 GtkTreeIter current;gboolean have=FALSE;
 if(ancestors) {
  GtkTreeIter first;if(gtk_tree_model_get_iter_first(model,&first)) { gchar *path=NULL;gtk_tree_model_get(model,&first,1,&path,-1);if(!same_path(path,g_ptr_array_index(ancestors,0))) gtk_tree_store_clear(b->folder_store);g_free(path); }
  for(guint i=0;i<ancestors->len;i++) {
   const gchar *path=g_ptr_array_index(ancestors,i);GtkTreeIter iter;
   if(!folder_find_child(b,have ? &current : NULL,path,&iter)) { gtk_tree_store_append(b->folder_store,&iter,have ? &current : NULL);gchar *name=i==0 ? g_strdup(path) : g_path_get_basename(path);gtk_tree_store_set(b->folder_store,&iter,0,name,1,path,-1);g_free(name); }
   current=iter;have=TRUE;
  }
  g_ptr_array_unref(ancestors);
 }
 if(have && replace) { GtkTreeIter child;while(gtk_tree_model_iter_children(model,&child,&current)) gtk_tree_store_remove(b->folder_store,&child); }
 if(have && entries) for(guint i=0;i<entries->len;i++) {
  DiskEntry *entry=g_ptr_array_index(entries,i);if(!entry->directory || entry->link) continue;
  GtkTreeIter iter;gtk_tree_store_append(b->folder_store,&iter,&current);gtk_tree_store_set(b->folder_store,&iter,0,entry->name,1,entry->path,-1);
 }
 if(have) {
  GtkTreePath *path=gtk_tree_model_get_path(model,&current);gtk_tree_view_expand_to_path(GTK_TREE_VIEW(b->folders),path);gtk_tree_view_expand_row(GTK_TREE_VIEW(b->folders),path,FALSE);
  gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(b->folders)),&current);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(b->folders),path,NULL,TRUE,0.5f,0);gtk_tree_path_free(path);
 }
 b->setting=setting;folder_request_reveal(b);
}
static void folder_selected(GtkTreeSelection *selection,gpointer data) {
 DiskBrowser *b=data;if(!b->alive || b->setting) return;GtkTreeModel *model;GtkTreeIter iter;
 if(gtk_tree_selection_get_selected(selection,&model,&iter)) {
  gchar *path=NULL;gtk_tree_model_get(model,&iter,1,&path,-1);if(path && !same_path(path,b->root)) load(b,path);g_free(path);
 }
}
static void folder_activated(GtkTreeView *view,GtkTreePath *row,GtkTreeViewColumn *column,gpointer data) {
 (void)column;DiskBrowser *b=data;GtkTreeIter iter;GtkTreeModel *model=gtk_tree_view_get_model(view);if(!b->alive || !gtk_tree_model_get_iter(model,&iter,row)) return;
 gchar *path=NULL;gtk_tree_model_get(model,&iter,1,&path,-1);if(path && !same_path(path,b->root)) load(b,path);g_free(path);
}
static void split_arrange(DiskBrowser *b,gint width,gint height) {
 gboolean current=gtk_orientable_get_orientation(GTK_ORIENTABLE(b->split))==GTK_ORIENTATION_VERTICAL;
 gint shape_width=gtk_widget_get_allocated_width(b->box),shape_height=gtk_widget_get_allocated_height(b->box);
 gboolean vertical=insight_folder_layout_vertical(b->adaptive,b->vertical,shape_width>1 ? shape_width : width,shape_height>1 ? shape_height : height,current);
 gint extent=vertical ? height : width;if(extent<120) return;
 b->split_setting=TRUE;
 if(current!=vertical) gtk_orientable_set_orientation(GTK_ORIENTABLE(b->split),vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
 gint desired=vertical ? b->split_v : b->split_h;
 gtk_paned_set_position(GTK_PANED(b->split),CLAMP(desired,40,MAX(40,extent-80)));b->split_setting=FALSE;
}
static void split_allocated(GtkWidget *widget,GtkAllocation *allocation,gpointer data) {
 (void)widget;DiskBrowser *b=data;if(!b->alive) return;split_arrange(b,allocation->width,allocation->height);folder_request_reveal(b);
}
static void split_position(GObject *object,GParamSpec *spec,gpointer data) {
 (void)spec;DiskBrowser *b=data;if(!b->alive || b->split_setting) return;
 gint position=gtk_paned_get_position(GTK_PANED(object));if(position<40 || position>2000) return;
 if(gtk_orientable_get_orientation(GTK_ORIENTABLE(object))==GTK_ORIENTATION_VERTICAL) b->split_v=position;else b->split_h=position;
}
static void layout_toggle(GtkToggleButton *toggle,gpointer data) { gtk_widget_set_sensitive(GTK_WIDGET(data),!gtk_toggle_button_get_active(toggle)); }

static void read_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source;(void)data;GTask *task=G_TASK(result);DiskJob *j=g_task_get_task_data(task);GError *error=NULL;GPtrArray *entries=g_task_propagate_pointer(task,&error);
 GtkWidget *box=g_weak_ref_get(&j->box);DiskBrowser *b=box ? g_object_get_data(G_OBJECT(box),"insight-disk-browser") : NULL;
 if(b && b->alive && b->generation==j->generation) {
  g_clear_object(&b->cancel);b->setting=TRUE;gtk_list_store_clear(b->store);
  if(entries) for(guint i=0;i<entries->len;i++) { DiskEntry *e=g_ptr_array_index(entries,i);GtkTreeIter iter;gtk_list_store_append(b->store,&iter);gtk_list_store_set(b->store,&iter,NAME,e->name,PATH,e->path,DIRECTORY,e->directory,LINK,e->link,SIZE,e->size,MODIFIED,e->modified,FILE_ID,e->file_id,ETAG,e->etag,-1); }
  b->setting=FALSE;folders_rebuild(b,entries,TRUE);
  b->loading=FALSE;b->cancelled=FALSE;b->mutation_error=FALSE;b->operation_error=FALSE;b->count=entries ? entries->len : 0;b->limited=j->limited;g_free(b->failure);b->failure=error ? g_strdup(error->message) : NULL;status(b);
  select_current(b);
 }
 if(box) g_object_unref(box);
 if(entries) g_ptr_array_unref(entries);
 g_clear_error(&error);
}
static void load(DiskBrowser *b,const gchar *path) {
 if(!b->alive || !path || !*path || !g_utf8_validate(path,-1,NULL) || strlen(path)>16384) return;
 gchar *root=g_canonicalize_filename(path,b->root);b->generation++;if(b->cancel) g_cancellable_cancel(b->cancel);g_clear_object(&b->cancel);b->cancel=g_cancellable_new();
 g_free(b->root);b->root=root;folders_rebuild(b,NULL,FALSE);gtk_entry_set_text(GTK_ENTRY(b->path),root);gtk_widget_set_sensitive(b->create,!b->creating && !b->operating);b->setting=TRUE;gtk_list_store_clear(b->store);b->setting=FALSE;b->loading=TRUE;b->cancelled=FALSE;b->mutation_error=FALSE;b->operation_error=FALSE;g_clear_pointer(&b->failure,g_free);status(b);
 DiskJob *j=g_new0(DiskJob,1);g_weak_ref_init(&j->box,b->box);j->generation=b->generation;j->root=g_strdup(root);j->hidden=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->hidden));
 if(following(b)) { gchar *parent=g_path_get_dirname(b->current_file);if(same_path(parent,root)) j->current_name=g_path_get_basename(b->current_file);g_free(parent); }
 GTask *task=g_task_new(NULL,b->cancel,read_done,NULL);g_task_set_task_data(task,j,job_free);g_task_run_in_thread(task,read_directory);g_object_unref(task);
}
void insight_disk_browser_set_root(GtkWidget *box,const gchar *path) { DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(b) load(b,path); }
void insight_disk_browser_resume(GtkWidget *box,const gchar *root) { DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(b) load(b,b->root ? b->root : root); }
void insight_disk_browser_current_file(GtkWidget *box,const gchar *path,gboolean active) {
 DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(!b || !b->alive) return;
 gchar *file=path && *path && g_path_is_absolute(path) && strlen(path)<=16384 && g_utf8_validate(path,-1,NULL) ? g_canonicalize_filename(path,NULL) : NULL;gboolean changed=!same_path(file,b->current_file);
 g_free(b->current_file);b->current_file=file;b->active=active;gboolean setting=b->setting;b->setting=TRUE;gtk_tree_model_filter_refilter(b->model);b->setting=setting;
 if(!following(b) || b->setting) return;
 gchar *parent=g_path_get_dirname(b->current_file);
 if(!same_path(parent,b->root) || (!select_current(b) && (!b->loading || changed))) load(b,parent);
 g_free(parent);
}
static void follow_changed(GtkToggleButton *button,gpointer data) {
 (void)button;DiskBrowser *b=data;if(b->setting) return;
 if(following(b)) { if(b->loading) insight_disk_browser_cancel(b->box);gchar *path=g_strdup(b->current_file);insight_disk_browser_current_file(b->box,path,b->active);g_free(path); }
 else { gtk_tree_model_filter_refilter(b->model);if(b->active && b->root) { gchar *root=g_strdup(b->root);load(b,root);g_free(root); } }
}
static void options(GtkWidget *widget,gpointer data) {
 (void)widget;DiskBrowser *b=data;if(!b->alive) return;g_object_ref(b->box);GtkWidget *parent=gtk_widget_get_toplevel(b->box);
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Folder Browser Options",GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL);gtk_widget_set_name(dialog,"insight-disk-options-dialog");GtkWidget *content=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_container_set_border_width(GTK_CONTAINER(content),8);
 GtkWidget *columns=gtk_label_new("Show Columns");gtk_widget_set_halign(columns,GTK_ALIGN_START);gtk_box_pack_start(GTK_BOX(content),columns,FALSE,FALSE,3);
 const gchar *labels[]={"Kind","File Size","File Date","Follow current file","Show hidden disk entries","Layout based on window shape","Stack lists vertically"};const gchar *names[]={"kind","size","date","follow","hidden","adaptive","vertical"};GtkWidget *checks[7];GtkTreeViewColumn *views[]={b->kind_column,b->size_column,b->date_column};
 for(guint i=0;i<7;i++) { checks[i]=gtk_check_button_new_with_label(labels[i]);gchar *name=g_strconcat("insight-disk-options-",names[i],NULL);gtk_widget_set_name(checks[i],name);g_free(name);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checks[i]),i<3 ? gtk_tree_view_column_get_visible(views[i]) : (i<5 ? gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(i==3 ? b->follow : b->hidden)) : (i==5 ? b->adaptive : b->vertical)));gtk_box_pack_start(GTK_BOX(content),checks[i],FALSE,FALSE,3); }
 g_signal_connect(checks[5],"toggled",G_CALLBACK(layout_toggle),checks[6]);layout_toggle(GTK_TOGGLE_BUTTON(checks[5]),checks[6]);
 GtkWidget *folder_style=insight_list_style_button(b->folders);gtk_button_set_label(GTK_BUTTON(folder_style),"Folder Font & Color…");gtk_widget_set_name(folder_style,"insight-disk-options-folder-appearance");gtk_box_pack_start(GTK_BOX(content),folder_style,FALSE,FALSE,3);
 GtkWidget *appearance=insight_list_style_button(b->view);gtk_widget_set_name(appearance,"insight-disk-options-appearance");gtk_box_pack_start(GTK_BOX(content),appearance,FALSE,FALSE,3);insight_i18n_apply(dialog);gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_APPLY && b->alive) {
  gboolean follow=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[3])),hidden=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[4]));gboolean reload=follow!=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->follow)) || hidden!=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->hidden));b->setting=TRUE;
  GtkWidget *toggles[]={b->kind_toggle,b->size_toggle,b->date_toggle};for(guint i=0;i<3;i++) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(toggles[i]),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[i])));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b->follow),follow);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b->hidden),hidden);b->setting=FALSE;
  b->adaptive=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[5]));b->vertical=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[6]));split_arrange(b,gtk_widget_get_allocated_width(b->split),gtk_widget_get_allocated_height(b->split));
  if(reload) follow_changed(NULL,b);
 }
 gtk_widget_destroy(dialog);g_object_unref(b->box);
}
typedef struct { GWeakRef box;gchar *root;guint64 generation; } CreateJob;
static void create_done(GObject *source,GAsyncResult *result,gpointer data) {
 CreateJob *job=data;GError *error=NULL;gboolean created=g_file_make_directory_finish(G_FILE(source),result,&error);GtkWidget *box=g_weak_ref_get(&job->box);DiskBrowser *b=box ? g_object_get_data(G_OBJECT(box),"insight-disk-browser") : NULL;
 if(b && b->alive) {
  b->creating=FALSE;g_clear_object(&b->mutation);gtk_widget_set_sensitive(b->create,b->root!=NULL);
  if(created && b->active && same_path(b->root,job->root)) load(b,job->root);
  else if(!created && b->generation==job->generation) { g_free(b->failure);b->failure=g_strdup(error ? error->message : "Cannot create folder");b->mutation_error=TRUE;b->cancelled=FALSE;status(b); }
  else status(b);
 }
 if(box) g_object_unref(box);
 g_clear_error(&error);g_weak_ref_clear(&job->box);g_free(job->root);g_free(job);
}
static void create_folder(GtkButton *button,gpointer data) {
 (void)button;DiskBrowser *b=data;if(!b->alive || !b->root || b->creating || b->operating) return;g_object_ref(b->box);gchar *root=g_strdup(b->root);guint64 generation=b->generation;GtkWidget *parent=gtk_widget_get_toplevel(b->box);
 GtkWidget *dialog=gtk_dialog_new_with_buttons("New folder…",GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Create",GTK_RESPONSE_ACCEPT,NULL);gtk_widget_set_name(dialog,"insight-disk-create-dialog");GtkWidget *content=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*directory=gtk_label_new(root),*entry=gtk_entry_new(),*failure=gtk_label_new("");gtk_widget_set_name(entry,"insight-disk-create-name");gtk_widget_set_name(failure,"insight-disk-create-error");gtk_entry_set_max_length(GTK_ENTRY(entry),255);gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE);gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);gtk_entry_set_placeholder_text(GTK_ENTRY(entry),"Folder name");gtk_label_set_ellipsize(GTK_LABEL(directory),PANGO_ELLIPSIZE_MIDDLE);gtk_label_set_max_width_chars(GTK_LABEL(directory),60);gtk_widget_set_tooltip_text(directory,root);gtk_label_set_line_wrap(GTK_LABEL(failure),TRUE);gtk_box_pack_start(GTK_BOX(content),directory,FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(content),entry,FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(content),failure,FALSE,FALSE,4);insight_i18n_apply(dialog);gtk_widget_show_all(dialog);gtk_widget_hide(failure);gtk_widget_grab_focus(entry);
 while(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && b->alive) {
  const gchar *name=gtk_entry_get_text(GTK_ENTRY(entry));
  if(!*name || !strcmp(name,".") || !strcmp(name,"..") || strlen(name)>255 || strpbrk(name,"/\\\r\n") || !g_utf8_validate(name,-1,NULL) || strlen(root)+strlen(name)+2>16384) { gtk_label_set_text(GTK_LABEL(failure),insight_tr("Enter one folder name, without path separators"));gtk_widget_show(failure);continue; }
  if(b->generation!=generation || !same_path(root,b->root)) { gtk_label_set_text(GTK_LABEL(failure),insight_tr("Directory changed; reopen New folder"));gtk_widget_show(failure);gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT,FALSE);continue; }
  insight_disk_browser_cancel(b->box);b->generation++;b->creating=TRUE;b->cancelled=FALSE;b->mutation_error=FALSE;b->operation_error=FALSE;g_clear_pointer(&b->failure,g_free);gtk_widget_set_sensitive(b->create,FALSE);status(b);b->mutation=g_cancellable_new();CreateJob *job=g_new0(CreateJob,1);g_weak_ref_init(&job->box,b->box);job->root=g_strdup(root);job->generation=b->generation;gchar *path=g_build_filename(root,name,NULL);GFile *file=g_file_new_for_path(path);g_file_make_directory_async(file,G_PRIORITY_DEFAULT,b->mutation,create_done,job);g_object_unref(file);g_free(path);break;
 }
 gtk_widget_destroy(dialog);g_free(root);g_object_unref(b->box);
}
static void refresh(GtkWidget *widget,gpointer data) { (void)widget;DiskBrowser *b=data;if(b->setting) return;gchar *path=g_strdup(gtk_entry_get_text(GTK_ENTRY(b->path)));load(b,path);g_free(path); }
static void parent(GtkWidget *widget,gpointer data) { (void)widget;DiskBrowser *b=data;if(b->root) { gchar *path=g_path_get_dirname(b->root);load(b,path);g_free(path); } }
static gchar *selected_path(DiskBrowser *b,gboolean *directory) {
 GtkTreeModel *model;GtkTreeIter iter;gchar *path=NULL;*directory=FALSE;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(b->view)),&model,&iter)) gtk_tree_model_get(model,&iter,PATH,&path,DIRECTORY,directory,-1);
 return path;
}
typedef struct { GWeakRef box;gchar *root,*source,*destination,*file_id,*etag;guint64 generation;gboolean trash; } ChangeJob;
static void change_job_free(gpointer data) {
 ChangeJob *j=data;g_weak_ref_clear(&j->box);g_free(j->root);g_free(j->source);g_free(j->destination);g_free(j->file_id);g_free(j->etag);g_free(j);
}
static void change_worker(GTask *task,gpointer object,gpointer data,GCancellable *cancel) {
 (void)object;ChangeJob *j=data;GError *error=NULL;
 if(insight_disk_operation(j->source,j->destination,j->file_id,j->etag,j->trash,cancel,&error)) g_task_return_boolean(task,TRUE);
 else g_task_return_error(task,error);
}
static void change_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source;(void)data;GTask *task=G_TASK(result);ChangeJob *j=g_task_get_task_data(task);GError *error=NULL;gboolean ok=g_task_propagate_boolean(task,&error);
 GtkWidget *box=g_weak_ref_get(&j->box);DiskBrowser *b=box ? g_object_get_data(G_OBJECT(box),"insight-disk-browser") : NULL;
 if(b && b->alive) {
  b->operating=FALSE;g_clear_object(&b->mutation);gtk_widget_set_sensitive(b->create,b->root!=NULL);
  if(ok && b->active && same_path(b->root,j->root)) load(b,j->root);
  else if(!ok && b->generation==j->generation) { g_free(b->failure);b->failure=g_strdup(insight_tr(error ? error->message : "Operation failed"));b->operation_error=TRUE;status(b); }
  else status(b);
 }
 if(box) g_object_unref(box);
 g_clear_error(&error);
}
/* Never leave an open editor buffer referring to a renamed/trashed path or a
 * directory below it. This also covers unsaved changes in those buffers. */
static gboolean entry_is_open(const gchar *path) {
 GFile *source=g_file_new_for_path(path);gboolean found=FALSE;
 for(guint i=0;documents_array && i<documents_array->len && !found;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);
  if(!DOC_VALID(doc) || !doc->file_name) continue;
  GFile *file=g_file_new_for_path(doc->file_name);found=g_file_equal(file,source) || g_file_has_prefix(file,source);g_object_unref(file);
 }
 g_object_unref(source);return found;
}
static void change_entry(DiskBrowser *b,guint action) {
 if(!b->alive || b->loading || b->creating || b->operating) return;
 GtkTreeModel *model;GtkTreeIter iter;
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(b->view)),&model,&iter)) return;
 ChangeJob *j=g_new0(ChangeJob,1);g_weak_ref_init(&j->box,b->box);j->root=g_strdup(b->root);j->generation=b->generation;j->trash=action==DB_TRASH;
 gtk_tree_model_get(model,&iter,PATH,&j->source,FILE_ID,&j->file_id,ETAG,&j->etag,-1);
 g_object_ref(b->box);GtkWidget *parent=gtk_widget_get_toplevel(b->box);
 const gchar *title=j->trash ? "Move to Trash…" : (action==DB_RENAME ? "Rename…" : "Move to…");
 GtkWidget *dialog=gtk_dialog_new_with_buttons(title,GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,j->trash ? "Move to Trash" : "Apply",GTK_RESPONSE_ACCEPT,NULL);
 gtk_widget_set_name(dialog,"insight-disk-change-dialog");GtkWidget *content=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_container_set_border_width(GTK_CONTAINER(content),8);
 GtkWidget *label=gtk_label_new(j->source),*entry=gtk_entry_new(),*failure=gtk_label_new("");gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_MIDDLE);gtk_label_set_max_width_chars(GTK_LABEL(label),60);gtk_widget_set_tooltip_text(label,j->source);
 gtk_widget_set_name(entry,"insight-disk-change-destination");gtk_widget_set_name(failure,"insight-disk-change-error");gtk_label_set_line_wrap(GTK_LABEL(failure),TRUE);
 gchar *name=g_path_get_basename(j->source);gtk_entry_set_text(GTK_ENTRY(entry),action==DB_RENAME ? name : j->source);g_free(name);gtk_entry_set_max_length(GTK_ENTRY(entry),action==DB_RENAME ? 255 : 16384);gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE);
 gtk_box_pack_start(GTK_BOX(content),label,FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(content),entry,FALSE,FALSE,4);
 GtkWidget *notice=gtk_label_new(j->trash ? "Send this entry to the system trash? Permanent deletion is never used." : "Existing destinations are never replaced. Stored project paths are not rewritten.");gtk_label_set_line_wrap(GTK_LABEL(notice),TRUE);gtk_label_set_max_width_chars(GTK_LABEL(notice),60);gtk_box_pack_start(GTK_BOX(content),notice,FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(content),failure,FALSE,FALSE,4);
 gtk_dialog_set_default_response(GTK_DIALOG(dialog),j->trash ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_ACCEPT);insight_i18n_apply(dialog);gtk_widget_show_all(dialog);gtk_widget_hide(failure);if(j->trash) gtk_widget_hide(entry);else gtk_widget_grab_focus(entry);
 gboolean launched=FALSE;
 while(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && b->alive) {
  const gchar *value=gtk_entry_get_text(GTK_ENTRY(entry));const gchar *problem=NULL;
  if(b->creating || b->operating || b->generation!=j->generation || !same_path(b->root,j->root)) problem="Directory changed; reopen the operation";
  else if(!j->file_id || !*j->file_id) problem="Refresh the directory before changing this entry";
  else if(entry_is_open(j->source)) problem="Close documents in this entry before changing it";
  else if(!j->trash && (!*value || !g_utf8_validate(value,-1,NULL) || strpbrk(value,"\r\n") || (action==DB_RENAME && (!strcmp(value,".") || !strcmp(value,"..") || strpbrk(value,"/\\") || strlen(value)>255)) || (action==DB_MOVE && !g_path_is_absolute(value)))) problem="Enter a filename or an absolute destination path";
  if(problem) { gtk_label_set_text(GTK_LABEL(failure),insight_tr(problem));gtk_widget_show(failure);continue; }
  j->destination=j->trash ? NULL : (action==DB_RENAME ? g_build_filename(j->root,value,NULL) : g_strdup(value));
  insight_disk_browser_cancel(b->box);b->generation++;j->generation=b->generation;b->operating=TRUE;b->cancelled=FALSE;g_clear_pointer(&b->failure,g_free);gtk_widget_set_sensitive(b->create,FALSE);status(b);b->mutation=g_cancellable_new();
  GTask *task=g_task_new(NULL,b->mutation,change_done,NULL);g_task_set_task_data(task,j,change_job_free);g_task_run_in_thread(task,change_worker);g_object_unref(task);launched=TRUE;break;
 }
 gtk_widget_destroy(dialog);g_object_unref(b->box);if(!launched) change_job_free(j);
}

static void menu_action(GtkMenuItem *item,gpointer data) {
 DiskBrowser *b=data;if(!b->alive) return;guint action=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"insight-disk-action"));gboolean directory=FALSE;gchar *path=selected_path(b,&directory);
 if(action==DB_PREVIEW && path && !directory && b->open) b->open(path,FALSE,b->data);
 else if(action==DB_OPEN && path) { if(directory) load(b,path);else if(b->open) b->open(path,TRUE,b->data); }
 else if(action==DB_PATH || action==DB_NAME) { gchar *text=action==DB_NAME ? (path ? g_path_get_basename(path) : NULL) : g_strdup(path ? path : b->root);if(text) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text,-1);g_free(text); }
 else if(action==DB_UP) parent(NULL,b);
 else if(action==DB_REFRESH) refresh(NULL,b);
 else if(action==DB_CREATE) create_folder(NULL,b);
 else if(action==DB_OPTIONS) options(NULL,b);
 else if(action==DB_RENAME || action==DB_MOVE || action==DB_TRASH) change_entry(b,action);
 g_free(path);
}
static GtkWidget *menu(DiskBrowser *b) {
 if(!b->popup) {
  b->popup=gtk_menu_new();g_object_ref_sink(b->popup);gtk_menu_attach_to_widget(GTK_MENU(b->popup),b->view,NULL);g_object_set_data(G_OBJECT(b->view),"insight-disk-popup",b->popup);
  const gchar *labels[]={"Preview in Context","Open","Copy full path","Copy filename","Up","Refresh","New folder…","Font & Color…","Folder Browser Options…","Rename…","Move to…","Move to Trash…"};const gchar *names[]={"preview","open","copy-path","copy-name","up","refresh","create","appearance","options","rename","move","trash"};
  for(guint i=0;i<DB_ACTIONS;i++) { if(i==DB_PATH || i==DB_UP || i==DB_CREATE) gtk_menu_shell_append(GTK_MENU_SHELL(b->popup),gtk_separator_menu_item_new());GtkWidget *item=i==DB_STYLE ? insight_list_style_menu_item(b->view) : gtk_menu_item_new_with_label(labels[i]);gchar *name=g_strconcat("insight-disk-menu-",names[i],NULL);gtk_widget_set_name(item,name);g_free(name);b->actions[i]=item;g_object_set_data(G_OBJECT(item),"insight-disk-action",GUINT_TO_POINTER(i));if(i!=DB_STYLE) g_signal_connect(item,"activate",G_CALLBACK(menu_action),b);gtk_menu_shell_append(GTK_MENU_SHELL(b->popup),item); }
 }
 gboolean directory=FALSE;gchar *path=selected_path(b,&directory);gtk_widget_set_sensitive(b->actions[DB_PREVIEW],path && !directory);gtk_widget_set_sensitive(b->actions[DB_OPEN],path!=NULL);gtk_widget_set_sensitive(b->actions[DB_PATH],path || b->root);gtk_widget_set_sensitive(b->actions[DB_NAME],path!=NULL);gtk_widget_set_sensitive(b->actions[DB_UP],b->root!=NULL);gtk_widget_set_sensitive(b->actions[DB_REFRESH],b->root!=NULL);gtk_widget_set_sensitive(b->actions[DB_CREATE],b->root && !b->creating && !b->operating);for(guint i=DB_RENAME;i<=DB_TRASH;i++) gtk_widget_set_sensitive(b->actions[i],path && !b->loading && !b->creating && !b->operating);g_free(path);insight_i18n_apply(b->popup);gtk_widget_show_all(b->popup);return b->popup;
}
static gchar *selected_folder_path(DiskBrowser *b) {
 GtkTreeModel *model;GtkTreeIter iter;gchar *path=NULL;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(b->folders)),&model,&iter)) gtk_tree_model_get(model,&iter,1,&path,-1);
 return path;
}
static void folder_menu_action(GtkMenuItem *item,gpointer data) {
 DiskBrowser *b=data;if(!b->alive) return;guint action=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"insight-disk-folder-action"));gchar *path=selected_folder_path(b);
 if(action==FD_OPEN && path) load(b,path);
 else if(action==FD_PATH || action==FD_NAME) { gchar *text=action==FD_NAME ? (path ? g_path_get_basename(path) : NULL) : g_strdup(path ? path : b->root);if(text) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text,-1);g_free(text); }
 else if(action==FD_UP) parent(NULL,b);else if(action==FD_REFRESH) refresh(NULL,b);else if(action==FD_OPTIONS) options(NULL,b);
 g_free(path);
}
static GtkWidget *folder_menu(DiskBrowser *b) {
 if(!b->folder_popup) {
  b->folder_popup=gtk_menu_new();g_object_ref_sink(b->folder_popup);gtk_menu_attach_to_widget(GTK_MENU(b->folder_popup),b->folders,NULL);g_object_set_data(G_OBJECT(b->folders),"insight-disk-folder-popup",b->folder_popup);
  const gchar *labels[]={"Open folder","Copy full path","Copy filename","Up","Refresh","Font & Color…","Folder Browser Options…"},*names[]={"open","copy-path","copy-name","up","refresh","appearance","options"};
  for(guint i=0;i<FD_ACTIONS;i++) { GtkWidget *item=i==FD_STYLE ? insight_list_style_menu_item(b->folders) : gtk_menu_item_new_with_label(labels[i]);gchar *name=g_strconcat("insight-disk-folder-menu-",names[i],NULL);gtk_widget_set_name(item,name);g_free(name);b->folder_actions[i]=item;g_object_set_data(G_OBJECT(item),"insight-disk-folder-action",GUINT_TO_POINTER(i));if(i!=FD_STYLE) g_signal_connect(item,"activate",G_CALLBACK(folder_menu_action),b);gtk_menu_shell_append(GTK_MENU_SHELL(b->folder_popup),item); }
 }
 gchar *path=selected_folder_path(b);gtk_widget_set_sensitive(b->folder_actions[FD_OPEN],path!=NULL);gtk_widget_set_sensitive(b->folder_actions[FD_PATH],path || b->root);gtk_widget_set_sensitive(b->folder_actions[FD_NAME],path!=NULL);gtk_widget_set_sensitive(b->folder_actions[FD_UP],b->root!=NULL);gtk_widget_set_sensitive(b->folder_actions[FD_REFRESH],b->root!=NULL);g_free(path);insight_i18n_apply(b->folder_popup);gtk_widget_show_all(b->folder_popup);return b->folder_popup;
}

static gboolean popup_key(GtkWidget *widget,gpointer data) {
 DiskBrowser *b=data;if(!b->alive) return FALSE;GtkWidget *popup=widget==b->folders ? folder_menu(b) : menu(b);GdkEvent *event=gtk_get_current_event();
 if(event) { gtk_menu_popup_at_widget(GTK_MENU(popup),widget,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,event);gdk_event_free(event); }
 /* Programmatic popup-menu commands have no trigger event. The GTK3 fallback
  * supports that invocation without an invalid event/device warning. */
 else gtk_menu_popup(GTK_MENU(popup),NULL,NULL,NULL,NULL,0,gtk_get_current_event_time());
 return TRUE;
}
static gboolean popup_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 DiskBrowser *b=data;if(!b->alive || event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;
 gboolean setting=b->setting;b->setting=TRUE;GtkTreePath *row=NULL;GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));gboolean hit=gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&row,NULL,NULL,NULL);if(hit) { gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget),row,NULL,FALSE);gtk_tree_path_free(row); }else gtk_tree_selection_unselect_all(selection);gtk_widget_grab_focus(widget);if(!hit) gtk_tree_selection_unselect_all(selection);b->setting=setting;gtk_menu_popup_at_pointer(GTK_MENU(widget==b->folders ? folder_menu(b) : menu(b)),(GdkEvent *)event);return TRUE;
}
static void filter_changed(GtkEditable *entry,gpointer data) {
 DiskBrowser *b=data;GError *error=NULL;InsightLookupFilter *filter=insight_lookup_filter_new(gtk_entry_get_text(GTK_ENTRY(entry)),TRUE,&error);
 if(!filter) { gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(entry)),"error");gtk_widget_set_tooltip_text(GTK_WIDGET(entry),error ? error->message : "Invalid filter");g_clear_error(&error);return; }
 insight_lookup_filter_free(b->filter);b->filter=filter;g_free(b->valid_query);b->valid_query=g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(entry)),"error");gtk_widget_set_tooltip_text(GTK_WIDGET(entry),"Filter loaded files: fragments, glob, ?regex or ~fuzzy; folders stay visible");gtk_tree_model_filter_refilter(b->model);
}
static void selected(GtkTreeSelection *selection,gpointer data) {
 DiskBrowser *b=data;if(b->setting || !b->alive || !b->open) return;GtkTreeModel *model;GtkTreeIter iter;
 if(gtk_tree_selection_get_selected(selection,&model,&iter)) { gchar *path=NULL;gboolean directory=FALSE;gtk_tree_model_get(model,&iter,PATH,&path,DIRECTORY,&directory,-1);if(path && !directory) b->open(path,FALSE,b->data);g_free(path); }
}
static void activated(GtkTreeView *view,GtkTreePath *row,GtkTreeViewColumn *column,gpointer data) {
 (void)column;DiskBrowser *b=data;GtkTreeModel *model=gtk_tree_view_get_model(view);GtkTreeIter iter;if(!b->alive || !gtk_tree_model_get_iter(model,&iter,row)) return;
 gchar *path=NULL;gboolean directory=FALSE;gtk_tree_model_get(model,&iter,PATH,&path,DIRECTORY,&directory,-1);if(directory) load(b,path);else if(path && b->open) b->open(path,TRUE,b->data);g_free(path);
}
static void kind_cell(GtkTreeViewColumn *column,GtkCellRenderer *cell,GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)column;(void)data;gboolean directory=FALSE,link=FALSE;gtk_tree_model_get(model,iter,DIRECTORY,&directory,LINK,&link,-1);g_object_set(cell,"text",insight_tr(link ? "Link" : (directory ? "Folder" : "File")),NULL);
}
/* Rendering uses only the worker snapshot: no filesystem reads on the UI thread. */
static void metadata_cell(GtkTreeViewColumn *column,GtkCellRenderer *cell,GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)column;guint field=GPOINTER_TO_UINT(data);gint64 value=-1;gtk_tree_model_get(model,iter,field,&value,-1);gchar *text=NULL;
 if(value>=0) { if(field==SIZE) text=g_format_size((guint64)value);else { GDateTime *date=g_date_time_new_from_unix_local(value);if(date) { text=g_date_time_format(date,"%Y-%m-%d %H:%M:%S");g_date_time_unref(date); } } }
 g_object_set(cell,"text",text ? text : "",NULL);g_free(text);
}
static void metadata_toggled(GtkCheckMenuItem *item,gpointer data) { gtk_tree_view_column_set_visible(GTK_TREE_VIEW_COLUMN(data),gtk_check_menu_item_get_active(item)); }
/* GtkTreeModelFilter is not sortable. Route header clicks to the backing store
 * and keep indicators in sync for both clicks and restored settings. */
static void sort_changed(GtkTreeSortable *sortable,gpointer data) {
 DiskBrowser *b=data;gint id=GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;GtkSortType direction=GTK_SORT_ASCENDING;gtk_tree_sortable_get_sort_column_id(sortable,&id,&direction);GtkTreeViewColumn *columns[]={b->name_column,b->kind_column,b->size_column,b->date_column};
 for(guint i=0;i<4;i++) { gtk_tree_view_column_set_sort_indicator(columns[i],GPOINTER_TO_INT(g_object_get_data(G_OBJECT(columns[i]),"insight-disk-sort-column"))-1==id);gtk_tree_view_column_set_sort_order(columns[i],direction); }
}
static void sort_clicked(GtkTreeViewColumn *column,gpointer data) {
 DiskBrowser *b=data;gint id=GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,next=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column),"insight-disk-sort-column"))-1;GtkSortType direction=GTK_SORT_ASCENDING;gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(b->store),&id,&direction);gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(b->store),next,id==next && direction==GTK_SORT_ASCENDING ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;DiskBrowser *b=data;guint mods=event->state&gtk_accelerator_get_default_mod_mask();
 if(mods==GEANY_PRIMARY_MOD_MASK && event->keyval==GDK_KEY_q) { options(NULL,b);return TRUE; }
 if(event->keyval==GDK_KEY_F2 && !mods && gtk_widget_has_focus(b->view)) { change_entry(b,DB_RENAME);return TRUE; }
 if(event->keyval==GDK_KEY_F5 && !mods) { refresh(NULL,b);return TRUE; }
 if(event->keyval==GDK_KEY_Up && mods==GDK_MOD1_MASK) { parent(NULL,b);return TRUE; }
 if(mods==GEANY_PRIMARY_MOD_MASK && (event->keyval==GDK_KEY_l || event->keyval==GDK_KEY_f)) { gtk_widget_grab_focus(event->keyval==GDK_KEY_l ? b->path : b->query);return TRUE; }
 return FALSE;
}
static void destroyed(GtkWidget *widget,gpointer data) { (void)widget;DiskBrowser *b=data;b->alive=FALSE;b->generation++;b->open=NULL;if(b->cancel) g_cancellable_cancel(b->cancel);if(b->mutation) g_cancellable_cancel(b->mutation);if(b->popup) gtk_widget_destroy(b->popup);if(b->folder_popup) gtk_widget_destroy(b->folder_popup);if(b->folder_reveal) { guint id=b->folder_reveal;b->folder_reveal=0;g_source_remove(id); } }
void insight_disk_browser_shutdown(GtkWidget *box) { if(!box) return;DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(b && b->alive) destroyed(box,b); }
static void freed(gpointer data) { DiskBrowser *b=data;g_clear_object(&b->popup);g_clear_object(&b->folder_popup);g_clear_object(&b->cancel);g_clear_object(&b->mutation);insight_lookup_filter_free(b->filter);g_object_unref(b->model);g_object_unref(b->store);g_object_unref(b->folder_store);g_free(b->root);g_free(b->failure);g_free(b->valid_query);g_free(b->current_file);g_free(b); }
static void kind_toggled(GtkCheckMenuItem *item,gpointer data) { DiskBrowser *b=data;gtk_tree_view_column_set_visible(b->kind_column,gtk_check_menu_item_get_active(item)); }
void insight_disk_browser_write(GtkWidget *box,GKeyFile *settings) {
 DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");if(!b || !settings) return;const gchar *group="disk-browser";
 insight_list_style_write(b->view,settings,"disk-browser-style");insight_list_style_write(b->folders,settings,"disk-folders-style");
 g_key_file_set_boolean(settings,group,"adaptive-layout",b->adaptive);g_key_file_set_boolean(settings,group,"stack-vertically",b->vertical);g_key_file_set_integer(settings,group,"horizontal-position",b->split_h);g_key_file_set_integer(settings,group,"vertical-position",b->split_v);
 g_key_file_set_integer(settings,group,"version",1);g_key_file_set_string(settings,group,"directory",b->root && strlen(b->root)<=16384 ? b->root : "");g_key_file_set_string(settings,group,"query",b->valid_query ? b->valid_query : "");g_key_file_set_boolean(settings,group,"hidden",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->hidden)));
 g_key_file_set_boolean(settings,group,"follow-current-file",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->follow)));
 g_key_file_set_integer(settings,group,"name-width",CLAMP(gtk_tree_view_column_get_fixed_width(b->name_column),32,2000));g_key_file_set_integer(settings,group,"kind-width",CLAMP(gtk_tree_view_column_get_fixed_width(b->kind_column),32,2000));g_key_file_set_boolean(settings,group,"show-kind",gtk_tree_view_column_get_visible(b->kind_column));g_key_file_set_boolean(settings,group,"kind-first",gtk_tree_view_get_column(GTK_TREE_VIEW(b->view),0)==b->kind_column);
 GtkTreeViewColumn *columns[]={b->name_column,b->kind_column,b->size_column,b->date_column};gint order[4];for(guint i=0;i<4;i++) for(guint j=0;j<4;j++) if(gtk_tree_view_get_column(GTK_TREE_VIEW(b->view),i)==columns[j]) order[i]=(gint)j;
 g_key_file_set_integer_list(settings,group,"column-order",order,4);
 g_key_file_set_integer(settings,group,"size-width",CLAMP(gtk_tree_view_column_get_fixed_width(b->size_column),32,2000));g_key_file_set_integer(settings,group,"date-width",CLAMP(gtk_tree_view_column_get_fixed_width(b->date_column),32,2000));
 g_key_file_set_boolean(settings,group,"show-size",gtk_tree_view_column_get_visible(b->size_column));g_key_file_set_boolean(settings,group,"show-date",gtk_tree_view_column_get_visible(b->date_column));
 gint sort=GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;GtkSortType direction=GTK_SORT_ASCENDING;gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(b->store),&sort,&direction);g_key_file_set_integer(settings,group,"sort-column",sort);g_key_file_set_integer(settings,group,"sort-order",direction);
}
static gboolean saved_boolean(GKeyFile *settings,const gchar *key,gboolean fallback) { GError *error=NULL;gboolean value=g_key_file_get_boolean(settings,"disk-browser",key,&error);if(error) { g_error_free(error);return fallback; }return value; }
gboolean insight_disk_browser_read(GtkWidget *box,GKeyFile *settings) {
 DiskBrowser *b=g_object_get_data(G_OBJECT(box),"insight-disk-browser");const gchar *group="disk-browser";if(!b || !settings || g_key_file_get_integer(settings,group,"version",NULL)!=1) return FALSE;
 insight_list_style_read(b->view,settings,"disk-browser-style");insight_list_style_read(b->folders,settings,"disk-folders-style");
 b->adaptive=saved_boolean(settings,"adaptive-layout",TRUE);b->vertical=saved_boolean(settings,"stack-vertically",TRUE);
 gint horizontal=g_key_file_get_integer(settings,group,"horizontal-position",NULL),vertical=g_key_file_get_integer(settings,group,"vertical-position",NULL);if(horizontal>=40 && horizontal<=2000) b->split_h=horizontal;if(vertical>=40 && vertical<=2000) b->split_v=vertical;
 insight_disk_browser_cancel(box);b->setting=TRUE;gtk_list_store_clear(b->store);gtk_tree_store_clear(b->folder_store);gchar *root=g_key_file_get_string(settings,group,"directory",NULL),*query=g_key_file_get_string(settings,group,"query",NULL);
 if(root && *root && strlen(root)<=16384 && g_utf8_validate(root,-1,NULL) && g_path_is_absolute(root)) { g_free(b->root);b->root=g_canonicalize_filename(root,NULL);gtk_entry_set_text(GTK_ENTRY(b->path),b->root); }
 InsightLookupFilter *filter=query ? insight_lookup_filter_new(query,TRUE,NULL) : NULL;if(filter) { gtk_entry_set_text(GTK_ENTRY(b->query),query);insight_lookup_filter_free(filter); }
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b->hidden),saved_boolean(settings,"hidden",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b->hidden))));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b->follow),saved_boolean(settings,"follow-current-file",FALSE));
 gint name=g_key_file_get_integer(settings,group,"name-width",NULL),kind=g_key_file_get_integer(settings,group,"kind-width",NULL);if(name>=32 && name<=2000) gtk_tree_view_column_set_fixed_width(b->name_column,name);if(kind>=32 && kind<=2000) gtk_tree_view_column_set_fixed_width(b->kind_column,kind);
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(b->kind_toggle),saved_boolean(settings,"show-kind",gtk_tree_view_column_get_visible(b->kind_column)));
 gtk_tree_view_move_column_after(GTK_TREE_VIEW(b->view),saved_boolean(settings,"kind-first",gtk_tree_view_get_column(GTK_TREE_VIEW(b->view),0)==b->kind_column) ? b->kind_column : b->name_column,NULL);
 GtkTreeViewColumn *columns[]={b->name_column,b->kind_column,b->size_column,b->date_column};gsize length=0;gint *order=g_key_file_get_integer_list(settings,group,"column-order",&length,NULL);guint seen=0;gboolean valid=order && length==4;
 for(guint i=0;valid && i<4;i++) { if(order[i]<0 || order[i]>3 || (seen&(1u<<order[i]))) valid=FALSE;else seen|=1u<<order[i]; }
 if(valid) { GtkTreeViewColumn *previous=NULL;for(guint i=0;i<4;i++) { gtk_tree_view_move_column_after(GTK_TREE_VIEW(b->view),columns[order[i]],previous);previous=columns[order[i]]; } }g_free(order);
 const gchar *width_keys[]={"size-width","date-width"};for(guint i=0;i<2;i++) { gint width=g_key_file_get_integer(settings,group,width_keys[i],NULL);if(width>=32 && width<=2000) gtk_tree_view_column_set_fixed_width(columns[i+2],width); }
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(b->size_toggle),saved_boolean(settings,"show-size",gtk_tree_view_column_get_visible(b->size_column)));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(b->date_toggle),saved_boolean(settings,"show-date",gtk_tree_view_column_get_visible(b->date_column)));
 GError *error=NULL;gint sort=g_key_file_get_integer(settings,group,"sort-column",&error);gint direction=error ? -1 : g_key_file_get_integer(settings,group,"sort-order",&error);if(!error && (sort==NAME || sort==DIRECTORY || sort==SIZE || sort==MODIFIED || sort==GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID) && (direction==GTK_SORT_ASCENDING || direction==GTK_SORT_DESCENDING)) { gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(b->store),sort,direction); }g_clear_error(&error);
 b->setting=FALSE;folders_rebuild(b,NULL,FALSE);split_arrange(b,gtk_widget_get_allocated_width(b->split),gtk_widget_get_allocated_height(b->split));g_free(root);g_free(query);return b->root!=NULL;
}
GtkWidget *insight_disk_browser_new(InsightDiskOpen open,gpointer data) {
 DiskBrowser *b=g_new0(DiskBrowser,1);b->alive=TRUE;b->adaptive=TRUE;b->vertical=TRUE;b->split_h=180;b->split_v=130;b->open=open;b->data=data;b->filter=insight_lookup_filter_new("",TRUE,NULL);b->valid_query=g_strdup("");b->box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_widget_set_name(b->box,"insight-disk-browser");g_object_set_data_full(G_OBJECT(b->box),"insight-disk-browser",b,freed);g_signal_connect(b->box,"destroy",G_CALLBACK(destroyed),b);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*up=gtk_button_new_with_label("Up"),*go=gtk_button_new_with_label("Refresh");gtk_widget_set_name(up,"insight-disk-up");gtk_widget_set_name(go,"insight-disk-refresh");b->path=gtk_entry_new();gtk_widget_set_name(b->path,"insight-disk-root");gtk_entry_set_max_length(GTK_ENTRY(b->path),16384);gtk_box_pack_start(GTK_BOX(row),up,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),b->path,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),go,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(b->box),row,FALSE,FALSE,0);
 b->query=gtk_search_entry_new();gtk_widget_set_name(b->query,"insight-disk-query");gtk_entry_set_max_length(GTK_ENTRY(b->query),1024);gtk_entry_set_placeholder_text(GTK_ENTRY(b->query),"Filter disk files…");gtk_box_pack_start(GTK_BOX(b->box),b->query,FALSE,FALSE,0);
 b->hidden=gtk_check_button_new_with_label("Show hidden disk entries");gtk_widget_set_name(b->hidden,"insight-disk-hidden");gtk_box_pack_start(GTK_BOX(b->box),b->hidden,FALSE,FALSE,0);
 b->follow=gtk_check_button_new_with_label("Follow current file");gtk_widget_set_name(b->follow,"insight-disk-follow");gtk_box_pack_start(GTK_BOX(b->box),b->follow,FALSE,FALSE,0);g_signal_connect(b->follow,"toggled",G_CALLBACK(follow_changed),b);
 b->store=gtk_list_store_new(COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_BOOLEAN,G_TYPE_INT64,G_TYPE_INT64,G_TYPE_STRING,G_TYPE_STRING);b->model=GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(b->store),NULL));gtk_tree_model_filter_set_visible_func(b->model,visible,b,NULL);b->view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(b->model));gtk_widget_set_name(b->view,"insight-disk-files");b->name_column=gtk_tree_view_column_new_with_attributes("Disk file or folder",gtk_cell_renderer_text_new(),"text",NAME,NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(b->view),b->name_column);GtkCellRenderer *kind=gtk_cell_renderer_text_new();GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes("Kind",kind,NULL);b->kind_column=column;gtk_tree_view_column_set_cell_data_func(column,kind,kind_cell,NULL,NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(b->view),column);gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(b->view),PATH);
 GtkCellRenderer *size=gtk_cell_renderer_text_new(),*date=gtk_cell_renderer_text_new();g_object_set(size,"xalign",1.0f,NULL);b->size_column=gtk_tree_view_column_new_with_attributes("File Size",size,NULL);b->date_column=gtk_tree_view_column_new_with_attributes("File Date",date,NULL);gtk_tree_view_column_set_cell_data_func(b->size_column,size,metadata_cell,GUINT_TO_POINTER(SIZE),NULL);gtk_tree_view_column_set_cell_data_func(b->date_column,date,metadata_cell,GUINT_TO_POINTER(MODIFIED),NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(b->view),b->size_column);gtk_tree_view_append_column(GTK_TREE_VIEW(b->view),b->date_column);gtk_tree_view_column_set_visible(b->size_column,FALSE);
 GtkTreeViewColumn *columns[]={b->name_column,b->kind_column,b->size_column,b->date_column};gint ids[]={NAME,DIRECTORY,SIZE,MODIFIED};gint widths[]={240,90,100,175};for(guint i=0;i<4;i++) {g_object_set_data(G_OBJECT(columns[i]),"insight-disk-sort-column",GINT_TO_POINTER(ids[i]+1));gtk_tree_view_column_set_clickable(columns[i],TRUE); gtk_tree_view_column_set_sizing(columns[i],GTK_TREE_VIEW_COLUMN_FIXED);gtk_tree_view_column_set_fixed_width(columns[i],widths[i]);gtk_tree_view_column_set_resizable(columns[i],TRUE);gtk_tree_view_column_set_reorderable(columns[i],TRUE);g_signal_connect_after(columns[i],"clicked",G_CALLBACK(sort_clicked),b); }g_signal_connect(b->store,"sort-column-changed",G_CALLBACK(sort_changed),b);
 GtkWidget *menu=gtk_menu_new(),*choose=gtk_menu_button_new();gtk_button_set_label(GTK_BUTTON(choose),"Columns");gtk_widget_set_name(choose,"insight-disk-columns");b->kind_toggle=gtk_check_menu_item_new_with_label("Kind");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(b->kind_toggle),TRUE);gtk_menu_shell_append(GTK_MENU_SHELL(menu),b->kind_toggle);gtk_menu_button_set_popup(GTK_MENU_BUTTON(choose),menu);gtk_box_pack_start(GTK_BOX(row),choose,FALSE,FALSE,0);g_signal_connect(b->kind_toggle,"toggled",G_CALLBACK(kind_toggled),b);
 b->size_toggle=gtk_check_menu_item_new_with_label("File Size");b->date_toggle=gtk_check_menu_item_new_with_label("File Date");gtk_widget_set_name(b->size_toggle,"insight-disk-show-size");gtk_widget_set_name(b->date_toggle,"insight-disk-show-date");GtkWidget *toggles[]={b->size_toggle,b->date_toggle};for(guint i=0;i<2;i++) { gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(toggles[i]),gtk_tree_view_column_get_visible(columns[i+2]));gtk_menu_shell_append(GTK_MENU_SHELL(menu),toggles[i]);g_signal_connect(toggles[i],"toggled",G_CALLBACK(metadata_toggled),columns[i+2]); }g_signal_connect_swapped(menu,"show",G_CALLBACK(insight_i18n_apply),menu);gtk_widget_show_all(menu);

 GtkWidget *settings=gtk_button_new_with_label("Folder Browser Options…");gtk_widget_set_name(settings,"insight-disk-options");gtk_widget_set_tooltip_text(settings,"Folder Browser Options (Ctrl+Q)");gtk_box_pack_start(GTK_BOX(b->box),settings,FALSE,FALSE,0);g_signal_connect(settings,"clicked",G_CALLBACK(options),b);
 GtkWidget *appearance_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*appearance=insight_list_style_button(b->view);gtk_widget_set_name(appearance,"insight-disk-appearance");gtk_box_pack_start(GTK_BOX(appearance_row),appearance,TRUE,TRUE,0);b->create=gtk_button_new_with_label("New folder…");gtk_widget_set_name(b->create,"insight-disk-create");gtk_widget_set_sensitive(b->create,FALSE);gtk_box_pack_start(GTK_BOX(appearance_row),b->create,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(b->box),appearance_row,FALSE,FALSE,0);g_signal_connect(b->create,"clicked",G_CALLBACK(create_folder),b);
 b->folder_store=gtk_tree_store_new(2,G_TYPE_STRING,G_TYPE_STRING);b->folders=gtk_tree_view_new_with_model(GTK_TREE_MODEL(b->folder_store));gtk_widget_set_name(b->folders,"insight-disk-folders");g_signal_connect_after(b->folders,"size-allocate",G_CALLBACK(folder_allocated),b);GtkCellRenderer *folder_cell=gtk_cell_renderer_text_new();g_object_set(folder_cell,"ellipsize",PANGO_ELLIPSIZE_END,NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(b->folders),gtk_tree_view_column_new_with_attributes("Disk folders",folder_cell,"text",0,NULL));gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(b->folders),1);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(b->folders)),"changed",G_CALLBACK(folder_selected),b);g_signal_connect(b->folders,"button-press-event",G_CALLBACK(popup_button),b);g_signal_connect(b->folders,"popup-menu",G_CALLBACK(popup_key),b);g_signal_connect(b->folders,"row-activated",G_CALLBACK(folder_activated),b);
 GtkWidget *folder_scroll=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_size_request(folder_scroll,60,50);gtk_container_add(GTK_CONTAINER(folder_scroll),b->folders);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_size_request(scroll,80,70);gtk_container_add(GTK_CONTAINER(scroll),b->view);
 b->split=gtk_paned_new(GTK_ORIENTATION_VERTICAL);gtk_widget_set_name(b->split,"insight-disk-split");gtk_paned_pack1(GTK_PANED(b->split),folder_scroll,FALSE,TRUE);gtk_paned_pack2(GTK_PANED(b->split),scroll,TRUE,TRUE);gtk_box_pack_start(GTK_BOX(b->box),b->split,TRUE,TRUE,0);g_signal_connect(b->split,"size-allocate",G_CALLBACK(split_allocated),b);g_signal_connect(b->split,"notify::position",G_CALLBACK(split_position),b);b->status=gtk_label_new("Choose a disk directory");gtk_widget_set_name(b->status,"insight-disk-status");gtk_label_set_line_wrap(GTK_LABEL(b->status),TRUE);gtk_box_pack_start(GTK_BOX(b->box),b->status,FALSE,FALSE,0);
 g_signal_connect(up,"clicked",G_CALLBACK(parent),b);g_signal_connect(go,"clicked",G_CALLBACK(refresh),b);g_signal_connect(b->path,"activate",G_CALLBACK(refresh),b);g_signal_connect(b->hidden,"toggled",G_CALLBACK(refresh),b);g_signal_connect(b->query,"changed",G_CALLBACK(filter_changed),b);g_signal_connect(b->view,"row-activated",G_CALLBACK(activated),b);g_signal_connect(b->view,"button-press-event",G_CALLBACK(popup_button),b);g_signal_connect(b->view,"popup-menu",G_CALLBACK(popup_key),b);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(b->view)),"changed",G_CALLBACK(selected),b);g_signal_connect(b->box,"key-press-event",G_CALLBACK(key),b);filter_changed(GTK_EDITABLE(b->query),b);return b->box;
}
