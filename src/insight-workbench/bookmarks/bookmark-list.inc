/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Bookmark window and list state.
 * RU: Окно закладок и состояние списка. */

static void bookmark_window_destroyed(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;wb.bookmark_window=NULL;wb.bookmark_search=NULL;g_clear_object(&wb.bookmark_list);
}
static void show_bookmarks(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;insight_workbench_bookmarks_changed();
 if(wb.bookmark_window) { bookmark_list_reload(NULL,NULL);gtk_window_present(GTK_WINDOW(wb.bookmark_window));return; }
 wb.bookmark_window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(wb.bookmark_window),"Bookmarks");gtk_window_set_default_size(GTK_WINDOW(wb.bookmark_window),750,420);
 gtk_window_set_transient_for(GTK_WINDOW(wb.bookmark_window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(wb.bookmark_window),TRUE);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*view;gtk_container_set_border_width(GTK_CONTAINER(box),6);gtk_container_add(GTK_CONTAINER(wb.bookmark_window),box);
 wb.bookmark_search=gtk_search_entry_new();gtk_widget_set_tooltip_text(wb.bookmark_search,"Prefix with ~ for fuzzy subsequence matching");gtk_widget_set_name(wb.bookmark_search,"insight-bookmark-list-query");gtk_entry_set_placeholder_text(GTK_ENTRY(wb.bookmark_search),"Filter bookmarks…");gtk_box_pack_start(GTK_BOX(box),wb.bookmark_search,FALSE,FALSE,0);
 wb.bookmark_list=gtk_list_store_new(6,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT64,G_TYPE_UINT,G_TYPE_INT,G_TYPE_STRING);view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.bookmark_list));
 gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),-1,"File:line > source",gtk_cell_renderer_text_new(),"text",0,NULL);gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view),1);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.bookmark_list),0,GTK_SORT_ASCENDING);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 GtkWidget *refresh=gtk_button_new_with_label("Refresh");gtk_box_pack_start(GTK_BOX(box),refresh,FALSE,FALSE,0);
 g_signal_connect(refresh,"clicked",G_CALLBACK(bookmark_list_reload),NULL);g_signal_connect(wb.bookmark_search,"changed",G_CALLBACK(bookmark_list_reload),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),"changed",G_CALLBACK(bookmark_list_selected),NULL);
 insight_panel_help_attach(gtk_bin_get_child(GTK_BIN(wb.bookmark_window)),"panel-bookmarks.md");
 g_signal_connect(wb.bookmark_window,"destroy",G_CALLBACK(bookmark_window_destroyed),NULL);bookmark_list_reload(NULL,NULL);gtk_widget_show_all(wb.bookmark_window);gtk_widget_grab_focus(wb.bookmark_search);
}
static void bookmark_scope_label(void) {
 if(!wb.marks_scope) return;
 gchar *base=wb.project_marks_path ? g_path_get_basename(wb.project_marks_path) : NULL,*label=wb.marks_stale ? g_strdup("Previous bookmark store (unsaved)") : wb.marks_project ? g_strdup_printf(wb.marks_valid ? "Project bookmarks: %s" : "Project bookmarks unavailable: %s",base ? base : "") : g_strdup("Global bookmarks (no project)");
 gtk_label_set_text(GTK_LABEL(wb.marks_scope),label);gtk_widget_set_tooltip_text(wb.marks_scope,wb.project_marks_path ? wb.project_marks_path : wb.notes_path);gtk_widget_set_sensitive(wb.marks_reload,wb.marks_project || wb.marks_stale);g_free(label);g_free(base);
}
static gboolean bookmarks_project_load(const gchar *project,const gchar *root,gboolean force) {
 if(!wb.project_marks) return FALSE;
 gchar *native=project ? utils_get_locale_from_utf8(project) : NULL,*path=native ? g_strconcat(native,".bookmarks.conf",NULL) : NULL;g_free(native);
 gboolean same=!g_strcmp0(path,wb.project_marks_path) && !g_strcmp0(root,wb.project_marks_root);
 if(wb.marks_initialized && !force && same && !wb.marks_stale) { g_free(path);return TRUE; }
 if(wb.marks_initialized) {
  if(force && same && wb.marks_save_failed) { ui_set_statusbar(TRUE,"Unsaved bookmarks remain in memory; export them before reloading");g_free(path);return FALSE; }
  if(!(force && same)) {
   gboolean stale=wb.marks_stale;wb.marks_stale=FALSE;
   if(!stale) {
    for(guint i=0;i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!doc->is_valid || !doc->file_name) continue;for(guint n=0;n<notes_for(INSIGHT_BOOKMARK)->items->len;n++) { const InsightNote *note=g_ptr_array_index(notes_for(INSIGHT_BOOKMARK)->items,n);if(note->kind==INSIGHT_BOOKMARK && !g_strcmp0(note->path,doc->file_name)) { bookmark_prepare_tags(doc);break; } } }
    insight_workbench_bookmarks_changed();
   }
   if(wb.marks_save_failed && !notes_save()) { wb.marks_stale=TRUE;wb.marks_generation++;bookmark_scope_label();g_free(path);return FALSE; }
  }
  bookmark_clear_handles();
 }
 wb.marks_initialized=TRUE;wb.marks_generation++;wb.marks_project=project!=NULL;wb.marks_valid=path!=NULL && root && g_path_is_absolute(root);wb.marks_stale=FALSE;wb.marks_save_failed=FALSE;
 insight_notes_free(wb.project_marks);wb.project_marks=insight_notes_new();g_free(wb.project_marks_path);wb.project_marks_path=path;g_free(wb.project_marks_root);wb.project_marks_root=g_strdup(root);
 if(path && wb.marks_valid && g_file_test(path,G_FILE_TEST_EXISTS)) {
  bookmark_session_remember(path,root,NULL);
  GError *error=NULL;InsightNotes *loaded=insight_notes_load_portable(path,root,&error),*marks=loaded ? insight_notes_select(loaded,INSIGHT_BOOKMARK,&error) : NULL;
  if(marks && marks->items->len!=loaded->items->len) { insight_notes_free(marks);marks=NULL;g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Project bookmark store contains non-bookmark notes"); }
  insight_notes_free(loaded);
  if(marks) { insight_notes_free(wb.project_marks);wb.project_marks=marks; }else { g_hash_table_remove(wb.marks_session_stores,path);wb.marks_valid=FALSE;ui_set_statusbar(TRUE,"Project bookmarks ignored: %s",error ? error->message : "invalid snapshot"); }g_clear_error(&error);
 }
 bookmark_scope_label();if(wb.marks_view) gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)));
 notes_filter(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK));
 for(guint i=0;i<documents_array->len;i++) bookmark_restore_document(g_ptr_array_index(documents_array,i));
 bookmark_list_reload(NULL,NULL);return TRUE;
}
static void bookmarks_project_reload(GtkButton *button,gpointer data) { (void)button;(void)data;gchar *root=app->project ? project_get_base_path() : NULL;bookmarks_project_load(app->project ? app->project->file_name : NULL,root,TRUE);g_free(root); }
static void notes_filter(GtkEditable *entry,gpointer data) {
 (void)entry; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); GtkListStore *store=kind==INSIGHT_BOOKMARK ? wb.marks_store : wb.clips_store; GtkWidget *field=kind==INSIGHT_BOOKMARK ? wb.marks_entry : wb.clips_entry;
 if(!store) return;
 GPtrArray *selected=selected_notes(kind);gchar *language=kind==INSIGHT_CLIP ? clip_current_language() : NULL;GHashTable *wanted=g_hash_table_new(g_str_hash,g_str_equal);
 for(guint i=0;i<selected->len;i++) g_hash_table_add(wanted,g_ptr_array_index(selected,i));
 GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(kind==INSIGHT_CLIP ? wb.clips_view : wb.marks_view));
 if(selection) g_signal_handlers_block_by_func(selection,note_selected,GINT_TO_POINTER(kind));
 gtk_list_store_clear(store); const gchar *query=gtk_entry_get_text(GTK_ENTRY(field));
 InsightNotes *source=notes_for(kind);
 for(guint i=0;i<source->items->len;i++) { InsightNote *note=g_ptr_array_index(source->items,i); if(note->kind!=kind || (kind==INSIGHT_CLIP && !(wb.clips_all && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.clips_all))) && !insight_note_matches_language(note,language))) continue;
  gchar *line=g_strdup_printf("%"G_GUINT64_FORMAT,note->line),*search=g_strconcat(note->name," ",note->path," ",line," ",note->anchor ? note->anchor->name : "",NULL); gboolean match=insight_lookup_text_match(search,query); g_free(line); g_free(search); if(!match) continue;
  GtkTreeIter iter; gtk_list_store_append(store,&iter); gtk_list_store_set(store,&iter,0,note->name,1,note->path,2,(gulong)note->line,3,note->text,4,note->anchor ? note->anchor->name : "",-1); if(g_hash_table_contains(wanted,note->name)) gtk_tree_selection_select_iter(selection,&iter);
 }
 if(selection) g_signal_handlers_unblock_by_func(selection,note_selected,GINT_TO_POINTER(kind));
 if(kind==INSIGHT_CLIP) { g_free(wb.clips_language); wb.clips_language=language; } else g_free(language);g_hash_table_unref(wanted);g_ptr_array_unref(selected);
}
static void clips_scope_changed(GtkComboBox *combo,gpointer data) {
 (void)data;gboolean project=gtk_combo_box_get_active(combo)==1;
 if(project && (!wb.project_notes_path || !wb.project_notes_valid)) { ui_set_statusbar(TRUE,"Open a project with a valid template store first");gtk_combo_box_set_active(combo,0);return; }
 if(!wb.clips_scope_setting) wb.clips_prefer_project=project;
 if(project==wb.clips_project) return;
 wb.clips_project=project;wb.clips_generation++;gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.clips_view)));notes_filter(NULL,GINT_TO_POINTER(INSIGHT_CLIP));
}
static void clips_project_load(const gchar *project,gboolean force) {
 if(!wb.project_notes) return;
 gchar *native=project ? utils_get_locale_from_utf8(project) : NULL,*path=native ? g_strconcat(native,".templates.conf",NULL) : NULL;g_free(native);
 if(!force && !g_strcmp0(path,wb.project_notes_path)) { g_free(path);return; }
 wb.clips_generation++;insight_notes_free(wb.project_notes);wb.project_notes=insight_notes_new();g_free(wb.project_notes_path);wb.project_notes_path=path;wb.project_notes_valid=path!=NULL;
 if(path && g_file_test(path,G_FILE_TEST_EXISTS)) { GError *error=NULL;InsightNotes *loaded=insight_notes_load(path,&error),*clips=loaded ? insight_notes_select(loaded,INSIGHT_CLIP,&error) : NULL;insight_notes_free(loaded);if(clips) { insight_notes_free(wb.project_notes);wb.project_notes=clips; }else { wb.project_notes_valid=FALSE;ui_set_statusbar(TRUE,"Project templates ignored: %s",error ? error->message : "invalid snapshot"); }g_clear_error(&error); }
 gtk_widget_set_tooltip_text(wb.clips_scope,path ? path : "Open a project to use project templates");
 wb.clips_scope_setting=TRUE;gtk_combo_box_set_active(GTK_COMBO_BOX(wb.clips_scope),wb.clips_prefer_project && wb.project_notes_valid ? 1 : 0);wb.clips_scope_setting=FALSE;
 gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.clips_view)));notes_filter(NULL,GINT_TO_POINTER(INSIGHT_CLIP));
}
static void clips_project_reload(GtkButton *button,gpointer data) { (void)button;(void)data;clips_project_load(app->project ? app->project->file_name : NULL,TRUE); }
static GPtrArray *selected_notes(InsightNoteKind kind) {
 GtkWidget *view=kind==INSIGHT_BOOKMARK ? wb.marks_view : wb.clips_view;GtkTreeModel *model=NULL;
 GList *rows=gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&model);GPtrArray *names=g_ptr_array_new_with_free_func(g_free);
 for(GList *p=rows;p;p=p->next) { GtkTreeIter iter;if(gtk_tree_model_get_iter(model,&iter,p->data)) { gchar *name=NULL;gtk_tree_model_get(model,&iter,0,&name,-1);g_ptr_array_add(names,name); } }
 g_list_free_full(rows,(GDestroyNotify)gtk_tree_path_free);return names;
}
static gchar *selected_note(InsightNoteKind kind) {
 GPtrArray *names=selected_notes(kind);gchar *name=names->len==1 ? g_strdup(g_ptr_array_index(names,0)) : NULL;g_ptr_array_unref(names);return name;
}
static gchar *bookmark_cursor_note(void) {
 GtkTreePath *path=NULL;GtkTreeIter iter;gchar *name=NULL;gtk_tree_view_get_cursor(GTK_TREE_VIEW(wb.marks_view),&path,NULL);
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(wb.marks_view));
 if(path && gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)),path) && gtk_tree_model_get_iter(model,&iter,path)) gtk_tree_model_get(model,&iter,0,&name,-1);
 if(path) gtk_tree_path_free(path);
 return name;
}
static void note_selected(GtkTreeSelection *selection,gpointer data) {
 (void)selection; InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data); gchar *name=selected_note(kind); if(!name || !wb.preview) { g_free(name); return; }
 const InsightNote *note=insight_notes_get(notes_for(kind),kind,name);
 if(note && kind==INSIGHT_BOOKMARK) bookmark_open_note(note);
 else if(note) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),TRUE); GeanyDocument *doc=document_get_current(); render_source(note->text,doc ? doc->file_type : NULL,1,NULL); g_clear_pointer(&wb.source_path,g_free); g_clear_pointer(&wb.source_digest,g_free); wb.source_id=0; wb.source_line=0; preview_links();navigation_state(); gchar *label=g_strdup_printf("Clip — %s",note->name); gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label); }
 g_free(name);
}
static void bookmark_add(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!bookmarks_writable()) return;GeanyDocument *doc=document_get_current();if(!doc || !doc->file_name) { ui_set_statusbar(TRUE,"Save the file before adding a persistent bookmark");return; }
 bookmark_prepare_tags(doc);insight_workbench_bookmarks_changed();gint line=sci_get_current_line(doc->editor->sci);gchar *text=bookmark_line_text(doc->editor->sci,line);
 const gchar *input=gtk_entry_get_text(GTK_ENTRY(wb.marks_entry));gchar *name=*input ? g_strdup(input) : NULL;
 for(guint n=0;!name;n++) { gchar *candidate=n<26 ? g_strdup_printf("%c",'A'+n) : g_strdup_printf("%c%u",'A'+n%26,n/26);if(!insight_notes_get(notes_for(INSIGHT_BOOKMARK),INSIGHT_BOOKMARK,candidate)) name=candidate;else g_free(candidate); }GError *error=NULL;InsightNotes *candidate=notes_clone(INSIGHT_BOOKMARK,&error);
 if(candidate && insight_notes_set(candidate,INSIGHT_BOOKMARK,name,doc->file_name,(guint64)line+1,text,&error)) { GArray *symbols=bookmark_symbols(doc);if(symbols) { insight_notes_capture_anchor(candidate,name,(const InsightBookmarkSymbol *)symbols->data,symbols->len,NULL);g_array_unref(symbols); }notes_commit(candidate,INSIGHT_BOOKMARK); }
 else { insight_notes_free(candidate);ui_set_statusbar(TRUE,"Bookmark: %s",error ? error->message : "not stored"); }
 g_clear_error(&error);g_free(name);g_free(text);
}
/* Snapshots of the loaded, filtered relation model. No traversal loads branches. */
typedef struct { gchar *name,*path,*source,*digest;gulong line;guint id;gboolean borrowed; } RelationNoteRow;
typedef struct { GString *text;GPtrArray *rows;guint visited;gboolean invalid; } RelationSnapshot;
typedef struct { GtkWidget *dialog,*preview,*status;RelationSnapshot *snapshot;GCancellable *cancel;guint epoch;guint64 scope;gboolean busy,closed; } RelationReport;
typedef struct { GPtrArray *rows;InsightNotes *candidate;GHashTable *versions;gchar *outline,*store_digest;guint epoch;guint64 scope;gint encoding; } RelationNoteJob;
static void relation_note_row_free(gpointer data) { RelationNoteRow *r=data;g_free(r->name);g_free(r->path);if(!r->borrowed) g_free(r->source);g_free(r->digest);g_free(r); }
static void relation_snapshot_free(RelationSnapshot *s) { if(s) { g_string_free(s->text,TRUE);g_ptr_array_unref(s->rows);g_free(s); } }
static gchar *relation_location_key(const gchar *path,gulong line) { return g_strdup_printf("%" G_GSIZE_FORMAT ":%s:%lu",strlen(path),path,line); }
static gchar *relation_escape(const gchar *text) {
 GString *s=g_string_new(NULL);for(const gchar *p=text ? text:"";*p;p++) { if(*p=='\\') g_string_append(s,"\\\\");else if(*p=='\n') g_string_append(s,"\\n");else if(*p=='\r') g_string_append(s,"\\r");else if(*p=='\t') g_string_append(s,"\\t");else g_string_append_c(s,*p); }return g_string_free(s,FALSE);
}
static gboolean relation_snapshot_row(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 RelationSnapshot *s=data;gint depth=gtk_tree_path_get_depth(path);if(++s->visited>4000 || depth>64) { s->invalid=TRUE;return TRUE; }
 gchar *name=NULL,*file=NULL;gulong line=0;guint id=0;gtk_tree_model_get(model,iter,USAGE_SYMBOL,&name,USAGE_PATH,&file,USAGE_LINE,&line,USAGE_ID,&id,-1);
 if((name && (!g_utf8_validate(name,-1,NULL) || strlen(name)>8192)) || (file && (!g_utf8_validate(file,-1,NULL) || strlen(file)>16384))) s->invalid=TRUE;
 if(!s->invalid && name && *name) {
  gchar *n=relation_escape(name),*f=relation_escape(file);for(gint i=1;i<depth;i++) g_string_append(s->text,"  ");g_string_append(s->text,n);
  if(file && *file && line) g_string_append_printf(s->text,"\t%s:%lu",f,line);g_string_append_c(s->text,'\n');g_free(n);g_free(f);
  if(file && g_path_is_absolute(file) && line && line<=2147483647) { RelationNoteRow *r=g_new0(RelationNoteRow,1);r->name=g_strdup(name);r->path=g_strdup(file);r->line=line;r->id=id;g_ptr_array_add(s->rows,r); }
 }
 g_free(name);g_free(file);if(s->text->len>1024u*1024u || s->rows->len>2000) s->invalid=TRUE;return s->invalid;
}
static RelationSnapshot *relation_snapshot_new(void) {
 if(wb.search_worker || wb.usage_idle || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==3 || strlen(gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)))>8192) return NULL;
 const gchar *shown=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));gboolean tree=!g_strcmp0(shown,"tree") || !g_strcmp0(shown,"treegraph");GtkTreeView *view=tree ? insight_relation_tree_view(wb.relation_tree):GTK_TREE_VIEW(wb.usage_view);
 RelationSnapshot *s=g_new0(RelationSnapshot,1);s->rows=g_ptr_array_new_with_free_func(relation_note_row_free);gchar *query=relation_escape(gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)));s->text=g_string_new("Relations: ");g_string_append_printf(s->text,"%s\nMode: %d; %s\n",query,gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)),tree ? "loaded tree":"filtered list");g_free(query);
 gtk_tree_model_foreach(gtk_tree_view_get_model(view),relation_snapshot_row,s);if(s->invalid) { relation_snapshot_free(s);return NULL; }return s;
}
static void relation_print_show(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(wb.search_worker || wb.usage_idle || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==3) { ui_set_statusbar(TRUE,"Wait for symbol Relations to finish before printing");return; }
 const gchar *view=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));GtkPrintOperation *op=NULL;
 if(!g_strcmp0(view,"graph") || !g_strcmp0(view,"treegraph")) { op=insight_relation_graph_print_operation(!strcmp(view,"treegraph") ? wb.relation_tree_graph:wb.relation_graph);if(!op) { ui_set_statusbar(TRUE,"Graph print snapshot could not be created");return; } }
 else { RelationSnapshot *snapshot=relation_snapshot_new();if(snapshot) { op=insight_text_print_operation(insight_tr("Relations"),snapshot->text->str,snapshot->text->len);relation_snapshot_free(snapshot); } }
 insight_text_print_run(op,GTK_WINDOW(main_widgets.window));
}
static void relation_report_free(gpointer data) { RelationReport *s=data;relation_snapshot_free(s->snapshot);g_clear_object(&s->cancel);g_free(s); }
static void relation_report_destroy(GtkWidget *widget,gpointer data) { (void)widget;RelationReport *s=data;s->closed=TRUE;if(s->cancel) g_cancellable_cancel(s->cancel); }
static void relation_note_job_free(gpointer data) { RelationNoteJob *j=data;g_ptr_array_unref(j->rows);insight_notes_free(j->candidate);g_hash_table_unref(j->versions);g_free(j->outline);g_free(j->store_digest);g_free(j); }
static gchar *relation_file_version(const gchar *path,GCancellable *cancel) {
 gchar *native=utils_get_locale_from_utf8(path);GFile *file=g_file_new_for_path(native);g_free(native);GFileInfo *info=g_file_query_info(file,"standard::type,standard::size,time::modified,time::modified-usec,etag::value,id::file",G_FILE_QUERY_INFO_NONE,cancel,NULL);g_object_unref(file);
 if(!info) return NULL;gchar *version=NULL;if(g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR) version=g_strdup_printf("%" G_GOFFSET_FORMAT ":%" G_GUINT64_FORMAT ":%u:%s:%s",g_file_info_get_size(info),g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_TIME_MODIFIED),g_file_info_get_attribute_uint32(info,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC),g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ETAG_VALUE) ? g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ETAG_VALUE):"",g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE) ? g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE):"");g_object_unref(info);return version;
}
static gchar *relation_source_line(const gchar *text,gulong line) {
 const gchar *p=text;for(gulong n=1;n<line;n++) { while(*p && *p!='\n' && *p!='\r') p++;if(!*p) return NULL;gchar c=*p++;if(c=='\r' && *p=='\n') p++; }
 const gchar *end=p;while(*end && *end!='\n' && *end!='\r') end++;if(end-p>65536) return NULL;return g_strndup(p,(gsize)(end-p));
}
static void relation_notes_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;RelationNoteJob *j=data;GHashTable *files=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);gsize bytes=0;GError *error=NULL;
 for(guint i=0;i<j->rows->len && !error;i++) {
  RelationNoteRow *r=g_ptr_array_index(j->rows,i);if(g_cancellable_set_error_if_cancelled(cancel,&error)) break;const gchar *text=g_hash_table_lookup(files,r->path);
  if(!text) {
   gchar *loaded=r->source ? g_strdup(r->source):NULL,*version=NULL;
   if(!r->source) { version=relation_file_version(r->path,cancel);if(version) loaded=read_disk_source_full(r->path,TRUE,j->encoding,cancel);gchar *after=loaded ? relation_file_version(r->path,cancel):NULL;if(!version || !after || strcmp(version,after)) g_clear_pointer(&loaded,g_free);g_free(after); }
   if(!loaded || bytes+strlen(loaded)>16u*1024u*1024u) { g_free(loaded);g_free(version);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Cannot read a stable source snapshot (2 MiB per file, 16 MiB total)");break; }
   bytes+=strlen(loaded);g_hash_table_insert(files,g_strdup(r->path),loaded);text=loaded;if(version) g_hash_table_insert(j->versions,g_strdup(r->path),version);
  }
  gchar *line=relation_source_line(text,r->line);if(!line) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"A result line is unavailable or exceeds 64 KiB; refresh Relations");break; }
  gchar *base=g_utf8_substring(r->name,0,MIN((glong)32,g_utf8_strlen(r->name,-1))),*name=NULL;for(guint n=1;!name;n++) { gchar *candidate=g_strdup_printf("%s #%u",base,n);if(!insight_notes_get(j->candidate,INSIGHT_BOOKMARK,candidate)) name=candidate;else g_free(candidate); }
  insight_notes_set(j->candidate,INSIGHT_BOOKMARK,name,r->path,r->line,line,&error);g_free(base);g_free(name);g_free(line);
 }
 g_hash_table_unref(files);if(error) g_task_return_error(task,error);else g_task_return_boolean(task,TRUE);
}
static void relation_digest_field(GChecksum *sum,const gchar *text) { const gchar *value=text ? text:"";g_checksum_update(sum,(const guchar *)value,strlen(value)+1); }
static gchar *relation_notes_digest(const InsightNotes *notes) {
 GChecksum *sum=g_checksum_new(G_CHECKSUM_SHA256);for(guint i=0;i<notes->items->len;i++) { const InsightNote *n=g_ptr_array_index(notes->items,i);gchar *numbers=g_strdup_printf("%u:%" G_GUINT64_FORMAT ":%u:%" G_GUINT64_FORMAT ":%u",n->kind,n->line,n->anchor ? n->anchor->kind:0,n->anchor ? n->anchor->offset:0,n->languages ? g_strv_length(n->languages):0);relation_digest_field(sum,numbers);g_free(numbers);relation_digest_field(sum,n->name);relation_digest_field(sum,n->path);relation_digest_field(sum,n->text);relation_digest_field(sum,n->anchor ? n->anchor->name:NULL);relation_digest_field(sum,n->anchor ? n->anchor->scope:NULL);if(n->languages) for(guint k=0;n->languages[k];k++) relation_digest_field(sum,n->languages[k]);relation_digest_field(sum,"/"); }gchar *result=g_strdup(g_checksum_get_string(sum));g_checksum_free(sum);return result;
}
static gboolean relation_notes_current(RelationNoteJob *j) {
 if(!wb.alive || j->epoch!=workbench_epoch || !notes_scope_current(INSIGHT_BOOKMARK,j->scope)) return FALSE;
 gchar *store=relation_notes_digest(notes_for(INSIGHT_BOOKMARK));gboolean same_store=!strcmp(store,j->store_digest);g_free(store);if(!same_store) return FALSE;
 RelationSnapshot *current=relation_snapshot_new();gboolean valid=current && !strcmp(current->text->str,j->outline);relation_snapshot_free(current);if(!valid) return FALSE;
 GHashTable *checked=g_hash_table_new(g_str_hash,g_str_equal);for(guint i=0;i<j->rows->len && valid;i++) { RelationNoteRow *r=g_ptr_array_index(j->rows,i);if(g_hash_table_contains(checked,r->path)) continue;g_hash_table_add(checked,r->path);GeanyDocument *doc=document_find_by_filename(r->path);
  if(r->source) { if(!doc || doc->id!=r->id || sci_get_length(doc->editor->sci)>2*1024*1024) valid=FALSE;else { gchar *text=sci_get_contents(doc->editor->sci,-1),*digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);valid=!strcmp(digest,r->digest);g_free(text);g_free(digest); } }else if(doc) valid=FALSE;
 }
 g_hash_table_unref(checked);GHashTableIter it;gpointer path,version;g_hash_table_iter_init(&it,j->versions);while(valid && g_hash_table_iter_next(&it,&path,&version)) { gchar *current_version=relation_file_version(path,NULL);valid=current_version && !strcmp(current_version,version);g_free(current_version); }return valid;
}
static void relation_notes_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)data;RelationReport *s=g_object_get_data(source,"insight-relation-report");RelationNoteJob *j=g_task_get_task_data(G_TASK(result));GError *error=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);
 if(s->closed) { g_clear_error(&error);return; }s->busy=FALSE;g_object_set_data(source,"insight-relation-busy",NULL);g_clear_object(&s->cancel);
 for(gint response=1;response<=5;response++) gtk_dialog_set_response_sensitive(GTK_DIALOG(source),response,TRUE);
 if(ok && !relation_notes_current(j)) { ok=FALSE;g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_BUSY,"Relations, source or bookmark store changed; refresh before creating bookmarks"); }
 if(ok) { InsightNotes *candidate=j->candidate;j->candidate=NULL;ok=notes_commit(candidate,INSIGHT_BOOKMARK); }
 if(ok) { gchar *text=g_strdup_printf("%u bookmarks created; existing locations were kept",j->rows->len);gtk_label_set_text(GTK_LABEL(s->status),text);g_free(text); }else gtk_label_set_text(GTK_LABEL(s->status),error ? error->message:"Bookmarks were not saved");g_clear_error(&error);
}
static void relation_notes_start(RelationReport *s) {
 if(!s->snapshot || !bookmarks_writable()) return;if(s->epoch!=workbench_epoch || !notes_scope_current(INSIGHT_BOOKMARK,s->scope)) { gtk_label_set_text(GTK_LABEL(s->status),"Relations changed; refresh the outline first");return; }insight_workbench_bookmarks_changed();
 RelationSnapshot *current=relation_snapshot_new();gboolean fresh=current && !strcmp(current->text->str,s->snapshot->text->str);relation_snapshot_free(current);if(!fresh) { gtk_label_set_text(GTK_LABEL(s->status),"Relations changed; refresh the outline first");return; }
 GError *error=NULL;RelationNoteJob *j=g_new0(RelationNoteJob,1);j->rows=g_ptr_array_new_with_free_func(relation_note_row_free);j->versions=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);j->candidate=notes_clone(INSIGHT_BOOKMARK,&error);j->outline=g_strdup(s->snapshot->text->str);j->epoch=workbench_epoch;j->scope=notes_generation(INSIGHT_BOOKMARK);j->store_digest=relation_notes_digest(notes_for(INSIGHT_BOOKMARK));j->encoding=file_prefs.default_open_encoding;
 GHashTable *seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);InsightNotes *notes=notes_for(INSIGHT_BOOKMARK);for(guint i=0;i<notes->items->len;i++) { InsightNote *n=g_ptr_array_index(notes->items,i);if(n->kind==INSIGHT_BOOKMARK) g_hash_table_add(seen,relation_location_key(n->path,(gulong)n->line)); }
 GHashTable *opened=g_hash_table_new(g_str_hash,g_str_equal);gsize bytes=0;for(guint i=0;j->candidate && !error && i<s->snapshot->rows->len;i++) {
  RelationNoteRow *r=g_ptr_array_index(s->snapshot->rows,i);gchar *key=relation_location_key(r->path,r->line);if(g_hash_table_contains(seen,key)) { g_free(key);continue; }g_hash_table_add(seen,key);
  if(j->candidate->items->len+j->rows->len>=64) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"The batch exceeds the remaining note capacity (64 total); no bookmarks were created");break; }
  RelationNoteRow *copy=g_new0(RelationNoteRow,1);copy->name=g_strdup(r->name);copy->path=g_strdup(r->path);copy->line=r->line;GeanyDocument *doc=document_find_by_filename(r->path);
  if(doc) { if(sci_get_length(doc->editor->sci)>2*1024*1024) { relation_note_row_free(copy);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"A source exceeds 2 MiB; no bookmarks were created");break; }copy->id=doc->id;RelationNoteRow *first=g_hash_table_lookup(opened,r->path);if(first) { copy->source=first->source;copy->digest=g_strdup(first->digest);copy->borrowed=TRUE; }else { copy->source=sci_get_contents(doc->editor->sci,-1);if(strlen(copy->source)!=(gsize)sci_get_length(doc->editor->sci) || !g_utf8_validate(copy->source,-1,NULL)) { relation_note_row_free(copy);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Source contains NUL or invalid UTF-8; no bookmarks were created");break; }bytes+=strlen(copy->source);copy->digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,copy->source,-1);g_hash_table_insert(opened,copy->path,copy); } }
  g_ptr_array_add(j->rows,copy);if(bytes>16u*1024u*1024u) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Source snapshots exceed 16 MiB; no bookmarks were created");
 }
 g_hash_table_unref(opened);g_hash_table_unref(seen);if(error || !j->candidate || !j->rows->len) { gtk_label_set_text(GTK_LABEL(s->status),error ? error->message:"No new source locations; existing bookmarks were kept");g_clear_error(&error);relation_note_job_free(j);return; }
 s->busy=TRUE;s->cancel=g_cancellable_new();g_object_set_data(G_OBJECT(s->dialog),"insight-relation-busy",GINT_TO_POINTER(1));for(gint response=1;response<=5;response++) gtk_dialog_set_response_sensitive(GTK_DIALOG(s->dialog),response,FALSE);gtk_label_set_text(GTK_LABEL(s->status),"Preparing bookmarks from source snapshots…");
 GTask *task=g_task_new(s->dialog,s->cancel,relation_notes_done,NULL);g_task_set_task_data(task,j,relation_note_job_free);g_task_run_in_thread(task,relation_notes_run);g_object_unref(task);
}
static void relation_report_refresh(RelationReport *s) {
 s->epoch=workbench_epoch;s->scope=notes_generation(INSIGHT_BOOKMARK);relation_snapshot_free(s->snapshot);s->snapshot=relation_snapshot_new();gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->preview)),s->snapshot ? s->snapshot->text->str:"",-1);
 gchar *text=s->snapshot ? g_strdup_printf("%u source items. Loaded/filtered rows only; unloaded branches are not fetched. Existing bookmark locations are skipped.",s->snapshot->rows->len):g_strdup("Wait for Relations to finish, or reduce results (2000 source items / 1 MiB outline)");gchar *destination=g_strconcat(text,"\n",gtk_label_get_text(GTK_LABEL(wb.marks_scope)),NULL);gtk_label_set_text(GTK_LABEL(s->status),destination);g_free(destination);g_free(text);
 for(gint response=2;response<=5;response++) gtk_dialog_set_response_sensitive(GTK_DIALOG(s->dialog),response,s->snapshot!=NULL);
}
static void relation_report_show(GtkWidget *widget,gpointer data) {
 (void)widget;RelationReport *s=g_new0(RelationReport,1);s->dialog=gtk_dialog_new_with_buttons(insight_tr(GPOINTER_TO_INT(data) ? "Create Bookmarks from Relation Items":"View Relation Outline"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Close"),GTK_RESPONSE_CLOSE,insight_tr("Refresh"),1,insight_tr("Copy outline"),2,insight_tr("Open outline buffer"),3,insight_tr("Create bookmarks"),4,insight_tr("Print outline…"),5,NULL);g_object_ref_sink(s->dialog);gtk_widget_set_name(s->dialog,"insight-relation-report");gtk_window_set_default_size(GTK_WINDOW(s->dialog),820,520);g_object_set_data_full(G_OBJECT(s->dialog),"insight-relation-report",s,relation_report_free);g_signal_connect(s->dialog,"destroy",G_CALLBACK(relation_report_destroy),s);
 const gchar *names[]={"insight-relation-report-refresh","insight-relation-report-copy","insight-relation-report-open","insight-relation-report-create","insight-relation-report-print"};for(gint i=0;i<5;i++) gtk_widget_set_name(gtk_dialog_get_widget_for_response(GTK_DIALOG(s->dialog),i+1),names[i]);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(s->dialog)),*scroll=gtk_scrolled_window_new(NULL,NULL);s->preview=gtk_text_view_new();gtk_widget_set_name(s->preview,"insight-relation-report-text");gtk_text_view_set_editable(GTK_TEXT_VIEW(s->preview),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(s->preview),TRUE);g_object_set_data(G_OBJECT(s->preview),"insight-no-i18n",GINT_TO_POINTER(1));gtk_container_add(GTK_CONTAINER(scroll),s->preview);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);s->status=gtk_label_new("");gtk_widget_set_name(s->status,"insight-relation-report-status");gtk_label_set_line_wrap(GTK_LABEL(s->status),TRUE);gtk_label_set_selectable(GTK_LABEL(s->status),TRUE);gtk_box_pack_start(GTK_BOX(box),s->status,FALSE,FALSE,4);insight_panel_help_attach(box,"relation-results.md");relation_report_refresh(s);gtk_widget_show_all(s->dialog);
 for(;;) { gint response=gtk_dialog_run(GTK_DIALOG(s->dialog));if(response<0 || s->closed) break;if(s->busy) continue;if(response==1) relation_report_refresh(s);else if(response==2 && s->snapshot) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),s->snapshot->text->str,(gint)s->snapshot->text->len);else if(response==3 && s->snapshot) { GeanyDocument *doc=document_new_file(NULL,NULL,s->snapshot->text->str);if(doc) document_set_text_changed(doc,TRUE); }else if(response==4) relation_notes_start(s);else if(response==5 && s->snapshot) insight_text_print_run(insight_text_print_operation(insight_tr("Relations"),s->snapshot->text->str,s->snapshot->text->len),GTK_WINDOW(s->dialog)); }
 GtkWidget *dialog=s->dialog;gtk_widget_destroy(dialog);g_object_unref(dialog);
}

