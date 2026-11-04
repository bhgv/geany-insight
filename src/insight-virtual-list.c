/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-virtual-list.h"
#include <gdk/gdkkeysyms.h>
#include <string.h>
#define WINDOW_ROWS 256u
#define CACHE_ROWS 1024u
#define CACHE_PAGES 3u
#define HYSTERESIS 128u
#define PREFETCH_BEFORE 256u
typedef struct {
 gint refs;guint count,columns;GType *types;
 InsightVirtualRead read;gpointer data;GDestroyNotify destroy;
} Provider;
typedef struct { GValue *values;gchar *key; } CachedRow;
typedef struct { gint refs;Provider *provider;guint first;GPtrArray *rows; } Page;
typedef struct { GtkTreeIter iter;gboolean used; } ExistingRow;
typedef struct { ExistingRow *existing;CachedRow *row; } DesiredRow;
typedef struct {
 GtkTreeView *view;GtkListStore *store;GtkWidget *scroll,*bar;GtkAdjustment *position;
 Provider *provider;GQueue pages;GCancellable *cancel;
 guint count,first,requested,debounce,apply,pending_first,pending_end;
 guint64 request_id,load_id,dataset,rendered_dataset;
 gboolean updating,destroyed,selection_blocked;
 InsightVirtualReload reload;gpointer reload_data;
} VirtualList;
typedef struct { GWeakRef view;Provider *provider;Page *page;guint64 load_id;guint first,length; } ReadJob;
typedef struct { GWeakRef view;Page *page;GArray *existing,*desired;guint position,first;guint64 request_id,dataset; } ApplyJob;
static GThreadPool *read_pool;
static VirtualList *state(GtkTreeView *view) {return g_object_get_data(G_OBJECT(view),"insight-virtual-list");}
static Provider *provider_ref(Provider *p) {if(p)g_atomic_int_inc(&p->refs);return p;}
static void provider_destroy(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;(void)cancel;Provider *p=data;if(p->destroy)p->destroy(p->data);g_free(p->types);g_free(p);g_task_return_boolean(task,TRUE);
}
static void provider_unref(Provider *p) {
 if(!p||!g_atomic_int_dec_and_test(&p->refs))return;
 GTask *task=g_task_new(NULL,NULL,NULL,NULL);g_task_set_task_data(task,p,NULL);g_task_run_in_thread(task,provider_destroy);g_object_unref(task);
}
static Page *page_ref(Page *p) {if(p)g_atomic_int_inc(&p->refs);return p;}
static void row_free(CachedRow *row,guint columns) {for(guint n=0;n<columns;n++)g_value_unset(&row->values[n]);g_free(row->values);g_free(row->key);g_free(row);}
static void page_unref(Page *p) {
 if(!p||!g_atomic_int_dec_and_test(&p->refs))return;
 for(guint n=0;n<p->rows->len;n++)row_free(g_ptr_array_index(p->rows,n),p->provider->columns);
 g_ptr_array_unref(p->rows);provider_unref(p->provider);g_free(p);
}
static Page *page_new(Provider *p,guint first) {Page *page=g_new0(Page,1);page->refs=1;page->provider=provider_ref(p);page->first=first;page->rows=g_ptr_array_new();return page;}
static gchar *value_identity(GValue *values,guint columns) {
 GString *key=g_string_new(NULL);gboolean strings=FALSE;
 for(guint n=0;n<columns;n++)if(G_VALUE_HOLDS_STRING(&values[n])){const gchar *v=g_value_get_string(&values[n]);strings=TRUE;g_string_append_printf(key,"%" G_GSIZE_FORMAT ":%s",v?strlen(v):0,v?v:"");}
 if(!strings&&columns){gchar *v=g_strdup_value_contents(values);g_string_append(key,v);g_free(v);}return g_string_free(key,FALSE);
}
static gchar *model_identity(GtkTreeModel *model,GtkTreeIter *iter) {
 guint count=gtk_tree_model_get_n_columns(model);GValue *values=g_new0(GValue,count);
 for(guint n=0;n<count;n++)gtk_tree_model_get_value(model,iter,n,&values[n]);
 gchar *key=value_identity(values,count);for(guint n=0;n<count;n++)g_value_unset(&values[n]);g_free(values);return key;
}
static guint visible_rows(VirtualList *s) {
 gint height=0;
 GtkTreeIter first;
 GtkTreeModel *model=gtk_tree_view_get_model(s->view);
 if(gtk_widget_get_realized(GTK_WIDGET(s->view)) && model &&
    gtk_tree_model_get_iter_first(model,&first)) {
  GtkTreePath *path=gtk_tree_model_get_path(model,&first);
  GdkRectangle row={0};
  gtk_tree_view_get_background_area(s->view,path,NULL,&row);
  gtk_tree_path_free(path);
  height=row.height;
 }
 /* Before rows are laid out, fall back to the renderer requisition. */
 if(height<=0) {
  height=22;
  GList *columns=gtk_tree_view_get_columns(s->view);
  if(columns) {
   GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(columns->data));
   if(cells) {
    gint minimum=0,natural=0;
    gtk_cell_renderer_get_preferred_height(cells->data,GTK_WIDGET(s->view),&minimum,&natural);
    height=MAX(height,natural+2);
   }
   g_list_free(cells);
  }
  g_list_free(columns);
 }
 GdkRectangle visible={0};if(gtk_widget_get_realized(GTK_WIDGET(s->view))) gtk_tree_view_get_visible_rect(s->view,&visible);
 gint available=visible.height>0?visible.height:gtk_widget_get_allocated_height(GTK_WIDGET(s->view));
 /* A conservative page size keeps the final row visible despite theme padding. */
 return CLAMP(available/height-1,1,(gint)(WINDOW_ROWS/2));
}static void busy(VirtualList *s) {g_object_set_data(G_OBJECT(s->view),"insight-virtual-loading",GINT_TO_POINTER(s->cancel||s->debounce||s->apply));}
static void range(VirtualList *s) {
 guint page=visible_rows(s);s->updating=TRUE;
 gtk_adjustment_configure(s->position,s->requested,0,MAX(s->count,page),1,MAX(1,page-1),page);
 gtk_widget_set_sensitive(s->bar,s->count>page);s->updating=FALSE;
}
static void unblock_selection(VirtualList *s) {
 if(!s->selection_blocked)return;s->selection_blocked=FALSE;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(s->view);
 g_signal_handlers_unblock_matched(selection,G_SIGNAL_MATCH_ID,g_signal_lookup("changed",GTK_TYPE_TREE_SELECTION),0,NULL,NULL,NULL);
}
static void cancel_apply(VirtualList *s) {if(s->apply){g_source_remove(s->apply);s->apply=0;}unblock_selection(s);}
static void clear_pages(VirtualList *s) {while(!g_queue_is_empty(&s->pages))page_unref(g_queue_pop_head(&s->pages));}
static gboolean covers(Page *page,guint first,guint length) {return first>=page->first&&(guint64)first+length<=(guint64)page->first+page->rows->len;}
static Page *cached(VirtualList *s,guint first) {
 guint length=MIN(WINDOW_ROWS,s->count-first);
 for(GList *link=s->pages.head;link;link=link->next){Page *page=link->data;if(covers(page,first,length)){g_queue_unlink(&s->pages,link);g_queue_push_head_link(&s->pages,link);return page;}}return NULL;
}
static void queue_free(gpointer data) {g_queue_free(data);}
static void apply_free(gpointer data) {ApplyJob *j=data;g_weak_ref_clear(&j->view);page_unref(j->page);g_array_unref(j->existing);g_array_unref(j->desired);g_free(j);}
static gboolean apply_rows(gpointer data) {
 ApplyJob *j=data;GtkTreeView *view=g_weak_ref_get(&j->view);if(!view)return G_SOURCE_REMOVE;
 VirtualList *s=state(view);
 if(!s||s->destroyed||j->request_id!=s->request_id||j->dataset!=s->dataset){g_object_unref(view);return G_SOURCE_REMOVE;}
 gint64 deadline=g_get_monotonic_time()+3000;gint column;GtkSortType order;
 gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(s->store),&column,&order);
 gboolean sorted=column!=GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;
 while(j->position<j->desired->len){
  guint n=j->position++;DesiredRow *row=&g_array_index(j->desired,DesiredRow,n);GtkTreeIter iter;
  if(row->existing)iter=row->existing->iter;else gtk_list_store_append(s->store,&iter);
  for(guint c=0;c<j->page->provider->columns;c++)gtk_list_store_set_value(s->store,&iter,c,&row->row->values[c]);
  /* An unsorted provider must retain its global order when scrolling backward. */
  if(!sorted){GtkTreePath *path=gtk_tree_model_get_path(GTK_TREE_MODEL(s->store),&iter);gint at=gtk_tree_path_get_indices(path)[0];gtk_tree_path_free(path);
   if(at!=(gint)n){GtkTreeIter previous;if(n&&gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(s->store),&previous,NULL,n-1))gtk_list_store_move_after(s->store,&iter,&previous);else gtk_list_store_move_after(s->store,&iter,NULL);}}
  if(g_get_monotonic_time()>=deadline){g_object_unref(view);return G_SOURCE_CONTINUE;}
 }
 s->first=j->first;s->rendered_dataset=j->dataset;s->apply=0;unblock_selection(s);
 gtk_widget_queue_resize(GTK_WIDGET(view));
 gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(s->scroll)),0);
 g_object_set_data(G_OBJECT(view),"insight-virtual-window-rows",GUINT_TO_POINTER(j->desired->len));busy(s);g_object_unref(view);return G_SOURCE_REMOVE;
}
static void apply_page(VirtualList *s,Page *page,guint first) {
 cancel_apply(s);s->rendered_dataset=0;ApplyJob *j=g_new0(ApplyJob,1);g_weak_ref_init(&j->view,s->view);j->page=page_ref(page);j->first=first;j->request_id=s->request_id;j->dataset=s->dataset;
 j->existing=g_array_sized_new(FALSE,FALSE,sizeof(ExistingRow),WINDOW_ROWS);j->desired=g_array_new(FALSE,FALSE,sizeof(DesiredRow));
 GtkTreeIter iter;for(gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(s->store),&iter);valid;valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(s->store),&iter)){ExistingRow row={iter,FALSE};g_array_append_val(j->existing,row);}
 GHashTable *keys=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,queue_free);
 for(guint n=0;n<j->existing->len;n++){ExistingRow *row=&g_array_index(j->existing,ExistingRow,n);gchar *key=model_identity(GTK_TREE_MODEL(s->store),&row->iter);GQueue *q=g_hash_table_lookup(keys,key);if(!q){q=g_queue_new();g_hash_table_insert(keys,key,q);}else g_free(key);g_queue_push_tail(q,row);}
 for(guint n=first;n<s->count&&n-first<WINDOW_ROWS;n++){CachedRow *value=g_ptr_array_index(page->rows,n-page->first);GQueue *q=g_hash_table_lookup(keys,value->key);DesiredRow row={q?g_queue_pop_head(q):NULL,value};if(row.existing)row.existing->used=TRUE;g_array_append_val(j->desired,row);}
 GtkTreeSelection *selection=gtk_tree_view_get_selection(s->view);g_signal_handlers_block_matched(selection,G_SIGNAL_MATCH_ID,g_signal_lookup("changed",GTK_TYPE_TREE_SELECTION),0,NULL,NULL,NULL);s->selection_blocked=TRUE;
 for(guint n=0;n<j->existing->len;n++){ExistingRow *row=&g_array_index(j->existing,ExistingRow,n);if(!row->used)gtk_list_store_remove(s->store,&row->iter);}
 g_hash_table_unref(keys);s->apply=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,apply_rows,j,apply_free);busy(s);
}
static gboolean dispatch(gpointer);
static void read_job_free(gpointer data) {ReadJob *j=data;g_weak_ref_clear(&j->view);page_unref(j->page);provider_unref(j->provider);g_free(j);}
/* Dedicated two-worker pool: parsing/indexing cannot occupy the page-read queue. */
static void read_page(gpointer data,gpointer unused) {
 (void)unused;GTask *task=data;ReadJob *j=g_task_get_task_data(task);GCancellable *cancel=g_task_get_cancellable(task);gboolean ok=TRUE;
 j->page=page_new(j->provider,j->first);
 for(guint n=0;n<j->length;n++){
  if(g_cancellable_is_cancelled(cancel)){ok=FALSE;break;}
  CachedRow *row=g_new0(CachedRow,1);row->values=g_new0(GValue,j->provider->columns);
  for(guint c=0;c<j->provider->columns;c++)g_value_init(&row->values[c],j->provider->types[c]);
  if(!j->provider->read(j->first+n,row->values,j->provider->data,cancel)||g_cancellable_is_cancelled(cancel)){row_free(row,j->provider->columns);ok=FALSE;break;}
  row->key=value_identity(row->values,j->provider->columns);g_ptr_array_add(j->page->rows,row);
 }
 g_task_return_boolean(task,ok);g_object_unref(task);
}
static void read_done(GObject *source,GAsyncResult *result,gpointer unused) {
 (void)source;(void)unused;GTask *task=G_TASK(result);ReadJob *j=g_task_get_task_data(task);GError *error=NULL;gboolean ok=g_task_propagate_boolean(task,&error);
 GtkTreeView *view=g_weak_ref_get(&j->view);if(!view){g_clear_error(&error);return;}VirtualList *s=state(view);
 if(s&&!s->destroyed&&s->provider==j->provider&&s->load_id==j->load_id){
  g_clear_object(&s->cancel);
  if(ok){g_queue_push_head(&s->pages,page_ref(j->page));while(s->pages.length>CACHE_PAGES)page_unref(g_queue_pop_tail(&s->pages));
   guint rows=0;for(GList *p=s->pages.head;p;p=p->next)rows+=((Page *)p->data)->rows->len;g_object_set_data(G_OBJECT(view),"insight-virtual-cache-rows",GUINT_TO_POINTER(rows));
   if(s->debounce){g_source_remove(s->debounce);s->debounce=0;}dispatch(s);
  }else if(!g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)){s->requested=s->first;range(s);}
  busy(s);
 }
 g_clear_error(&error);g_object_unref(view);
}
static void load_page(VirtualList *s,guint first) {
 guint before=first> PREFETCH_BEFORE?first-PREFETCH_BEFORE:0;
 if(s->cancel){if(first>=s->pending_first&&(guint64)first+MIN(WINDOW_ROWS,s->count-first)<=s->pending_end)return;g_cancellable_cancel(s->cancel);g_clear_object(&s->cancel);}
 ReadJob *j=g_new0(ReadJob,1);g_weak_ref_init(&j->view,s->view);j->provider=provider_ref(s->provider);j->load_id=++s->load_id;j->first=before;j->length=MIN(CACHE_ROWS,s->count-before);
 s->pending_first=before;s->pending_end=before+j->length;s->cancel=g_cancellable_new();
 GTask *task=g_task_new(NULL,s->cancel,read_done,NULL);g_task_set_task_data(task,j,read_job_free);
 GError *error=NULL;if(!read_pool)read_pool=g_thread_pool_new(read_page,NULL,2,FALSE,&error);
 if(error)g_task_return_error(task,error);
 else if(!g_thread_pool_push(read_pool,g_object_ref(task),&error)){g_object_unref(task);g_task_return_error(task,error);}
 g_object_unref(task);busy(s);
}
static gboolean dispatch(gpointer data) {
 VirtualList *s=data;s->debounce=0;if(s->destroyed||!s->provider)return G_SOURCE_REMOVE;
 guint first=s->requested;
 if(!s->count){Page *empty=page_new(s->provider,0);apply_page(s,empty,0);page_unref(empty);return G_SOURCE_REMOVE;}
 Page *page=cached(s,first);
 if(!page){load_page(s,first);return G_SOURCE_REMOVE;}
 if(!s->apply&&(s->first!=first||s->rendered_dataset!=s->dataset||!gtk_tree_model_iter_n_children(GTK_TREE_MODEL(s->store),NULL)))apply_page(s,page,first);
 guint end=page->first+page->rows->len,wanted_end=first+MIN(WINDOW_ROWS,s->count-first);
 if((page->first&&first-page->first<HYSTERESIS)||(end<s->count&&end-wanted_end<HYSTERESIS))load_page(s,first);
 busy(s);return G_SOURCE_REMOVE;
}
static void render(VirtualList *s,guint first) {
 if(s->updating||s->destroyed)return;
 guint page=visible_rows(s);first=MIN(first,s->count>page?s->count-page:0);
 s->requested=first;s->request_id++;cancel_apply(s);
 if(s->debounce)g_source_remove(s->debounce);s->debounce=g_timeout_add(16,dispatch,s);range(s);busy(s);
}
static void changed(GtkAdjustment *adjustment,gpointer data) {VirtualList *s=data;if(!s->updating)render(s,(guint)gtk_adjustment_get_value(adjustment));}
static gboolean wheel(GtkWidget *widget,GdkEventScroll *event,gpointer data) {
 (void)widget;VirtualList *s=data;gdouble delta=0;
 /* Let the existing panel font zoom handle Ctrl+wheel. */
 if(event->state&GDK_CONTROL_MASK) return FALSE;
 if(event->direction==GDK_SCROLL_DOWN) delta=3;
 else if(event->direction==GDK_SCROLL_UP) delta=-3;
 else if(event->direction==GDK_SCROLL_SMOOTH) delta=event->delta_y*3;
 if(!delta) return FALSE;
 gtk_adjustment_set_value(s->position,gtk_adjustment_get_value(s->position)+delta);return TRUE;
}
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;VirtualList *s=data;gdouble next=s->requested;guint page=visible_rows(s);
 if((event->keyval==GDK_KEY_Down || event->keyval==GDK_KEY_Up) &&
    gtk_tree_view_get_model(s->view)==GTK_TREE_MODEL(s->store) && !(event->state&(GDK_CONTROL_MASK|GDK_SHIFT_MASK))) {
  if(s->rendered_dataset!=s->dataset || s->apply || s->requested!=s->first) return FALSE;
  GtkTreeIter iter;GtkTreeModel *model=NULL;gint64 absolute=s->first;
  if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(s->view),&model,&iter)) {
   GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gint *indices=gtk_tree_path_get_indices(path);
   if(indices) absolute+=indices[0]+(event->keyval==GDK_KEY_Down?1:-1);gtk_tree_path_free(path);
  }
  if(!s->count) return TRUE;
  absolute=CLAMP(absolute,0,(gint64)s->count-1);
  if(absolute<s->first) gtk_adjustment_set_value(s->position,absolute);
  else if(absolute>=s->first+page) gtk_adjustment_set_value(s->position,absolute-page+1);
  GtkTreePath *path=gtk_tree_path_new_from_indices((gint)(absolute-s->first),-1);
  gtk_tree_view_set_cursor(s->view,path,NULL,FALSE);gtk_tree_path_free(path);return TRUE;
 }
 switch(event->keyval) {
 case GDK_KEY_Home:next=0;break;
 case GDK_KEY_End:next=s->count;break;
 case GDK_KEY_Page_Down:next+=MAX(1,page-1);break;
 case GDK_KEY_Page_Up:next-=MAX(1,page-1);break;
 default:return FALSE;
 }
 gtk_adjustment_set_value(s->position,next);return TRUE;
}
static void allocated(GtkWidget *widget,GtkAllocation *allocation,gpointer data) {
 (void)widget;(void)allocation;VirtualList *s=data;if(s->updating||s->destroyed)return;
 guint page=visible_rows(s),first=MIN(s->requested,s->count>page?s->count-page:0);
 if(first!=s->requested)render(s,first);else range(s);
}
static void sort_changed(GtkTreeSortable *store,gpointer data) {(void)store;VirtualList *s=state(GTK_TREE_VIEW(data));if(s&&!s->updating&&s->reload)s->reload(s->view,s->reload_data);}
static void closed(GtkWidget *widget,gpointer data) {
 (void)widget;VirtualList *s=data;s->destroyed=TRUE;s->request_id++;s->load_id++;
 if(s->debounce){g_source_remove(s->debounce);s->debounce=0;}cancel_apply(s);
 if(s->cancel){g_cancellable_cancel(s->cancel);g_clear_object(&s->cancel);}clear_pages(s);provider_unref(s->provider);s->provider=NULL;
}
static void freed(gpointer data) {VirtualList *s=data;if(!s->destroyed)closed(NULL,s);g_signal_handlers_disconnect_by_data(s->position,s);g_object_unref(s->store);g_object_unref(s->position);g_free(s);}
GtkWidget *insight_virtual_list_wrap(GtkWidget *scroll,GtkTreeView *view,GtkListStore *store,InsightVirtualReload reload,gpointer data) {
 VirtualList *s=g_new0(VirtualList,1);s->view=view;s->store=g_object_ref(store);s->scroll=scroll;s->reload=reload;s->reload_data=data;
 s->position=GTK_ADJUSTMENT(g_object_ref_sink(gtk_adjustment_new(0,0,1,1,10,1)));
 s->bar=gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL,s->position);gtk_widget_set_name(s->bar,"insight-virtual-scrollbar");
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(box),s->bar,FALSE,FALSE,0);
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_NEVER);
 g_object_set_data_full(G_OBJECT(view),"insight-virtual-list",s,freed);
 g_signal_connect(s->position,"value-changed",G_CALLBACK(changed),s);g_signal_connect(view,"scroll-event",G_CALLBACK(wheel),s);g_signal_connect(view,"key-press-event",G_CALLBACK(key),s);g_signal_connect(view,"size-allocate",G_CALLBACK(allocated),s);g_signal_connect(view,"destroy",G_CALLBACK(closed),s);
 g_signal_connect_object(store,"sort-column-changed",G_CALLBACK(sort_changed),view,0);return box;
}
gboolean insight_virtual_list_attached(GtkTreeView *view) {return state(view)!=NULL;}
void insight_virtual_list_replace(GtkTreeView *view,guint count,InsightVirtualRead read,gpointer data,GDestroyNotify destroy) {
 VirtualList *s=state(view);if(!s||s->destroyed){if(destroy)destroy(data);return;}
 s->dataset++;s->load_id++;if(s->cancel){g_cancellable_cancel(s->cancel);g_clear_object(&s->cancel);}clear_pages(s);provider_unref(s->provider);
 Provider *p=g_new0(Provider,1);p->refs=1;p->count=count;p->columns=gtk_tree_model_get_n_columns(GTK_TREE_MODEL(s->store));p->types=g_new(GType,p->columns);for(guint n=0;n<p->columns;n++)p->types[n]=gtk_tree_model_get_column_type(GTK_TREE_MODEL(s->store),n);p->read=read;p->data=data;p->destroy=destroy;s->provider=p;s->count=count;render(s,s->requested);
}
void insight_virtual_list_clear(GtkTreeView *view) {insight_virtual_list_replace(view,0,NULL,NULL,NULL);}
void insight_virtual_list_reload(GtkTreeView *view) {VirtualList *s=state(view);if(s&&s->reload&&!s->updating&&!s->destroyed)s->reload(view,s->reload_data);}
void insight_virtual_list_position(GtkTreeView *view,guint first) {VirtualList *s=state(view);if(s&&!s->destroyed)gtk_adjustment_set_value(s->position,first);}
guint insight_virtual_list_count(GtkTreeView *view) {VirtualList *s=state(view);return s?s->count:0;}
guint insight_virtual_list_first(GtkTreeView *view) {VirtualList *s=state(view);return s?s->first:0;}
gboolean insight_virtual_list_loading(GtkTreeView *view) {VirtualList *s=state(view);return s&&(s->cancel||s->debounce||s->apply);}
