/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Symbol search jobs and list controls.
 * RU: Задачи поиска символов и управление списками. */

static void symbol_list_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; SymbolListJob *j=data; InsightLookupFilter *filter=insight_lookup_filter_new(j->query,j->paths!=NULL,NULL);
 if(j->paths) {
  j->files=g_ptr_array_new_with_free_func(file_list_row_free); GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
  for(guint n=0;filter && n<j->paths->len && !g_cancellable_is_cancelled(cancel);n++) {
   const char *path=g_ptr_array_index(j->paths,n);if(!insight_language_filter_match(j->language,path)) continue; if(!g_utf8_validate(path,-1,NULL) || g_hash_table_contains(seen,path)) continue; g_hash_table_add(seen,(gpointer)path);
   if(insight_lookup_filter_match(filter,NULL,NULL,path,NULL)) { FileListRow *r=g_new0(FileListRow,1); r->path=g_strdup(path); r->name=g_path_get_basename(path); r->directory=g_path_get_dirname(path); gchar *native=utils_get_locale_from_utf8(path); GStatBuf st; r->stat_valid=native && g_stat(native,&st)==0 && S_ISREG(st.st_mode); if(r->stat_valid) r->size=(guint64)st.st_size; g_free(native); g_ptr_array_add(j->files,r); j->count++; }
  }
  g_hash_table_destroy(seen);
 } else {
  if(j->local && !g_cancellable_is_cancelled(cancel)) { j->index=insight_index_new(); TMSourceFile captured={.tags_array=j->tags,.lang=j->lang}; if(!insight_native_outline_import(j->index,&captured,j->path ? j->path : "@untitled")) { insight_lookup_filter_free(filter); g_task_return_boolean(task,FALSE); return; } }
  if(j->outlines) { j->index=insight_index_new();for(guint n=0;n<j->outlines->len && !g_cancellable_is_cancelled(cancel);n++) { OutlineFile *o=g_ptr_array_index(j->outlines,n);TMSourceFile file={.tags_array=o->tags,.lang=o->lang};if(!insight_native_outline_import(j->index,&file,o->path)) { insight_lookup_filter_free(filter);g_task_return_boolean(task,FALSE);return; } } }
  j->rows=g_new(const InsightSymbol *,MAX((gsize)1,insight_index_count(j->index)));
  if(filter) for(gsize n=0;n<insight_index_count(j->index);n++) {
   if(g_cancellable_is_cancelled(cancel)) break;
   const InsightSymbol *tag=insight_index_at(j->index,n);if(!insight_language_filter_match(j->language,tag->path)) continue; if(j->path && g_strcmp0(j->path,tag->path)) continue;
   if(insight_lookup_filter_match(filter,tag->name,tag->scope,tag->path,g_str_has_prefix(tag->kind,"@decl:") ? strrchr(tag->kind,':')+1 : tag->kind)) { j->total++;if(insight_symbol_kind_matches(tag->kind,j->type_mask)) j->rows[j->count++]=tag; }
  }
 }
 if(!g_cancellable_is_cancelled(cancel) && j->count>1) {
  if(j->files) g_ptr_array_sort_with_data(j->files,virtual_compare,j);
  else g_qsort_with_data(j->rows,j->count,sizeof(*j->rows),virtual_compare,j);
 }
 insight_lookup_filter_free(filter); g_task_return_boolean(task,TRUE);
}
typedef struct { InsightIndex *index;const InsightSymbol **rows;GPtrArray *files;gchar *path;gboolean local; } VirtualSnapshot;
static void virtual_snapshot_free(gpointer data) {
 VirtualSnapshot *s=data;retire_index(s->index);g_free(s->rows);g_clear_pointer(&s->files,g_ptr_array_unref);g_free(s->path);g_free(s);
}
static gboolean virtual_snapshot_read(guint n,GValue *values,gpointer data,GCancellable *cancel) {
 if(g_cancellable_is_cancelled(cancel)) return FALSE;
 VirtualSnapshot *s=data;
 if(s->files) {FileListRow *f=g_ptr_array_index(s->files,n);g_value_set_string(&values[0],f->name);g_value_set_string(&values[1],f->path);g_value_set_string(&values[2],f->directory);g_value_set_uint64(&values[3],f->size);g_value_set_boolean(&values[4],f->stat_valid);}
 else {const InsightSymbol *t=s->rows[n];g_value_set_string(&values[SYMBOL_NAME],t->name);g_value_set_string(&values[SYMBOL_SCOPE],t->scope);g_value_set_string(&values[SYMBOL_PATH],s->local?s->path:t->path);g_value_set_ulong(&values[SYMBOL_LINE],(gulong)t->line);g_value_set_string(&values[SYMBOL_KIND],t->kind);}
 return TRUE;
}
static void virtual_reload(GtkTreeView *view,gpointer data) {
 (void)data;if(GTK_WIDGET(view)==wb.file_view) {g_object_set_data(G_OBJECT(wb.file_store),"insight-request-key",NULL);files_filter(NULL,NULL);}
 else if(GTK_WIDGET(view)==wb.symbol_view) browser_filter(NULL,NULL);
 else if(GTK_WIDGET(view)==wb.local_view) local_filter(NULL,NULL);
}
static void symbol_list_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); SymbolListJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL); GtkListStore *store=g_weak_ref_get(&j->store); if(!store) return;
 SymbolListState *s=symbol_list_state(store);
 if(wb.alive && j->epoch==workbench_epoch && j->generation==s->generation) {
  g_clear_object(&s->cancel); s->has_more=ok && j->has_more;
  GtkWidget *virtual_view=symbol_list_view(store);
  if(ok && insight_virtual_list_attached(GTK_TREE_VIEW(virtual_view))) {
   VirtualSnapshot *snapshot=g_new0(VirtualSnapshot,1);snapshot->index=j->index;j->index=NULL;
   snapshot->rows=j->rows;j->rows=NULL;snapshot->files=j->files;j->files=NULL;snapshot->local=j->local;snapshot->path=g_strdup(j->path);
   g_object_set_data(G_OBJECT(virtual_view),"insight-virtual-total",GUINT_TO_POINTER(snapshot->files?j->count:j->total));
   insight_virtual_list_replace(GTK_TREE_VIEW(virtual_view),j->count,virtual_snapshot_read,snapshot,virtual_snapshot_free);
   insight_symbol_view_page_state(GTK_TREE_VIEW(virtual_view),FALSE);
   gtk_widget_set_sensitive(virtual_view,TRUE);insight_symbol_view_loading(GTK_TREE_VIEW(virtual_view),FALSE);
   if(store==wb.file_store && wb.file_status) {
    gchar *text=insight_i18n_printf("Files: %u",j->count);gtk_label_set_text(GTK_LABEL(wb.file_status),text);g_free(text);
    g_object_set_data(G_OBJECT(wb.file_status),"insight-files-count",GUINT_TO_POINTER(j->count));
    g_object_set_data(G_OBJECT(wb.file_status),"insight-files-more",NULL);
   }
   g_object_unref(store);return;
  }
  if(ok) { s->index=insight_index_ref(j->index); s->rows=j->rows; j->rows=NULL; s->files=j->files; j->files=NULL; s->count=j->count; s->position=0; s->local=j->local; s->local_path=g_strdup(j->path); }
  else { if(j->paths) s->files=g_ptr_array_new_with_free_func(file_list_row_free); ui_set_statusbar(TRUE,"Symbol snapshot unavailable or exceeds its limit"); }
  s->existing=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,symbol_iter_queue_free);
  s->collecting=TRUE; s->iter_valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&s->iter);
  GtkWidget *view=symbol_list_view(store); if(view) { gtk_widget_set_sensitive(view,FALSE); insight_symbol_view_loading(GTK_TREE_VIEW(view),TRUE); }
  if(!s->apply) s->apply=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,symbol_list_apply,g_object_ref(store),g_object_unref);
 }
 g_object_unref(store);
}
static SymbolListJob *symbol_list_begin(GtkListStore *store,GtkWidget *entry,guint limit,gboolean files) {
 SymbolListState *s=symbol_list_state(store); symbol_list_stop(s); GtkWidget *view=symbol_list_view(store); if(view) { gtk_widget_set_sensitive(view,TRUE); insight_symbol_view_loading(GTK_TREE_VIEW(view),FALSE); }
 InsightLookupFilter *filter=entry_filter(entry,files); if(!filter) {
  if(view && insight_virtual_list_attached(GTK_TREE_VIEW(view))) {g_object_set_data(G_OBJECT(view),"insight-virtual-total",NULL);insight_virtual_list_clear(GTK_TREE_VIEW(view));}
  s->has_more=FALSE;
  if(view) { gtk_widget_set_sensitive(view,FALSE); insight_symbol_view_loading(GTK_TREE_VIEW(view),TRUE); }
  s->files=files ? g_ptr_array_new_with_free_func(file_list_row_free) : NULL;
  s->existing=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,symbol_iter_queue_free); s->collecting=TRUE;
  s->iter_valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&s->iter);
  s->apply=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,symbol_list_apply,g_object_ref(store),g_object_unref); return NULL;
 } insight_lookup_filter_free(filter);
 SymbolListJob *j=g_new0(SymbolListJob,1); g_weak_ref_init(&j->store,store); j->query=g_strdup(gtk_entry_get_text(GTK_ENTRY(entry))); j->limit=limit;j->type_mask=store==wb.file_store?G_MAXUINT:insight_symbol_view_types_mask(GTK_TREE_VIEW(view));
 j->sort_column=0;j->sort_direction=GTK_SORT_ASCENDING;gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(store),&j->sort_column,&j->sort_direction);
 j->generation=s->generation; j->epoch=workbench_epoch;GeanyDocument *active=document_get_current();if(active && (store==wb.symbols || store==wb.file_store)) j->language=insight_language_filter_new(active->file_name,active->file_type); return j;
}
static void symbol_list_launch(GtkListStore *store,SymbolListJob *j) {
 GtkWidget *view=symbol_list_view(store); if(view) { if(!insight_virtual_list_attached(GTK_TREE_VIEW(view))) gtk_widget_set_sensitive(view,FALSE); insight_symbol_view_loading(GTK_TREE_VIEW(view),TRUE); }
 SymbolListState *s=symbol_list_state(store); s->cancel=g_cancellable_new(); GTask *task=g_task_new(NULL,s->cancel,symbol_list_job_done,NULL); g_task_set_task_data(task,j,symbol_list_job_free); g_task_run_in_thread(task,symbol_list_job_run); g_object_unref(task);
}
static void fill_symbol_list(GtkListStore *store,GtkWidget *entry,const char *path,guint limit) {
 refresh_index(); SymbolListJob *j=symbol_list_begin(store,entry,limit,FALSE); if(!j) return;
 if(store==wb.symbols) {
  j->outlines=g_ptr_array_new_with_free_func(outline_free);GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);gchar *root=wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;
  for(guint n=0;n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(!doc->is_valid || !doc->file_name || (wb.workspace_from_project && root && !insight_project_files_contains(wb.membership,root,doc->file_name))) continue;g_ptr_array_add(j->outlines,outline_capture(doc->tm_file,doc->file_name));g_hash_table_add(seen,doc->file_name); }
  GHashTableIter iter;gpointer key,value;g_hash_table_iter_init(&iter,wb.declarations);while(g_hash_table_iter_next(&iter,&key,&value)) if(!g_hash_table_contains(seen,key)) g_ptr_array_add(j->outlines,outline_copy(value));g_hash_table_unref(seen);g_free(root);
 }else j->index=insight_index_ref(wb.index);j->path=g_strdup(path);symbol_list_launch(store,j);
}
static void browser_filter(GtkEditable *entry,gpointer data) {
 (void)data; if(!wb.symbols) return;
 if(entry) insight_virtual_list_position(GTK_TREE_VIEW(wb.symbol_view),0);
 g_object_set_data_full(G_OBJECT(wb.symbol_view),"insight-project-root",wb.root ? utils_get_utf8_from_locale(wb.root) : NULL,g_free);
 GeanyDocument *doc=document_get_current();gchar *profile=doc ? insight_languages_profile_name(doc->editor->sci,doc->file_name,doc->file_type) : g_strdup("");
 if(entry || !wb.project_symbol_limit || g_strcmp0(wb.project_page_root,wb.root) || g_strcmp0(wb.project_page_profile,profile)) wb.project_symbol_limit=500;
 g_free(wb.project_page_root);wb.project_page_root=g_strdup(wb.root);g_free(wb.project_page_profile);wb.project_page_profile=profile;
 fill_symbol_list(wb.symbols,wb.symbol_entry,NULL,wb.project_symbol_limit);
}
static void browser_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; if(!wb.preview || !gtk_widget_get_sensitive(GTK_WIDGET(gtk_tree_selection_get_tree_view(selection)))) return;
 GtkWidget *view=GTK_WIDGET(gtk_tree_selection_get_tree_view(selection));if((view==wb.local_view || view==wb.symbol_view) && !gtk_widget_has_focus(view)) return;
 GtkTreeIter iter; GtkTreeModel *model;
 if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path,*name; gulong line; gtk_tree_model_get(model,&iter,SYMBOL_PATH,&path,SYMBOL_NAME,&name,SYMBOL_LINE,&line,-1);
 if(!line) { g_free(path); g_free(name); return; }
 reveal_context(); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); if(show_location(path,gtk_tree_selection_get_tree_view(selection)==GTK_TREE_VIEW(wb.local_view) ? wb.local_doc_id : 0,line,TRUE)) preview_mark_symbol(name); g_free(path); g_free(name);
}
static gboolean browser_symbol_clicked(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->button!=1 || !gtk_widget_has_focus(widget)) return FALSE;
 GtkTreePath *row=NULL;if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&row,NULL,NULL,NULL)) { gtk_tree_path_free(row);browser_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)),NULL); }return FALSE;
}
static void browser_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(view);
 gtk_tree_selection_select_path(selection,path);
 if(GTK_WIDGET(view)==wb.symbol_view || GTK_WIDGET(view)==wb.local_view) { GtkTreeModel *model=gtk_tree_view_get_model(view); GtkTreeIter iter; gulong line=0; if(!gtk_tree_model_get_iter(model,&iter,path)) return; gtk_tree_model_get(model,&iter,SYMBOL_LINE,&line,-1); if(!line) { if(gtk_tree_view_row_expanded(view,path)) gtk_tree_view_collapse_row(view,path); else gtk_tree_view_expand_row(view,path,FALSE); return; } }
 if(GTK_WIDGET(view)==wb.local_view || GTK_WIDGET(view)==wb.symbol_view) {
  GtkTreeModel *model=gtk_tree_view_get_model(view);GtkTreeIter iter;gchar *file=NULL,*name=NULL;gulong line=0;
  if(gtk_tree_model_get_iter(model,&iter,path)) { gtk_tree_model_get(model,&iter,SYMBOL_PATH,&file,SYMBOL_NAME,&name,SYMBOL_LINE,&line,-1);GeanyDocument *doc=GTK_WIDGET(view)==wb.local_view ? document_find_by_id(wb.local_doc_id) : NULL;
   if(!doc && file && line) { gchar *native=utils_get_locale_from_utf8(file);doc=document_open_file(native,FALSE,NULL,NULL);g_free(native); }
   if(doc && line) { editor_mark_symbol(doc,line,name);document_grab_focus(doc); }
  }g_free(file);g_free(name);return;
 }
 if(GTK_WIDGET(view)==wb.symbol_view || GTK_WIDGET(view)==wb.local_view || GTK_WIDGET(view)==wb.category_view) browser_selected(selection,NULL);
 else if(GTK_WIDGET(view)==wb.file_view) {
  GtkTreeIter iter; GtkTreeModel *model=gtk_tree_view_get_model(view);
  if(!gtk_tree_model_get_iter(model,&iter,path)) return;
  gchar *filename; gtk_tree_model_get(model,&iter,1,&filename,-1);
  gchar *native=utils_get_locale_from_utf8(filename); GeanyDocument *doc=document_open_file(native,FALSE,NULL,NULL); g_free(native); g_free(filename);
  if(doc) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE); document_grab_focus(doc); insight_workbench_refresh(); }
  return;
 }
 else return;
 open_context(NULL,NULL);
}
static gboolean symbol_query_valid(const gchar *query,gboolean files) {
 if(!query || !*query || strlen(query)>256 || !g_utf8_validate(query,-1,NULL)) return FALSE;
 gchar *trimmed=g_strdup(query);g_strstrip(trimmed);gboolean empty=!*trimmed;g_free(trimmed);if(empty) return FALSE;
 InsightLookupFilter *filter=insight_lookup_filter_new(query,files,NULL);if(!filter) return FALSE;insight_lookup_filter_free(filter);return TRUE;
}
static void symbol_history_record(GtkWidget *entry) {
 GPtrArray *history=g_object_get_data(G_OBJECT(entry),"insight-symbol-history");const gchar *query=gtk_entry_get_text(GTK_ENTRY(entry));if(!history || !symbol_query_valid(query,entry==wb.file_entry)) return;
 insight_search_history_record(history,query);if(history->len>16) g_ptr_array_set_size(history,16);save_workbench_settings();
}
static void symbol_history_remember(GtkMenuItem *item,gpointer data) { (void)item;symbol_history_record(GTK_WIDGET(data)); }
static void symbol_history_pick(GtkMenuItem *item,gpointer data) {
 GtkWidget *entry=data;const gchar *query=g_object_get_data(G_OBJECT(item),"query");gtk_entry_set_text(GTK_ENTRY(entry),query);gtk_widget_grab_focus(entry);
}
static void symbol_history_clear(GtkMenuItem *item,gpointer data) {
 (void)item;GPtrArray *history=g_object_get_data(G_OBJECT(data),"insight-symbol-history");g_ptr_array_set_size(history,0);save_workbench_settings();
}
static GtkWidget *symbol_history_item(const gchar *label,const gchar *icon) {
 GtkWidget *item=gtk_image_menu_item_new_with_label(label);gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),insight_command_icon(icon));gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE);return item;
}
static void symbol_history_show(GtkWidget *menu,gpointer data) {
 GtkWidget *entry=data;GPtrArray *history=g_object_get_data(G_OBJECT(entry),"insight-symbol-history");GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data);g_list_free(children);
 GtkWidget *remember=symbol_history_item(entry==wb.file_entry ? "Remember current file filter" : "Remember current symbol filter","document-save-symbolic");gtk_widget_set_name(remember,"insight-symbol-history-remember");gtk_widget_set_sensitive(remember,symbol_query_valid(gtk_entry_get_text(GTK_ENTRY(entry)),entry==wb.file_entry));g_signal_connect_object(remember,"activate",G_CALLBACK(symbol_history_remember),entry,0);gtk_menu_shell_append(GTK_MENU_SHELL(menu),remember);gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
 for(guint i=0;i<history->len;i++) { const gchar *query=g_ptr_array_index(history,i);gchar *label=g_utf8_substring(query,0,MIN((glong)64,g_utf8_strlen(query,-1)));GtkWidget *item=symbol_history_item(label,"edit-find");g_free(label);gchar *name=g_strdup_printf("insight-symbol-history-%u",i);gtk_widget_set_name(item,name);g_free(name);g_object_set_data(G_OBJECT(item),"insight-no-i18n",GINT_TO_POINTER(1));gtk_widget_set_tooltip_text(item,query);g_object_set_data_full(G_OBJECT(item),"query",g_strdup(query),g_free);g_signal_connect_object(item,"activate",G_CALLBACK(symbol_history_pick),entry,0);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
 GtkWidget *clear=symbol_history_item(history->len ? "Clear history" : "History is empty","edit-clear");gtk_widget_set_name(clear,"insight-symbol-history-clear");gtk_widget_set_sensitive(clear,history->len>0);g_signal_connect_object(clear,"activate",G_CALLBACK(symbol_history_clear),entry,0);if(history->len) gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());gtk_menu_shell_append(GTK_MENU_SHELL(menu),clear);insight_i18n_apply(menu);gtk_widget_show_all(menu);
}
/* A narrow sidebar must shrink text, not allocate controls beyond its edge.
 * Keep native button/arrow/icon requisitions and ellipsize only their labels. */
