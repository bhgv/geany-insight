#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-relation-tree.h"
#include "insight-languages.h"
#include "insight-symbol-view.h"
#include "tagmanager/tm_tag.h"
#include <string.h>
#include <stdint.h>
enum { DISPLAY,PATH,LINE,START,END,ID,DIGEST,NAME,SNIPPET,COLUMN,OWNER,STATE,KEY,COLUMNS };
enum { UNLOADED,LOADING,LOADED,NOTICE,CYCLE };
#define TREE_LIMIT 2000
#define BRANCH_LIMIT 200
#define DEPTH_LIMIT 32
typedef struct {
 GtkWidget *box,*status;GtkTreeView *view;GtkTreeStore *store;GtkTreeModelFilter *filter;
 InsightIndex *index;GCancellable *cancel;GHashTable *expanded;
 gchar *root,*source,*needle;const gchar *detail;GeanyFiletype *filetype;InsightRelationMode mode;
 guint generation,nodes;gboolean setting;
} RelationTree;
typedef struct {
 GWeakRef box;GtkTreeRowReference *row;InsightIndex *index;InsightLanguageFilter *language;
 gchar *owner;InsightRelationMode mode;guint generation;const InsightSymbol *found[BRANCH_LIMIT];size_t total;int limited,ambiguous;
} BranchJob;
static gchar *canonical(const gchar *raw) {
 GString *s=g_string_new(NULL);while(g_str_has_prefix(raw,"::")) raw+=2;
 for(;*raw;raw++) { if(raw[0]==':' && raw[1]==':') { g_string_append_c(s,'.');raw++; }else g_string_append_c(s,*raw); }return g_string_free(s,FALSE);
}
static gchar *symbol_owner(const InsightSymbol *symbol) { gchar *raw=*symbol->scope ? g_strconcat(symbol->scope,".",symbol->name,NULL) : g_strdup(symbol->name),*owner=canonical(raw);g_free(raw);return owner; }
static void status(RelationTree *tree,const gchar *detail) { tree->detail=detail;gchar *text=insight_i18n_printf("Relation tree: %u / %u nodes",tree->nodes,TREE_LIMIT);gchar *label=detail ? g_strconcat(text," • ",insight_tr(detail),NULL) : g_strdup(text);gtk_label_set_text(GTK_LABEL(tree->status),label);g_free(text);g_free(label); }
static gboolean visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 RelationTree *tree=data;if(!tree->needle || !*tree->needle) return TRUE;
 GtkTreeIter parent;if(!gtk_tree_model_iter_parent(model,&parent,iter)) return TRUE;
 gchar *name=NULL,*path=NULL,*snippet=NULL;gtk_tree_model_get(model,iter,NAME,&name,PATH,&path,SNIPPET,&snippet,-1);
 gchar *raw=g_strjoin(" ",name ? name : "",path ? path : "",snippet ? snippet : "",NULL),*folded=g_utf8_casefold(raw,-1);gboolean match=strstr(folded,tree->needle)!=NULL;
 g_free(raw);g_free(folded);g_free(name);g_free(path);g_free(snippet);
 if(!match) { GtkTreeIter child;for(gboolean valid=gtk_tree_model_iter_children(model,&child,iter);valid && !match;valid=gtk_tree_model_iter_next(model,&child)) match=visible(model,&child,data); }
 return match;
}
static void notice(RelationTree *tree,GtkTreeIter *parent,const gchar *text) { GtkTreeIter child;gtk_tree_store_append(tree->store,&child,parent);gtk_tree_store_set(tree->store,&child,NAME,insight_tr(text),STATE,NOTICE,-1); }
static void clear_children(RelationTree *tree,GtkTreeIter *parent) { GtkTreeIter child;while(gtk_tree_model_iter_children(GTK_TREE_MODEL(tree->store),&child,parent)) gtk_tree_store_remove(tree->store,&child); }
static int eligible(const InsightSymbol *symbol,void *data) { return insight_language_filter_match(data,symbol->path); }
static void branch_free(gpointer data) { BranchJob *job=data;g_weak_ref_clear(&job->box);gtk_tree_row_reference_free(job->row);insight_index_free(job->index);insight_language_filter_free(job->language);g_free(job->owner);g_free(job); }
static void branch_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;BranchJob *job=data;job->total=SIZE_MAX;
 if(!g_cancellable_is_cancelled(cancel)) job->total=insight_relation_query_transitive(job->index,job->owner,job->mode,job->mode==INSIGHT_RELATION_MEMBERS ? 0 : 1,eligible,job->language,0,job->found,G_N_ELEMENTS(job->found),&job->limited,&job->ambiguous);
 g_task_return_boolean(task,!g_cancellable_is_cancelled(cancel));
}
static gboolean cycle(RelationTree *tree,GtkTreeIter *parent,const gchar *owner) {
 GtkTreeIter current=*parent;
 do { gchar *ancestor=NULL;gtk_tree_model_get(GTK_TREE_MODEL(tree->store),&current,OWNER,&ancestor,-1);
  const gchar *tail=strrchr(owner,'.');gboolean same=ancestor && (!strcmp(owner,ancestor) || (!strchr(ancestor,'.') && !strcmp(ancestor,tail ? tail+1 : owner)));g_free(ancestor);if(same) return TRUE;
  GtkTreeIter next;if(!gtk_tree_model_iter_parent(GTK_TREE_MODEL(tree->store),&next,&current)) break;current=next;
 }while(TRUE);return FALSE;
}
static void restore_branch(RelationTree *tree,GtkTreeIter *iter) {
 gchar *key=NULL;gtk_tree_model_get(GTK_TREE_MODEL(tree->store),iter,KEY,&key,-1);
 if(key && g_hash_table_contains(tree->expanded,key)) { GtkTreePath *child=gtk_tree_model_get_path(GTK_TREE_MODEL(tree->store),iter),*path=gtk_tree_model_filter_convert_child_path_to_path(tree->filter,child);if(path) { gtk_tree_view_expand_row(tree->view,path,FALSE);gtk_tree_path_free(path); }gtk_tree_path_free(child); }g_free(key);
}
static void branch_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source;(void)data;GTask *task=G_TASK(result);BranchJob *job=g_task_get_task_data(task);gboolean ok=g_task_propagate_boolean(task,NULL);
 GtkWidget *box=g_weak_ref_get(&job->box);if(!box) return;RelationTree *tree=g_object_get_data(G_OBJECT(box),"insight-relation-tree");
 GtkTreePath *path=gtk_tree_row_reference_get_path(job->row);GtkTreeIter parent;
 if(!tree || !ok || tree->generation!=job->generation || !path || !gtk_tree_model_get_iter(GTK_TREE_MODEL(tree->store),&parent,path)) { if(path) gtk_tree_path_free(path);g_object_unref(box);return; }
 GtkTreePath *shown=gtk_tree_model_filter_convert_child_path_to_path(tree->filter,path);gboolean was_expanded=shown && gtk_tree_view_row_expanded(tree->view,shown);if(shown) gtk_tree_path_free(shown);
 guint depth=gtk_tree_path_get_depth(path);gtk_tree_path_free(path);tree->setting=TRUE;clear_children(tree,&parent);gtk_tree_store_set(tree->store,&parent,STATE,LOADED,-1);
 if(job->total==SIZE_MAX) notice(tree,&parent,"Relation query could not be computed");
 else {
  gchar *parent_key=NULL;gtk_tree_model_get(GTK_TREE_MODEL(tree->store),&parent,KEY,&parent_key,-1);
  for(size_t i=0;i<MIN(job->total,G_N_ELEMENTS(job->found)) && tree->nodes<TREE_LIMIT;i++) {
   const InsightSymbol *symbol=job->found[i];gchar *owner=symbol_owner(symbol),*key=g_strdup_printf("%s/%" G_GSIZE_FORMAT ":%s",parent_key,strlen(owner),owner);
   gboolean repeated=cycle(tree,&parent,owner);guint type=insight_symbol_kind_type(symbol->path,symbol->kind,NULL);gboolean branch=(type&(tm_tag_class_t|tm_tag_struct_t|tm_tag_interface_t|tm_tag_union_t))!=0;
   gchar *description=g_strdup_printf("%s • %s%s%s",symbol->kind,symbol->scope,repeated ? " • " : "",repeated ? insight_tr("Cycle: branch stopped") : "");GtkTreeIter child;
   gtk_tree_store_append(tree->store,&child,&parent);gtk_tree_store_set(tree->store,&child,DISPLAY,symbol->path,PATH,symbol->path,LINE,symbol->line,NAME,symbol->name,SNIPPET,description,OWNER,owner,KEY,key,STATE,repeated ? CYCLE : UNLOADED,-1);tree->nodes++;
   if(branch && !repeated) { if(depth>=DEPTH_LIMIT) { gtk_tree_store_set(tree->store,&child,STATE,LOADED,-1);notice(tree,&child,"Relation tree depth limit reached"); }else notice(tree,&child,"Expand to load relations…"); }
   g_free(owner);g_free(key);g_free(description);
  }
  g_free(parent_key);
  if(job->total>BRANCH_LIMIT) notice(tree,&parent,"Branch result limit reached (200)");
  if(tree->nodes>=TREE_LIMIT) notice(tree,&parent,"Relation tree node limit reached");
  if(job->ambiguous) notice(tree,&parent,"Ambiguous class names omitted");
  if(!job->total) notice(tree,&parent,"No indexed relations");
 }
 tree->setting=FALSE;if(was_expanded) restore_branch(tree,&parent);status(tree,job->ambiguous ? "Ambiguous class names omitted" : NULL);
 GtkTreeIter child;for(gboolean valid=gtk_tree_model_iter_children(GTK_TREE_MODEL(tree->store),&child,&parent);valid;valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(tree->store),&child)) restore_branch(tree,&child);
 g_object_unref(box);
}
static void expanded(GtkTreeView *view,GtkTreeIter *filtered,GtkTreePath *path,gpointer data) {
 (void)view;(void)path;RelationTree *tree=data;if(tree->setting || !tree->index || !tree->box) return;
 GtkTreeIter iter;gtk_tree_model_filter_convert_iter_to_child_iter(tree->filter,&iter,filtered);gchar *key=NULL,*owner=NULL;gint state=LOADED;gtk_tree_model_get(GTK_TREE_MODEL(tree->store),&iter,KEY,&key,OWNER,&owner,STATE,&state,-1);
 if(key) { if(g_hash_table_size(tree->expanded)>=4096) g_hash_table_remove_all(tree->expanded);g_hash_table_add(tree->expanded,g_strdup(key)); }
 if(state==UNLOADED && owner && *owner) {
  gtk_tree_store_set(tree->store,&iter,STATE,LOADING,-1);GtkTreeIter placeholder;if(gtk_tree_model_iter_children(GTK_TREE_MODEL(tree->store),&placeholder,&iter)) gtk_tree_store_set(tree->store,&placeholder,NAME,insight_tr("Loading relations…"),-1);else notice(tree,&iter,"Loading relations…");
  BranchJob *job=g_new0(BranchJob,1);g_weak_ref_init(&job->box,tree->box);job->index=insight_index_ref(tree->index);job->owner=g_strdup(owner);job->mode=tree->mode;job->generation=tree->generation;job->language=insight_language_filter_new(tree->source,tree->filetype);
  GtkTreePath *child_path=gtk_tree_model_get_path(GTK_TREE_MODEL(tree->store),&iter);job->row=gtk_tree_row_reference_new(GTK_TREE_MODEL(tree->store),child_path);gtk_tree_path_free(child_path);
  GTask *task=g_task_new(NULL,tree->cancel,branch_done,NULL);g_task_set_task_data(task,job,branch_free);g_task_run_in_thread(task,branch_run);g_object_unref(task);
 }
 if(state==LOADED) { GtkTreeIter child;for(gboolean valid=gtk_tree_model_iter_children(GTK_TREE_MODEL(tree->store),&child,&iter);valid;valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(tree->store),&child)) restore_branch(tree,&child); }
 g_free(key);g_free(owner);
}
static void collapsed(GtkTreeView *view,GtkTreeIter *filtered,GtkTreePath *path,gpointer data) { (void)view;(void)path;RelationTree *tree=data;if(tree->setting) return;gchar *key=NULL;gtk_tree_model_get(GTK_TREE_MODEL(tree->filter),filtered,KEY,&key,-1);if(key) g_hash_table_remove(tree->expanded,key);g_free(key); }
static void freed(gpointer data) { RelationTree *tree=data;g_cancellable_cancel(tree->cancel);g_object_unref(tree->cancel);insight_index_free(tree->index);g_object_unref(tree->filter);g_object_unref(tree->store);g_hash_table_unref(tree->expanded);g_free(tree->root);g_free(tree->source);g_free(tree->needle);g_free(tree); }
static void line_cell(GtkTreeViewColumn *column,GtkCellRenderer *renderer,GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)column;(void)data;gulong line=0;gtk_tree_model_get(model,iter,LINE,&line,-1);gchar *text=line ? g_strdup_printf("%lu",line) : g_strdup("");g_object_set(renderer,"text",text,NULL);g_free(text);
}
static void box_destroyed(GtkWidget *box,gpointer data) { RelationTree *tree=data;g_object_set_data(G_OBJECT(box),"insight-relation-tree",NULL);tree->box=NULL;tree->generation++;g_cancellable_cancel(tree->cancel); }
GtkWidget *insight_relation_tree_new(void) {
 RelationTree *tree=g_new0(RelationTree,1);tree->box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);tree->cancel=g_cancellable_new();tree->expanded=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 tree->store=gtk_tree_store_new(COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING,G_TYPE_INT,G_TYPE_STRING);
 tree->filter=GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(tree->store),NULL));gtk_tree_model_filter_set_visible_func(tree->filter,visible,tree,NULL);
 tree->view=GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(tree->filter)));gtk_widget_set_name(GTK_WIDGET(tree->view),"insight-relations-tree");
 const gchar *titles[]={"Symbol","File","Line"};const gint fields[]={NAME,DISPLAY,LINE};
 for(guint i=0;i<G_N_ELEMENTS(fields);i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(insight_tr(titles[i]),gtk_cell_renderer_text_new(),"text",fields[i],NULL);gtk_tree_view_column_set_resizable(column,TRUE);if(fields[i]==LINE) { GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));if(cells) gtk_tree_view_column_set_cell_data_func(column,cells->data,line_cell,NULL,NULL);g_list_free(cells); }gtk_tree_view_append_column(tree->view,column); }
 gtk_tree_view_set_tooltip_column(tree->view,SNIPPET);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),GTK_WIDGET(tree->view));gtk_box_pack_start(GTK_BOX(tree->box),scroll,TRUE,TRUE,0);
 tree->status=gtk_label_new(NULL);gtk_widget_set_name(tree->status,"insight-relation-tree-status");gtk_label_set_line_wrap(GTK_LABEL(tree->status),TRUE);gtk_box_pack_start(GTK_BOX(tree->box),tree->status,FALSE,FALSE,0);
 g_signal_connect(tree->view,"row-expanded",G_CALLBACK(expanded),tree);g_signal_connect(tree->view,"row-collapsed",G_CALLBACK(collapsed),tree);g_object_set_data(G_OBJECT(tree->box),"insight-relation-tree",tree);g_object_set_data_full(G_OBJECT(tree->view),"insight-relation-tree",tree,freed);g_signal_connect(tree->box,"destroy",G_CALLBACK(box_destroyed),tree);status(tree,NULL);return tree->box;
}
GtkTreeView *insight_relation_tree_view(GtkWidget *box) { RelationTree *tree=g_object_get_data(G_OBJECT(box),"insight-relation-tree");return tree ? tree->view : NULL; }
void insight_relation_tree_query(GtkWidget *box,InsightIndex *index,const gchar *owner,InsightRelationMode mode,const gchar *source,GeanyFiletype *ft) {
 RelationTree *tree=g_object_get_data(G_OBJECT(box),"insight-relation-tree");if(!tree || !index || !owner) return;
 g_cancellable_cancel(tree->cancel);g_clear_object(&tree->cancel);tree->cancel=g_cancellable_new();tree->generation++;tree->setting=TRUE;gtk_tree_store_clear(tree->store);tree->nodes=1;insight_index_free(tree->index);tree->index=insight_index_ref(index);tree->mode=mode;
 g_free(tree->root);tree->root=canonical(owner);g_free(tree->source);tree->source=g_strdup(source);tree->filetype=ft;
 GtkTreeIter root;gtk_tree_store_append(tree->store,&root,NULL);gchar *key=g_strdup_printf("%d:%" G_GSIZE_FORMAT ":%s",mode,strlen(tree->root),tree->root);gtk_tree_store_set(tree->store,&root,NAME,owner,OWNER,tree->root,KEY,key,STATE,UNLOADED,-1);g_free(key);notice(tree,&root,"Expand to load relations…");tree->setting=FALSE;
 GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_expand_row(tree->view,path,FALSE);gtk_tree_path_free(path);gtk_widget_set_sensitive(GTK_WIDGET(tree->view),TRUE);status(tree,NULL);
}
static gboolean restore_filtered(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) { (void)model;(void)path;restore_branch(data,iter);return FALSE; }
void insight_relation_tree_filter(GtkWidget *box,const gchar *query) { RelationTree *tree=g_object_get_data(G_OBJECT(box),"insight-relation-tree");if(!tree) return;g_free(tree->needle);tree->needle=g_utf8_casefold(query ? query : "",-1);gtk_tree_model_filter_refilter(tree->filter);gtk_tree_model_foreach(GTK_TREE_MODEL(tree->store),restore_filtered,tree); }

