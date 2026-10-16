/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "folder_tree.h"
#include "folder_layout.h"
#include "../../src/insight-i18n.h"
#include <string.h>
#define TREE_LIMIT 4096u
#define FOLDER_PAGE 256u
#define MAX_READS 8u
typedef struct { GtkTreeView *view;GtkTreeStore *store;GHashTable *pending;gchar *current;gboolean hidden,setting,alive; } FolderTree;
typedef struct { GWeakRef view;GtkTreeRowReference *row;gchar *path;guint limit;gboolean hidden,limited; } FolderRead;
typedef struct { gchar *name,*path; } FolderEntry;
static FolderTree *tree(GtkTreeView *view) { return g_object_get_data(G_OBJECT(view),"insight-folder-tree"); }
gboolean insight_folder_tree_is_updating(GtkTreeView *view) { FolderTree *t=tree(view);return t && t->setting; }
static void entry_free(gpointer data) { FolderEntry *e=data;g_free(e->name);g_free(e->path);g_free(e); }
static void read_free(gpointer data) { FolderRead *r=data;g_weak_ref_clear(&r->view);gtk_tree_row_reference_free(r->row);g_free(r->path);g_free(r); }
static gboolean same(const gchar *a,const gchar *b) { if(!a || !b) return FALSE;GFile *x=g_file_new_for_path(a),*y=g_file_new_for_path(b);gboolean equal=g_file_equal(x,y);g_object_unref(x);g_object_unref(y);return equal; }
static gboolean contains_current(FolderTree *t,const gchar *path) { if(!t->current || !path) return FALSE;GFile *a=g_file_new_for_path(t->current),*b=g_file_new_for_path(path);gboolean inside=g_file_equal(a,b) || g_file_has_prefix(a,b);g_object_unref(a);g_object_unref(b);return inside; }
static gboolean count_row(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) { (void)path;gchar *value=NULL;gtk_tree_model_get(model,iter,INSIGHT_FOLDER_PATH,&value,-1);if(value) (*(guint *)data)++;g_free(value);return FALSE; }
static guint count(FolderTree *t) { guint n=0;gtk_tree_model_foreach(GTK_TREE_MODEL(t->store),count_row,&n);return n; }
static gboolean child(FolderTree *t,GtkTreeIter *parent,const gchar *path,GtkTreeIter *result)
{
 GtkTreeIter iter;GtkTreeModel *model=GTK_TREE_MODEL(t->store);
 for(gboolean valid=gtk_tree_model_iter_children(model,&iter,parent);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *value;gtk_tree_model_get(model,&iter,INSIGHT_FOLDER_PATH,&value,-1);gboolean equal=same(path,value);g_free(value);if(equal) { *result=iter;return TRUE; } }return FALSE;
}
static void placeholder(FolderTree *t,GtkTreeIter *parent,const gchar *text)
{
 GtkTreeModel *model=GTK_TREE_MODEL(t->store);GtkTreeIter iter;
 GtkTreePath *row=gtk_tree_model_get_path(model,parent);gboolean was_expanded=gtk_tree_view_row_expanded(t->view,row),setting=t->setting;t->setting=TRUE;
 for(gboolean valid=gtk_tree_model_iter_children(model,&iter,parent);valid;) { gchar *path;gtk_tree_model_get(model,&iter,INSIGHT_FOLDER_PATH,&path,-1);if(!path) valid=gtk_tree_store_remove(t->store,&iter);else valid=gtk_tree_model_iter_next(model,&iter);g_free(path); }
 if(text) { gtk_tree_store_append(t->store,&iter,parent);gtk_tree_store_set(t->store,&iter,INSIGHT_FOLDER_NAME,text,INSIGHT_FOLDER_STATE,INSIGHT_FOLDER_PLACEHOLDER,INSIGHT_FOLDER_TOOLTIP,insight_tr(text),-1); }
 if(was_expanded) gtk_tree_view_expand_row(t->view,row,FALSE);
 gtk_tree_path_free(row);t->setting=setting;
}
static void append(FolderTree *t,GtkTreeIter *parent,GtkTreeIter *iter,const gchar *name,const gchar *path)
{
 gtk_tree_store_append(t->store,iter,parent);gtk_tree_store_set(t->store,iter,INSIGHT_FOLDER_NAME,name,INSIGHT_FOLDER_PATH,path,INSIGHT_FOLDER_STATE,INSIGHT_FOLDER_UNKNOWN,INSIGHT_FOLDER_TOOLTIP,path,-1);placeholder(t,iter,"Expand to read subfolders");
 /* A known descendant already gives an ancestor its expansion arrow. Do not
  * display one redundant hint at every level of the current path. */
 if(parent) { gint state;gtk_tree_model_get(GTK_TREE_MODEL(t->store),parent,INSIGHT_FOLDER_STATE,&state,-1);if(state==INSIGHT_FOLDER_UNKNOWN) placeholder(t,parent,NULL); }
}
static gint sort_rows(GtkTreeModel *model,GtkTreeIter *a,GtkTreeIter *b,gpointer data)
{ (void)data;gchar *ap,*bp,*an,*bn;gtk_tree_model_get(model,a,INSIGHT_FOLDER_PATH,&ap,INSIGHT_FOLDER_NAME,&an,-1);gtk_tree_model_get(model,b,INSIGHT_FOLDER_PATH,&bp,INSIGHT_FOLDER_NAME,&bn,-1);gint result=!ap!=!bp ? (ap ? -1 : 1) : g_utf8_collate(an ? an : "",bn ? bn : "");g_free(ap);g_free(bp);g_free(an);g_free(bn);return result; }
static gint sort_entries(gconstpointer a,gconstpointer b) { return g_utf8_collate((*(FolderEntry *const *)a)->name,(*(FolderEntry *const *)b)->name); }
static void read_worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel)
{
 (void)source;FolderRead *r=data;GError *error=NULL;GFile *file=g_file_new_for_path(r->path);
 /* Do not traverse a link/reparse point substituted after the UI snapshot. */
 GFileInfo *self=g_file_query_info(file,"standard::type,standard::is-symlink",G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,&error);
 if(self && (g_file_info_get_is_symlink(self) || g_file_info_get_file_type(self)!=G_FILE_TYPE_DIRECTORY)) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY,"Folder is missing or is a symbolic link");
 g_clear_object(&self);GFileEnumerator *reader=error ? NULL : g_file_enumerate_children(file,"standard::name,standard::type,standard::is-symlink,standard::is-hidden",G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,&error);g_object_unref(file);
 GPtrArray *entries=g_ptr_array_new_with_free_func(entry_free);guint visited=0;
 if(reader) {
  while(!g_cancellable_is_cancelled(cancel)) {
   GFileInfo *info=g_file_enumerator_next_file(reader,cancel,&error);if(!info) break;
   if(++visited>100000u) { r->limited=TRUE;g_object_unref(info);break; }
   const gchar *name=g_file_info_get_name(info);
   if(name && g_utf8_validate(name,-1,NULL) && g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY && !g_file_info_get_is_symlink(info) && (r->hidden || (!g_file_info_get_is_hidden(info) && name[0]!='.'))) {
    if(entries->len>=r->limit) { r->limited=TRUE;g_object_unref(info);break; }
    FolderEntry *e=g_new0(FolderEntry,1);e->name=g_strdup(name);e->path=g_build_filename(r->path,name,NULL);if(strlen(e->path)<=16384) g_ptr_array_add(entries,e);else entry_free(e);
   }
   g_object_unref(info);
  }
  g_object_unref(reader);
 }
 if(g_cancellable_is_cancelled(cancel) && !error) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Folder read cancelled");
 if(error) { g_ptr_array_unref(entries);g_task_return_error(task,error);return; }g_ptr_array_sort(entries,sort_entries);g_task_return_pointer(task,entries,(GDestroyNotify)g_ptr_array_unref);
}
static void request(FolderTree *,GtkTreeIter *,guint);
static void read_done(GObject *source,GAsyncResult *result,gpointer data)
{
 (void)source;(void)data;GTask *task=G_TASK(result);FolderRead *r=g_task_get_task_data(task);GError *error=NULL;GPtrArray *entries=g_task_propagate_pointer(task,&error);
 GtkTreeView *view=g_weak_ref_get(&r->view);FolderTree *t=view ? tree(view) : NULL;
 if(t && t->alive && g_hash_table_lookup(t->pending,r->path)==g_task_get_cancellable(task)) {
  g_hash_table_remove(t->pending,r->path);GtkTreePath *row=gtk_tree_row_reference_get_path(r->row);GtkTreeIter parent;
  if(row && gtk_tree_model_get_iter(GTK_TREE_MODEL(t->store),&parent,row)) {
   gchar *path;gtk_tree_model_get(GTK_TREE_MODEL(t->store),&parent,INSIGHT_FOLDER_PATH,&path,-1);
   if(same(path,r->path)) {
    gboolean setting=t->setting,was_expanded=gtk_tree_view_row_expanded(view,row);t->setting=TRUE;
    if(error) { gtk_tree_store_set(t->store,&parent,INSIGHT_FOLDER_STATE,INSIGHT_FOLDER_ERROR,INSIGHT_FOLDER_TOOLTIP,error->message,-1);placeholder(t,&parent,"Cannot read subfolders; refresh to retry"); }
    else {
     GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);for(guint i=0;i<entries->len;i++) g_hash_table_add(seen,((FolderEntry *)g_ptr_array_index(entries,i))->path);
     GtkTreeIter iter;GtkTreeModel *model=GTK_TREE_MODEL(t->store);
     for(gboolean valid=gtk_tree_model_iter_children(model,&iter,&parent);valid;) { gchar *old;gtk_tree_model_get(model,&iter,INSIGHT_FOLDER_PATH,&old,-1);gboolean keep=old && (g_hash_table_contains(seen,old) || contains_current(t,old));if(!keep) valid=gtk_tree_store_remove(t->store,&iter);else valid=gtk_tree_model_iter_next(model,&iter);g_free(old); }
     guint used=count(t);gboolean full=FALSE;
     for(guint i=0;i<entries->len;i++) { FolderEntry *entry=g_ptr_array_index(entries,i);if(child(t,&parent,entry->path,&iter)) continue;if(used>=TREE_LIMIT) { full=TRUE;break; }append(t,&parent,&iter,entry->name,entry->path);used++; }
     g_hash_table_unref(seen);gtk_tree_store_set(t->store,&parent,INSIGHT_FOLDER_STATE,(r->limited || full) ? INSIGHT_FOLDER_LIMITED : INSIGHT_FOLDER_READY,INSIGHT_FOLDER_TOOLTIP,full ? insight_tr("Folder tree limit reached; clear cached folders to continue") : r->path,-1);
     placeholder(t,&parent,full ? "Folder tree limit reached; clear cached folders to continue" : (r->limited ? "Read more subfolders…" : NULL));
    }
    if(was_expanded) { GtkTreePath *updated=gtk_tree_model_get_path(GTK_TREE_MODEL(t->store),&parent);gtk_tree_view_expand_row(view,updated,FALSE);gtk_tree_path_free(updated); }
    t->setting=setting;
   }g_free(path);
  }if(row) gtk_tree_path_free(row);
 }
 if(view) g_object_unref(view);
 g_clear_pointer(&entries,g_ptr_array_unref);g_clear_error(&error);
}
static void request(FolderTree *t,GtkTreeIter *iter,guint limit)
{
 gchar *path;gtk_tree_model_get(GTK_TREE_MODEL(t->store),iter,INSIGHT_FOLDER_PATH,&path,-1);if(!path) return;
 GCancellable *old=g_hash_table_lookup(t->pending,path);if(old) g_cancellable_cancel(old);
 if(!old && g_hash_table_size(t->pending)>=MAX_READS) { gtk_tree_store_set(t->store,iter,INSIGHT_FOLDER_STATE,INSIGHT_FOLDER_ERROR,INSIGHT_FOLDER_TOOLTIP,insight_tr("Too many folder reads; refresh to retry"),-1);placeholder(t,iter,"Too many folder reads; refresh to retry");g_free(path);return; }
 GCancellable *cancel=g_cancellable_new();g_hash_table_replace(t->pending,g_strdup(path),cancel);
 FolderRead *r=g_new0(FolderRead,1);g_weak_ref_init(&r->view,t->view);GtkTreePath *row=gtk_tree_model_get_path(GTK_TREE_MODEL(t->store),iter);r->row=gtk_tree_row_reference_new(GTK_TREE_MODEL(t->store),row);gtk_tree_path_free(row);r->path=path;r->hidden=t->hidden;r->limit=MIN(MAX(limit,FOLDER_PAGE),TREE_LIMIT);
 gtk_tree_store_set(t->store,iter,INSIGHT_FOLDER_STATE,INSIGHT_FOLDER_LOADING,INSIGHT_FOLDER_TOOLTIP,insight_tr("Reading subfolders…"),-1);placeholder(t,iter,"Reading subfolders…");
 GTask *task=g_task_new(NULL,cancel,read_done,NULL);g_task_set_task_data(task,r,read_free);g_task_run_in_thread(task,read_worker);g_object_unref(task);
}
static void expanded(GtkTreeView *view,GtkTreeIter *iter,GtkTreePath *path,gpointer data)
{ (void)view;(void)path;FolderTree *t=data;if(!t->alive || t->setting) return;gint state;gtk_tree_model_get(GTK_TREE_MODEL(t->store),iter,INSIGHT_FOLDER_STATE,&state,-1);if(state==INSIGHT_FOLDER_UNKNOWN || state==INSIGHT_FOLDER_ERROR) request(t,iter,FOLDER_PAGE); }
static gboolean selected(FolderTree *t,GtkTreeIter *iter)
{
 GtkTreeModel *model;GtkTreeIter row;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(t->view),&model,&row)) return FALSE;gchar *path;gtk_tree_model_get(model,&row,INSIGHT_FOLDER_PATH,&path,-1);gboolean real=path!=NULL;g_free(path);if(real) { *iter=row;return TRUE; }return gtk_tree_model_iter_parent(model,iter,&row);
}
static gboolean cancelled_row(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data)
{ gint state;gtk_tree_model_get(model,iter,INSIGHT_FOLDER_STATE,&state,-1);if(state==INSIGHT_FOLDER_LOADING) g_ptr_array_add(data,gtk_tree_row_reference_new(model,path));return FALSE; }
void insight_folder_tree_cancel(GtkTreeView *view)
{
 FolderTree *t=tree(view);if(!t) return;GHashTableIter iter;gpointer value;g_hash_table_iter_init(&iter,t->pending);while(g_hash_table_iter_next(&iter,NULL,&value)) g_cancellable_cancel(value);g_hash_table_remove_all(t->pending);
 GPtrArray *rows=g_ptr_array_new_with_free_func((GDestroyNotify)gtk_tree_row_reference_free);gtk_tree_model_foreach(GTK_TREE_MODEL(t->store),cancelled_row,rows);gboolean setting=t->setting;t->setting=TRUE;
 for(guint i=0;i<rows->len;i++) { GtkTreePath *path=gtk_tree_row_reference_get_path(g_ptr_array_index(rows,i));GtkTreeIter row;if(path && gtk_tree_model_get_iter(GTK_TREE_MODEL(t->store),&row,path)) { gtk_tree_store_set(t->store,&row,INSIGHT_FOLDER_STATE,INSIGHT_FOLDER_UNKNOWN,INSIGHT_FOLDER_TOOLTIP,insight_tr("Folder read cancelled"),-1);placeholder(t,&row,"Folder read cancelled; expand or refresh to retry"); }if(path) gtk_tree_path_free(path); }g_ptr_array_unref(rows);t->setting=setting;
}
static void reset(FolderTree *t) { insight_folder_tree_cancel(t->view);gtk_tree_store_clear(t->store); }
void insight_folder_tree_current(GtkTreeView *view,const gchar *path,gboolean hidden,gboolean refresh)
{
 FolderTree *t=tree(view);if(!t || !t->alive || !path) return;gboolean setting=t->setting;t->setting=TRUE;
 if(hidden!=t->hidden) reset(t);
 t->hidden=hidden;g_free(t->current);t->current=g_strdup(path);
 GPtrArray *ancestors=insight_folder_ancestry(path,64);GtkTreeIter parent;gboolean have=FALSE;
 if(ancestors) {
  GtkTreeIter first;if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(t->store),&first)) { gchar *old;gtk_tree_model_get(GTK_TREE_MODEL(t->store),&first,INSIGHT_FOLDER_PATH,&old,-1);if(!same(old,g_ptr_array_index(ancestors,0))) reset(t);g_free(old); }
  if(count(t)+ancestors->len>TREE_LIMIT) reset(t);
  for(guint i=0;i<ancestors->len;i++) { const gchar *p=g_ptr_array_index(ancestors,i);GtkTreeIter iter;if(!child(t,have ? &parent : NULL,p,&iter)) { gchar *name=i ? g_path_get_basename(p) : g_strdup(p);append(t,have ? &parent : NULL,&iter,name,p);g_free(name); }parent=iter;have=TRUE; }
  g_ptr_array_unref(ancestors);
 }
 if(have) { GtkTreePath *row=gtk_tree_model_get_path(GTK_TREE_MODEL(t->store),&parent);gtk_tree_view_expand_to_path(view,row);gtk_tree_view_expand_row(view,row,FALSE);gtk_tree_selection_select_iter(gtk_tree_view_get_selection(view),&parent);gtk_tree_path_free(row);if(refresh) request(t,&parent,MAX(FOLDER_PAGE,(guint)gtk_tree_model_iter_n_children(GTK_TREE_MODEL(t->store),&parent))); }
 t->setting=setting;
}
void insight_folder_tree_action(GtkTreeView *view,InsightFolderAction action)
{
 FolderTree *t=tree(view);if(!t || !t->alive) return;
 if(action==INSIGHT_FOLDER_RESET) { gchar *current=g_strdup(t->current);reset(t);insight_folder_tree_current(view,current,t->hidden,TRUE);g_free(current);return; }
 GtkTreeIter iter;if(!selected(t,&iter)) return;GtkTreePath *row=gtk_tree_model_get_path(GTK_TREE_MODEL(t->store),&iter);
 if(action==INSIGHT_FOLDER_EXPAND) { gtk_tree_view_expand_row(view,row,FALSE);gint state;gtk_tree_model_get(GTK_TREE_MODEL(t->store),&iter,INSIGHT_FOLDER_STATE,&state,-1);if(state==INSIGHT_FOLDER_UNKNOWN || state==INSIGHT_FOLDER_ERROR) request(t,&iter,FOLDER_PAGE); }
 else if(action==INSIGHT_FOLDER_COLLAPSE) gtk_tree_view_collapse_row(view,row);
 else if(action==INSIGHT_FOLDER_CANCEL) { gchar *path;gtk_tree_model_get(GTK_TREE_MODEL(t->store),&iter,INSIGHT_FOLDER_PATH,&path,-1);GCancellable *cancel=g_hash_table_lookup(t->pending,path);if(cancel) { g_cancellable_cancel(cancel);g_hash_table_remove(t->pending,path);gtk_tree_store_set(t->store,&iter,INSIGHT_FOLDER_STATE,INSIGHT_FOLDER_UNKNOWN,INSIGHT_FOLDER_TOOLTIP,insight_tr("Folder read cancelled"),-1);placeholder(t,&iter,"Folder read cancelled; expand or refresh to retry"); }g_free(path); }
 else { guint loaded=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(t->store),&iter),limit=MAX(FOLDER_PAGE,loaded);if(action==INSIGHT_FOLDER_MORE) limit=MIN(TREE_LIMIT,loaded+FOLDER_PAGE);request(t,&iter,limit);gtk_tree_view_expand_row(view,row,FALSE); }
 gtk_tree_path_free(row);
}
static void activated(GtkTreeView *view,GtkTreePath *row,GtkTreeViewColumn *column,gpointer data)
{ (void)column;(void)data;FolderTree *t=tree(view);GtkTreeIter iter;if(!t || !gtk_tree_model_get_iter(GTK_TREE_MODEL(t->store),&iter,row)) return;gchar *path;gtk_tree_model_get(GTK_TREE_MODEL(t->store),&iter,INSIGHT_FOLDER_PATH,&path,-1);if(!path) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(view),&iter);insight_folder_tree_action(view,INSIGHT_FOLDER_MORE); }g_free(path); }
void insight_folder_tree_shutdown(GtkTreeView *view) { FolderTree *t=tree(view);if(t) { t->alive=FALSE;insight_folder_tree_cancel(view); } }
static void destroyed(GtkWidget *widget,gpointer data) { (void)data;insight_folder_tree_shutdown(GTK_TREE_VIEW(widget)); }
static void free_tree(gpointer data) { FolderTree *t=data;g_hash_table_unref(t->pending);g_object_unref(t->store);g_free(t->current);g_free(t); }
static void render_name(GtkTreeViewColumn *column,GtkCellRenderer *cell,GtkTreeModel *model,GtkTreeIter *iter,gpointer data)
{ (void)column;(void)data;gchar *name,*path;gtk_tree_model_get(model,iter,INSIGHT_FOLDER_NAME,&name,INSIGHT_FOLDER_PATH,&path,-1);g_object_set(cell,"text",path ? name : insight_tr(name ? name : ""),NULL);g_free(name);g_free(path); }
void insight_folder_tree_install(GtkTreeView *view,GtkTreeStore *store)
{
 FolderTree *t=g_new0(FolderTree,1);t->view=view;t->store=g_object_ref(store);t->alive=TRUE;t->pending=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_object_unref);g_object_set_data_full(G_OBJECT(view),"insight-folder-tree",t,free_tree);
 gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(store),sort_rows,NULL,NULL);gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,GTK_SORT_ASCENDING);
 gtk_tree_view_set_tooltip_column(view,INSIGHT_FOLDER_TOOLTIP);g_signal_connect(view,"row-expanded",G_CALLBACK(expanded),t);g_signal_connect(view,"row-activated",G_CALLBACK(activated),t);g_signal_connect(view,"destroy",G_CALLBACK(destroyed),t);
 GtkTreeViewColumn *column=gtk_tree_view_get_column(view,0);if(column) { GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));for(GList *p=cells;p;p=p->next) if(GTK_IS_CELL_RENDERER_TEXT(p->data)) gtk_tree_view_column_set_cell_data_func(column,p->data,render_name,NULL,NULL);g_list_free(cells); }
}