static void panel_scroll_minimum(GtkWidget *widget) {
 if(GTK_IS_SCROLLED_WINDOW(widget)) gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(widget),48);
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));for(GList *p=children;p;p=p->next) panel_scroll_minimum(p->data);g_list_free(children); }
}
static void compact_panel_control(GtkWidget *widget);
static void compact_button_label(GObject *object,GParamSpec *spec,gpointer data) {
 (void)spec;(void)data;
 /* GtkButton may rebuild its label when its translated caption changes. */
 GtkWidget *label=gtk_bin_get_child(GTK_BIN(object));if(label) compact_panel_control(label);
}
static void compact_panel_control(GtkWidget *widget) {
 if(GTK_IS_BUTTON(widget)) {
  if(!g_object_get_data(G_OBJECT(widget),"insight-compact-label")) {
   g_object_set_data(G_OBJECT(widget),"insight-compact-label",GINT_TO_POINTER(1));
   g_signal_connect(widget,"notify::label",G_CALLBACK(compact_button_label),NULL);
  }
  compact_button_label(G_OBJECT(widget),NULL,NULL);
 }
 if(GTK_IS_SCROLLED_WINDOW(widget)) gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(widget),48);
 if(GTK_IS_ENTRY(widget)) { gtk_style_context_add_class(gtk_widget_get_style_context(widget),"insight-shrink-control");gtk_entry_set_width_chars(GTK_ENTRY(widget),1);gtk_entry_set_max_width_chars(GTK_ENTRY(widget),18); }
 if(GTK_IS_COMBO_BOX(widget)) {
  gtk_style_context_add_class(gtk_widget_get_style_context(widget),"insight-shrink-control");
  GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(widget));for(GList *p=cells;p;p=p->next) if(GTK_IS_CELL_RENDERER_TEXT(p->data)) g_object_set(p->data,"ellipsize",PANGO_ELLIPSIZE_END,"width-chars",1,NULL);g_list_free(cells);
 }
 if(GTK_IS_LABEL(widget)) {
  const gchar *text=gtk_label_get_text(GTK_LABEL(widget));if(text && *text && !gtk_widget_get_has_tooltip(widget)) gtk_widget_set_tooltip_text(widget,text);
  gtk_label_set_ellipsize(GTK_LABEL(widget),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(widget),1);gtk_label_set_max_width_chars(GTK_LABEL(widget),24);
 }
 if(GTK_IS_CONTAINER(widget) && !GTK_IS_COMBO_BOX(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));for(GList *p=children;p;p=p->next) compact_panel_control(p->data);g_list_free(children); }
}
static GtkWidget *symbol_query_controls(GtkWidget *entry,gboolean local) {
 compact_panel_control(entry);
 gtk_widget_set_tooltip_text(entry,"Use ~term for fuzzy search (characters in order); @~path and scope=~owner filter fields");
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*button=gtk_menu_button_new(),*menu=gtk_menu_new();g_object_set_data_full(G_OBJECT(entry),"insight-symbol-history",g_ptr_array_new_with_free_func(g_free),(GDestroyNotify)g_ptr_array_unref);
 gtk_widget_set_name(button,entry==wb.file_entry ? "insight-files-query-history" : (local ? "insight-local-query-history" : "insight-project-query-history"));gtk_button_set_image(GTK_BUTTON(button),insight_command_icon("document-open-recent"));gtk_widget_set_tooltip_text(button,entry==wb.file_entry ? "File filter history: 16 valid queries, up to 256 bytes each" : "Symbol filter history: 16 valid queries, up to 256 bytes each");gtk_button_set_relief(GTK_BUTTON(button),GTK_RELIEF_NONE);gtk_menu_button_set_popup(GTK_MENU_BUTTON(button),menu);g_signal_connect_object(menu,"show",G_CALLBACK(symbol_history_show),entry,0);
 gtk_box_pack_start(GTK_BOX(row),entry,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),button,FALSE,FALSE,0);return row;
}
static void symbol_history_write(GKeyFile *settings,GtkWidget *entry,const gchar *prefix) {
 GPtrArray *history=g_object_get_data(G_OBJECT(entry),"insight-symbol-history");gchar *key=g_strconcat(prefix,"-query-history",NULL);g_key_file_set_string_list(settings,"workbench",key,(const gchar *const *)history->pdata,history->len);g_free(key);
}
static void symbol_history_read(GKeyFile *settings,GtkWidget *entry,const gchar *prefix) {
 gchar *key=g_strconcat(prefix,"-query-history",NULL);gsize count=0;gchar **queries=g_key_file_get_string_list(settings,"workbench",key,&count,NULL);g_free(key);GPtrArray *history=g_object_get_data(G_OBJECT(entry),"insight-symbol-history");
 if(queries && count<=16) for(gsize i=count;i>0;i--) if(symbol_query_valid(queries[i-1],entry==wb.file_entry)) insight_search_history_record(history,queries[i-1]);
 g_strfreev(queries);
}
static gboolean file_browse_command(GtkEntry *entry) {
 const gchar *query=gtk_entry_get_text(entry);if(!wb.disk_browser || (strcmp(query,".") && strcmp(query,"..") && strcmp(query,"**"))) return FALSE;
 gboolean project=!strcmp(query,"**"),up=!strcmp(query,"..");
 gtk_entry_set_text(entry,"");
 if(project) { gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),0);focus_widget_panel(wb.file_entry);return TRUE; }
 gchar *root=wb.root ? utils_get_utf8_from_locale(wb.root) : g_get_current_dir(),*path=up ? g_path_get_dirname(root) : g_strdup(root);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),1);insight_disk_browser_set_root(wb.disk_browser,path);g_free(path);g_free(root);
 focus_widget_panel(wb.disk_browser);gtk_widget_child_focus(wb.disk_browser,GTK_DIR_TAB_FORWARD);return TRUE;
}
static gboolean file_wildcard_command(GtkEntry *entry) {
 const gchar *query=gtk_entry_get_text(entry);gchar *trimmed=g_strdup(query);g_strstrip(trimmed);gboolean clear=!strcmp(trimmed,"*");g_free(trimmed);
 if(clear) { gtk_entry_set_text(entry,"");return TRUE; }
 InsightLookupFilter *filter=insight_lookup_filter_new(query,TRUE,NULL);gboolean wildcard=insight_lookup_filter_has_wildcards(filter);insight_lookup_filter_free(filter);
 if(wildcard) symbol_history_record(GTK_WIDGET(entry));
 return wildcard;
}
static void browser_entry_activate(GtkEntry *entry,gpointer data) {
 (void)data;if(GTK_WIDGET(entry)==wb.local_entry && wb.local_mode && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode))==0) { GtkWidget *tree=gtk_bin_get_child(GTK_BIN(ui_lookup_widget(main_widgets.window,"scrolledwindow2")));if(GTK_IS_TREE_VIEW(tree)) { GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));GtkTreeIter iter;if(gtk_tree_model_get_iter_first(model,&iter)) { GtkTreeIter child;while(gtk_tree_model_iter_children(model,&child,&iter)) iter=child;GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path);gtk_widget_grab_focus(tree); } }return; }if(GTK_WIDGET(entry)==wb.file_entry && (file_browse_command(entry) || file_wildcard_command(entry))) return;if(GTK_WIDGET(entry)==wb.local_entry || GTK_WIDGET(entry)==wb.symbol_entry || GTK_WIDGET(entry)==wb.file_entry) symbol_history_record(GTK_WIDGET(entry)); GtkTreeView *view=GTK_TREE_VIEW(GTK_WIDGET(entry)==wb.local_entry ? wb.local_view : (GTK_WIDGET(entry)==wb.symbol_entry ? wb.symbol_view : wb.file_view));
 if(!gtk_widget_get_sensitive(GTK_WIDGET(view))) return;
 GtkTreeModel *model=gtk_tree_view_get_model(view); GtkTreeIter iter;
 if(gtk_tree_model_get_iter_first(model,&iter)) { GtkTreeIter child; while(gtk_tree_model_iter_children(model,&child,&iter)) iter=child; GtkTreePath *path=gtk_tree_model_get_path(model,&iter); gtk_tree_view_expand_to_path(view,path); browser_activated(view,path,NULL,NULL); gtk_tree_path_free(path); }
}
static void outline_changed(GtkToggleButton *button,gpointer data) { (void)button; (void)data; browser_filter(NULL,NULL); }
static gint project_path_compare(gconstpointer a,gconstpointer b) { return g_strcmp0(*(gchar *const *)a,*(gchar *const *)b); }
static void files_filter(GtkEditable *entry,gpointer data) {
 (void)data; if(!wb.file_store) return;
 if(entry) insight_virtual_list_position(GTK_TREE_VIEW(wb.file_view),0);
 g_ptr_array_sort(wb.files,project_path_compare);GChecksum *sum=g_checksum_new(G_CHECKSUM_SHA256);const gchar *query=gtk_entry_get_text(GTK_ENTRY(wb.file_entry));g_checksum_update(sum,(const guchar *)query,strlen(query)+1);if(wb.root) g_checksum_update(sum,(const guchar *)wb.root,strlen(wb.root)+1);
 for(guint n=0;n<wb.files->len;n++) { const gchar *path=g_ptr_array_index(wb.files,n);g_checksum_update(sum,(const guchar *)path,strlen(path)+1); }
 GeanyDocument *active=document_get_current();gchar *profile=active?insight_languages_profile_name(active->editor->sci,active->file_name,active->file_type):g_strdup("");g_checksum_update(sum,(const guchar *)profile,strlen(profile)+1);g_free(profile);if(active&&active->file_type) g_checksum_update(sum,(const guchar *)active->file_type->name,strlen(active->file_type->name)+1);
 if(!wb.workspace_from_project) for(guint n=0;n<documents_array->len;n++) { GeanyDocument *d=g_ptr_array_index(documents_array,n);if(d->is_valid&&d->file_name) g_checksum_update(sum,(const guchar *)d->file_name,strlen(d->file_name)+1); }
 const gchar *key=g_checksum_get_string(sum);gboolean same=!g_strcmp0(key,g_object_get_data(G_OBJECT(wb.file_store),"insight-request-key"));if(!same) g_object_set_data_full(G_OBJECT(wb.file_store),"insight-request-key",g_strdup(key),g_free);g_checksum_free(sum);if(same) return;
 SymbolListJob *j=symbol_list_begin(wb.file_store,wb.file_entry,10000,TRUE); if(!j) return;
 j->paths=g_ptr_array_new_with_free_func(g_free);
 for(guint n=0;n<wb.files->len;n++) g_ptr_array_add(j->paths,g_strdup(g_ptr_array_index(wb.files,n)));
 if(!wb.workspace_from_project) for(guint n=0;n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n); if(doc->is_valid && doc->file_name) g_ptr_array_add(j->paths,g_strdup(doc->file_name)); }
 symbol_list_launch(wb.file_store,j);
}
static void file_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; GtkTreeIter iter; GtkTreeModel *model;
 if(!wb.preview || !gtk_widget_get_sensitive(GTK_WIDGET(gtk_tree_selection_get_tree_view(selection))) || !gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 gchar *path; gtk_tree_model_get(model,&iter,1,&path,-1); reveal_context(); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); request_file_preview(path); g_free(path);
}
static void file_action(guint action) {
 if(action==FP_HELP) { insight_help_show(NULL,"panel-project-files.md");return; }if(!gtk_widget_get_sensitive(wb.file_view)) return;
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(wb.file_view));GtkTreeIter iter;gboolean selected=gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.file_view)),NULL,&iter);gchar *name=NULL,*path=NULL;if(selected) gtk_tree_model_get(model,&iter,0,&name,1,&path,-1);
 switch(action) {
 case FP_REVEAL: if(path) ui_reveal_file(path);break;
 case FP_OPEN: if(selected) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter);browser_activated(GTK_TREE_VIEW(wb.file_view),row,NULL,NULL);gtk_tree_path_free(row); }break;
 case FP_NAME: case FP_PATH: if(selected) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),action==FP_NAME ? name : path,-1);break;
 case FP_FILTER: if(path) { gchar *query=g_strdup_printf("file:\"%s\"",path);if(strlen(query)<=1024) { gtk_entry_set_text(GTK_ENTRY(wb.file_entry),query);gtk_widget_grab_focus(wb.file_entry); }else ui_set_statusbar(TRUE,"Selected value is too long for a lookup filter");g_free(query); }break;
 case FP_CLEAR: gtk_entry_set_text(GTK_ENTRY(wb.file_entry),"");gtk_widget_grab_focus(wb.file_entry);break;
 case FP_RESET: insight_symbol_view_columns_reset(GTK_TREE_VIEW(wb.file_view));break;
 default:break;
 }g_free(name);g_free(path);
}