static gboolean reset_loading(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 (void)path;RelationTree *tree=data;gint state=LOADED;gtk_tree_model_get(model,iter,STATE,&state,-1);
 if(state==LOADING) { gtk_tree_store_set(tree->store,iter,STATE,UNLOADED,-1);GtkTreeIter child;if(gtk_tree_model_iter_children(model,&child,iter)) gtk_tree_store_set(tree->store,&child,NAME,insight_tr("Expand to load relations…"),-1); }
 return FALSE;
}
void insight_relation_tree_cancel(GtkWidget *box) {
 RelationTree *tree=g_object_get_data(G_OBJECT(box),"insight-relation-tree");if(!tree) return;
 g_cancellable_cancel(tree->cancel);g_clear_object(&tree->cancel);tree->cancel=g_cancellable_new();tree->generation++;tree->setting=TRUE;
 gtk_tree_model_foreach(GTK_TREE_MODEL(tree->store),reset_loading,tree);tree->setting=FALSE;status(tree,"Relation tree loading cancelled");
}

static gboolean translate_notice(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 (void)path;RelationTree *tree=data;gint state=LOADED;gtk_tree_model_get(model,iter,STATE,&state,-1);
 if(state==NOTICE) { gchar *name=NULL;gtk_tree_model_get(model,iter,NAME,&name,-1);gtk_tree_store_set(tree->store,iter,NAME,insight_tr(name),-1);g_free(name); }
 else if(state==CYCLE) { gchar *text=NULL;gtk_tree_model_get(model,iter,SNIPPET,&text,-1);gchar *last=text ? g_strrstr(text," • ") : NULL;if(last) { *last=0;gchar *translated=g_strconcat(text," • ",insight_tr("Cycle: branch stopped"),NULL);gtk_tree_store_set(tree->store,iter,SNIPPET,translated,-1);g_free(translated); }g_free(text); }
 return FALSE;
}
void insight_relation_tree_locale_changed(GtkWidget *box) {
 RelationTree *tree=g_object_get_data(G_OBJECT(box),"insight-relation-tree");if(!tree) return;
 gtk_tree_model_foreach(GTK_TREE_MODEL(tree->store),translate_notice,tree);status(tree,tree->detail);
}
