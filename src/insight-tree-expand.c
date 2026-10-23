/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-tree-expand.h"
typedef struct { GtkTreeRowReference *row; guint depth; } Pending;
static void pending_free(gpointer data)
{
 Pending *p=data;gtk_tree_row_reference_free(p->row);g_free(p);
}
gboolean insight_tree_expand_levels(GtkTreeView *view,GtkTreePath *path,guint levels)
{
 g_return_val_if_fail(GTK_IS_TREE_VIEW(view) && path,FALSE);
 GtkTreeModel *model=gtk_tree_view_get_model(view);GtkTreeIter iter;
 if(!model || !levels || levels>32 || !gtk_tree_model_get_iter(model,&iter,path)) return FALSE;
 g_object_ref(view);g_object_ref(model);
 GPtrArray *queue=g_ptr_array_new_with_free_func(pending_free);
 Pending *first=g_new0(Pending,1);first->row=gtk_tree_row_reference_new(model,path);g_ptr_array_add(queue,first);
 gboolean complete=TRUE;
 for(guint i=0;i<queue->len;i++) {
  Pending *p=g_ptr_array_index(queue,i);
  GtkTreePath *current=gtk_tree_row_reference_get_path(p->row);
  if(!current || gtk_widget_in_destruction(GTK_WIDGET(view)) || gtk_tree_view_get_model(view)!=model) { if(current) gtk_tree_path_free(current);complete=FALSE;break; }
  gtk_tree_view_expand_row(view,current,FALSE);gtk_tree_path_free(current);
  /* Expansion signals can replace the model or remove the row. Reacquire it. */
  current=gtk_tree_row_reference_get_path(p->row);
  if(!current || gtk_tree_view_get_model(view)!=model || !gtk_tree_model_get_iter(model,&iter,current)) { if(current) gtk_tree_path_free(current);complete=FALSE;break; }
  gtk_tree_path_free(current);
  if(p->depth+1>=levels) continue;
  GtkTreeIter child;
  for(gboolean valid=gtk_tree_model_iter_children(model,&child,&iter);valid;valid=gtk_tree_model_iter_next(model,&child)) {
   if(queue->len>=4096) { complete=FALSE;break; }
   GtkTreePath *next=gtk_tree_model_get_path(model,&child);Pending *entry=g_new0(Pending,1);
   entry->row=gtk_tree_row_reference_new(model,next);entry->depth=p->depth+1;
   gtk_tree_path_free(next);g_ptr_array_add(queue,entry);
  }
 }
 g_ptr_array_unref(queue);g_object_unref(model);g_object_unref(view);return complete;
}