static void bookmark_delete_names(GPtrArray *names,gboolean all) {
 if(!names->len || !bookmarks_writable()) return;
 guint64 generation=notes_generation(INSIGHT_BOOKMARK);
 if(all || wb.marks_confirm_delete) {
  GtkWidget *dialog=gtk_message_dialog_new(GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"%s",insight_tr(all ? "Delete all bookmarks?" : "Delete selected bookmarks?"));
  gtk_window_set_title(GTK_WINDOW(dialog),insight_tr("Delete bookmarks"));gtk_widget_set_name(dialog,"insight-confirm-bookmark-delete");
  gchar *text=g_strdup_printf(insight_tr("Bookmarks to delete: %u. Source files and clips are kept."),names->len);gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),"%s",text);g_free(text);
  gtk_dialog_add_buttons(GTK_DIALOG(dialog),insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Delete"),GTK_RESPONSE_ACCEPT,NULL);gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);
  gint response=gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);if(response!=GTK_RESPONSE_ACCEPT || !notes_scope_current(INSIGHT_BOOKMARK,generation)) return;
 }
 GError *error=NULL;InsightNotes *candidate=notes_clone(INSIGHT_BOOKMARK,&error);
 if(candidate) { for(guint i=0;i<names->len;i++) insight_notes_remove(candidate,INSIGHT_BOOKMARK,g_ptr_array_index(names,i));notes_commit(candidate,INSIGHT_BOOKMARK); }
 if(error) ui_set_statusbar(TRUE,"Note: %s",error->message);
 g_clear_error(&error);
}
static void bookmark_delete_all(void) {
 GPtrArray *names=g_ptr_array_new_with_free_func(g_free);InsightNotes *source=notes_for(INSIGHT_BOOKMARK);
 for(guint i=0;i<source->items->len;i++) { InsightNote *note=g_ptr_array_index(source->items,i);if(note->kind==INSIGHT_BOOKMARK) g_ptr_array_add(names,g_strdup(note->name)); }
 bookmark_delete_names(names,TRUE);g_ptr_array_unref(names);
}
static void note_delete(GtkWidget *widget,gpointer data) {
 (void)widget;InsightNoteKind kind=(InsightNoteKind)GPOINTER_TO_INT(data);GPtrArray *names=selected_notes(kind);
 if(kind==INSIGHT_BOOKMARK) bookmark_delete_names(names,FALSE);
 else if(names->len) { GError *error=NULL;InsightNotes *candidate=notes_clone(kind,&error);
  if(candidate) { for(guint i=0;i<names->len;i++) insight_notes_remove(candidate,kind,g_ptr_array_index(names,i));notes_commit(candidate,kind); }
  if(error) ui_set_statusbar(TRUE,"Note: %s",error->message);
  g_clear_error(&error);
 }g_ptr_array_unref(names);
}
