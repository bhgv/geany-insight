/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-i18n.h"
#include "insight-search-links.h"
#include "../insight/desktop/source_link_anchor.h"
#include "document.h"
#include "sciwrappers.h"
#include "navqueue.h"
#include "ui_utils.h"
#include "utils.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
typedef struct {
 GtkTreeRowReference *row;
 guint doc;
 InsightSourceLinkAnchor anchor;
} SearchLink;
struct InsightSearchLinks {
 GtkTreeView *view;
 GtkListStore *model;
 InsightSearchLinkColumns c;
 GPtrArray *links;
 SearchLink *last;
 void (*highlight)(ScintillaObject *,gsize,gsize);
 guint digest_doc;
 gchar *digest;
};
static void link_free(gpointer data) {
 SearchLink *link=data;gtk_tree_row_reference_free(link->row);g_free(link);
}
static void prune(InsightSearchLinks *s) {
 for(guint i=s->links->len;i>0;i--) {
  SearchLink *link=g_ptr_array_index(s->links,i-1);
  if(!gtk_tree_row_reference_valid(link->row)) {
   if(s->last==link) s->last=NULL;
   g_ptr_array_remove_index(s->links,i-1);
  }
 }
}
static SearchLink *lookup(InsightSearchLinks *s,GtkTreePath *path) {
 for(guint i=0;i<s->links->len;i++) {
  SearchLink *link=g_ptr_array_index(s->links,i);
  GtkTreePath *row=gtk_tree_row_reference_get_path(link->row);
  gboolean equal=row && !gtk_tree_path_compare(row,path);
  if(row) gtk_tree_path_free(row);
  if(equal) return link;
 }
 return NULL;
}
static const gchar *digest(InsightSearchLinks *s,GeanyDocument *doc) {
 if(s->digest_doc!=doc->id || !s->digest) {
  g_clear_pointer(&s->digest,g_free);s->digest_doc=doc->id;
  gint length=sci_get_length(doc->editor->sci);
  if(length<0 || length>2*1024*1024) return NULL;
  gchar *text=sci_get_contents(doc->editor->sci,length+1);
  if(text && !memchr(text,0,length) && g_utf8_validate(text,length,NULL))
   s->digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)text,length);
  g_free(text);
 }
 return s->digest;
}
static void bounds(SearchLink *link,ScintillaObject *sci) {
 gint length=sci_get_length(sci);
 if(length<0 || link->anchor.start>(gsize)length || link->anchor.end>(gsize)length) { link->anchor.valid=FALSE;return; }
 gint line=sci_get_line_from_position(sci,(gint)link->anchor.start);
 link->anchor.line_start=sci_get_position_from_line(sci,line);
 link->anchor.line_end=line+1<sci_get_line_count(sci) ? (gsize)sci_get_position_from_line(sci,line+1) : (gsize)length;
}
static gboolean row_iter(InsightSearchLinks *s,SearchLink *link,GtkTreeIter *iter) {
 GtkTreePath *path=gtk_tree_row_reference_get_path(link->row);
 gboolean valid=path && gtk_tree_model_get_iter(GTK_TREE_MODEL(s->model),iter,path);
 if(path) gtk_tree_path_free(path);
 return valid;
}
static void update_row(InsightSearchLinks *s,SearchLink *link,GeanyDocument *doc,gboolean full) {
 GtkTreeIter iter;if(!link->anchor.valid || !row_iter(s,link,&iter)) return;
 ScintillaObject *sci=doc->editor->sci;gint line=sci_get_line_from_position(sci,(gint)link->anchor.start);
 gulong column=(gulong)SEND(sci,SCI_COUNTCHARACTERS,sci_get_position_from_line(sci,line),link->anchor.start)+1;
 gtk_list_store_set(s->model,&iter,s->c.line,(gulong)line+1,s->c.column,column,
  s->c.start,(gulong)link->anchor.start,s->c.end,(gulong)link->anchor.end,-1);
 if(full) gtk_list_store_set(s->model,&iter,s->c.id,doc->id,s->c.path,doc->file_name,
  s->c.display,doc->file_name ? doc->file_name : "untitled",s->c.digest,digest(s,doc),-1);
}
static void link_icon(GtkTreeViewColumn *column,GtkCellRenderer *cell,GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)column;InsightSearchLinks *s=data;GtkTreePath *path=gtk_tree_model_get_path(model,iter);
 SearchLink *link=lookup(s,path);gtk_tree_path_free(path);
 g_object_set(cell,"icon-name",!link ? "insert-link-symbolic" : link->anchor.valid ? "emblem-default-symbolic" : "dialog-warning-symbolic",NULL);
}
InsightSearchLinks *insight_search_links_new(GtkTreeView *view,InsightSearchLinkColumns columns) {
 InsightSearchLinks *s=g_new0(InsightSearchLinks,1);s->view=view;s->model=GTK_LIST_STORE(g_object_ref(gtk_tree_view_get_model(view)));s->c=columns;
 s->links=g_ptr_array_new_with_free_func(link_free);
 GtkCellRenderer *cell=gtk_cell_renderer_pixbuf_new();GtkTreeViewColumn *column=gtk_tree_view_column_new();
 gtk_tree_view_column_set_title(column,"Link");gtk_tree_view_column_pack_start(column,cell,FALSE);
 gtk_tree_view_column_set_cell_data_func(column,cell,link_icon,s,NULL);gtk_tree_view_insert_column(view,column,0);
 return s;
}
void insight_search_links_set_highlight(InsightSearchLinks *s,void (*paint)(ScintillaObject *,gsize,gsize)) { if(s) s->highlight=paint; }
void insight_search_links_clear(InsightSearchLinks *s) {
 if(!s) return;
 /* Coordinates and digest must describe the same snapshot when anchors are
  * released. Otherwise undo after closing Results could validate old text
  * against offsets already shifted by a newer edit. */
 prune(s);
 for(guint i=0;i<s->links->len;i++) {
  SearchLink *link=g_ptr_array_index(s->links,i);GeanyDocument *doc=document_find_by_id(link->doc);
  if(doc && doc->is_valid) update_row(s,link,doc,TRUE);
 }
 s->last=NULL;g_ptr_array_set_size(s->links,0);s->digest_doc=0;g_clear_pointer(&s->digest,g_free);
 gtk_widget_queue_draw(GTK_WIDGET(s->view));
}
void insight_search_links_free(InsightSearchLinks *s) {
 if(!s) return;
 /* The view can outlive the controller during editor shutdown. */
 GtkTreeViewColumn *column=gtk_tree_view_get_column(s->view,0);
 if(column) gtk_tree_view_remove_column(s->view,column);
 g_ptr_array_unref(s->links);g_object_unref(s->model);g_free(s->digest);g_free(s);
}
void insight_search_links_capture(InsightSearchLinks *s,GtkTreeIter *iter) {
 if(!s) return;prune(s);GtkTreePath *path=gtk_tree_model_get_path(GTK_TREE_MODEL(s->model),iter);
 if(lookup(s,path)) { gtk_tree_path_free(path);return; }
 guint id=0;gulong start=0,end=0;gchar *expected=NULL;
 gtk_tree_model_get(GTK_TREE_MODEL(s->model),iter,s->c.id,&id,s->c.start,&start,s->c.end,&end,s->c.digest,&expected,-1);
 GeanyDocument *doc=id ? document_find_by_id(id) : NULL;
 if(doc && doc->is_valid && expected && !g_strcmp0(expected,digest(s,doc)) && start<=end && end<=(gulong)sci_get_length(doc->editor->sci)) {
  SearchLink *link=g_new0(SearchLink,1);link->doc=id;link->row=gtk_tree_row_reference_new(GTK_TREE_MODEL(s->model),path);
  link->anchor=(InsightSourceLinkAnchor){start,end,0,0,TRUE};bounds(link,doc->editor->sci);g_ptr_array_add(s->links,link);
 }
 g_free(expected);gtk_tree_path_free(path);
}
void insight_search_links_refresh(InsightSearchLinks *s,GtkTreePath *path) {
 if(!s || !path) return;prune(s);SearchLink *link=lookup(s,path);
 if(!link || !link->anchor.valid) return;
 GeanyDocument *doc=document_find_by_id(link->doc);
 if(doc && doc->is_valid) update_row(s,link,doc,TRUE);
}
gboolean insight_search_links_open(InsightSearchLinks *s,GtkTreePath *path) {
 if(!s || !path) return FALSE;prune(s);GtkTreeIter iter;
 if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(s->model),&iter,path)) return TRUE;
 SearchLink *link=lookup(s,path);
 if(link && !link->anchor.valid) { ui_set_statusbar(TRUE,"%s",insight_tr("Source link line was deleted; run the search again"));return TRUE; }
 if(link && link->doc && !document_find_by_id(link->doc)) link->doc=0;
 if(!link || !link->doc) {
  /* Opening a file may run nested dialogs/signals. Keep a row reference, not
   * an iterator or controller-owned link pointer, across that operation. */
  GtkTreeRowReference *origin=gtk_tree_row_reference_new(GTK_TREE_MODEL(s->model),path);
  guint id=0;gchar *file=NULL,*expected=NULL;gtk_tree_model_get(GTK_TREE_MODEL(s->model),&iter,s->c.id,&id,s->c.path,&file,s->c.digest,&expected,-1);
  GeanyDocument *doc=id ? document_find_by_id(id) : NULL;
  if(!doc && file) doc=document_find_by_filename(file);
  if(!doc && file) { gchar *native=utils_get_locale_from_utf8(file);doc=document_open_file(native,FALSE,NULL,NULL);g_free(native); }
  gboolean unchanged=doc && doc->is_valid && expected && !g_strcmp0(expected,digest(s,doc));g_free(file);g_free(expected);
  GtkTreePath *current=gtk_tree_row_reference_get_path(origin);gtk_tree_row_reference_free(origin);
  if(!current) return TRUE;
  if(!unchanged) { gtk_tree_path_free(current);ui_set_statusbar(TRUE,"%s",insight_tr("Source link snapshot changed or is unavailable; run the search again"));return TRUE; }
  if(!gtk_tree_model_get_iter(GTK_TREE_MODEL(s->model),&iter,current)) { gtk_tree_path_free(current);return TRUE; }
  link=lookup(s,current);
  if(link && !link->anchor.valid) { gtk_tree_path_free(current);return TRUE; }
  gtk_list_store_set(s->model,&iter,s->c.id,doc->id,-1);
  if(link) { link->doc=doc->id;bounds(link,doc->editor->sci); }
  else { insight_search_links_capture(s,&iter);link=lookup(s,current); }
  gtk_tree_path_free(current);
 }
 if(!link || !link->anchor.valid) return TRUE;
 GeanyDocument *doc=document_find_by_id(link->doc);if(!doc || !doc->is_valid) return TRUE;
 update_row(s,link,doc,TRUE);s->last=link;
 navqueue_goto_line(document_get_current(),doc,sci_get_line_from_position(doc->editor->sci,link->anchor.start)+1);
 SEND(doc->editor->sci,SCI_SETSEL,link->anchor.start,link->anchor.end);
 if(s->highlight) s->highlight(doc->editor->sci,link->anchor.start,link->anchor.end);
 SEND(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0);document_grab_focus(doc);
 ui_set_statusbar(TRUE,"%s",insight_tr("Source link opened; Ctrl+L returns to the search result"));return TRUE;
}
gboolean insight_search_links_return(InsightSearchLinks *s,GeanyDocument *doc) {
 if(!s || !doc || !doc->is_valid) return FALSE;prune(s);gint line=sci_get_current_line(doc->editor->sci);SearchLink *found=NULL;
 if(s->last && s->last->anchor.valid && s->last->doc==doc->id && sci_get_line_from_position(doc->editor->sci,s->last->anchor.start)==line) found=s->last;
 for(guint i=0;!found && i<s->links->len;i++) { SearchLink *link=g_ptr_array_index(s->links,i);if(link->anchor.valid && link->doc==doc->id && sci_get_line_from_position(doc->editor->sci,link->anchor.start)==line) found=link; }
 if(!found) return FALSE;
 GtkTreePath *path=gtk_tree_row_reference_get_path(found->row);if(!path) return FALSE;
 s->last=found;gtk_tree_view_set_cursor(s->view,path,NULL,FALSE);gtk_tree_view_scroll_to_cell(s->view,path,NULL,TRUE,0.5,0);gtk_widget_grab_focus(GTK_WIDGET(s->view));gtk_tree_path_free(path);return TRUE;
}
void insight_search_links_notify(InsightSearchLinks *s,GeanyEditor *editor,SCNotification *nt) {
 if(!s || nt->nmhdr.code!=SCN_MODIFIED || !(nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) return;
 s->digest_doc=0;g_clear_pointer(&s->digest,g_free);prune(s);
 for(guint i=0;i<s->links->len;i++) {
  SearchLink *link=g_ptr_array_index(s->links,i);if(link->doc!=editor->document->id || !link->anchor.valid) continue;
  if(nt->position<0 || nt->length<0) { link->anchor.valid=FALSE;continue; }
  insight_source_link_anchor_edit(&link->anchor,nt->position,nt->modificationType&SC_MOD_DELETETEXT ? nt->length : 0,nt->modificationType&SC_MOD_INSERTTEXT ? nt->length : 0);
  if(link->anchor.valid) { bounds(link,editor->sci);update_row(s,link,editor->document,FALSE); }
 }
 gtk_widget_queue_draw(GTK_WIDGET(s->view));
}
void insight_search_links_closed(InsightSearchLinks *s,GeanyDocument *doc) {
 if(!s || !doc || !doc->is_valid) return;
 for(guint i=0;i<s->links->len;i++) {
  SearchLink *link=g_ptr_array_index(s->links,i);if(link->doc!=doc->id) continue;
  update_row(s,link,doc,TRUE);link->doc=0;
 }
 s->digest_doc=0;g_clear_pointer(&s->digest,g_free);
}
