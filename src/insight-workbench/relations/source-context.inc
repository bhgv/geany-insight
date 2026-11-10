/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: HDL colors and contextual source previews.
 * RU: Расцветка HDL и контекстный просмотр исходников. */

static void hdl_colors_done(GObject *source,GAsyncResult *result,gpointer data)
{
 (void)source;(void)data;
 GTask *task=G_TASK(result);HdlJob *j=g_task_get_task_data(task);
 if(hdl_color_worker==g_task_get_cancellable(task)) g_clear_object(&hdl_color_worker);
 if(!g_task_propagate_boolean(task,NULL) || !wb.alive || j->epoch!=workbench_epoch ||
    j->generation!=hdl_color_revision || j->scan_generation!=wb.scan_generation ||
    g_strcmp0(j->project,app->project ? app->project->file_name:NULL)) return;
 GeanyDocument *active=document_get_current();
 if(DOC_VALID(active) && active->id==j->paint_doc) hdl_paint(j);
}

typedef struct {HdlJob *job;guint next;gsize total;gint offset;GString *text;} HdlCapture;

static void hdl_capture_free(gpointer data)
{
 HdlCapture *c=data;
 if(c->job) hdl_job_free(c->job);
 if(c->text) g_string_free(c->text,TRUE);
 g_free(c);
}

static gboolean hdl_capture_step(gpointer data)
{
 HdlCapture *c=data;HdlJob *j=c->job;
 if(!wb.alive || j->generation!=hdl_color_revision || j->scan_generation!=wb.scan_generation) goto done;
 gint64 deadline=g_get_monotonic_time()+1500;
 while(c->next<documents_array->len) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,c->next);
  if(!DOC_VALID(doc) || !insight_slang_source(doc->file_name)) {c->next++;continue;}
  gint length=sci_get_length(doc->editor->sci);
  if(!c->text) {
   if(length<0 || length>2*1024*1024 || c->total+(gsize)length>32u*1024u*1024u ||
      g_hash_table_size(j->buffers)>=256) goto done;
   c->text=g_string_new(NULL);c->offset=0;c->total+=length;
   if(!g_ptr_array_find_with_equal_func(j->paths,doc->file_name,(GEqualFunc)g_str_equal,NULL))
    g_ptr_array_add(j->paths,g_strdup(doc->file_name));
   if(j->paths->len>256) goto done;
  }
  if(c->offset<length) {
   gint end=MIN(length,c->offset+65536),n=end-c->offset;
   gchar *part=sci_get_contents_range(doc->editor->sci,c->offset,end);
   if(!part || memchr(part,0,n)) {g_free(part);goto done;}
   g_string_append_len(c->text,part,n);g_free(part);c->offset=end;
  }
  if(c->offset==length) {
   g_hash_table_insert(j->buffers,g_strdup(doc->file_name),g_string_free(c->text,FALSE));
   c->text=NULL;c->next++;
  }
  if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 if(!j->root && j->paths->len) j->root=g_path_get_dirname(g_ptr_array_index(j->paths,0));
 hdl_color_worker=g_cancellable_new();
 GTask *task=g_task_new(NULL,hdl_color_worker,hdl_colors_done,NULL);
 g_task_set_task_data(task,j,hdl_job_free);c->job=NULL;
 g_task_run_in_thread(task,hdl_job_run);g_object_unref(task);
done:
 hdl_snapshot_idle=0;return G_SOURCE_REMOVE;
}

static gboolean hdl_auto_colors(gpointer data)
{
 (void)data;
 GeanyDocument *doc=wb.alive ? document_get_current():NULL;
 if(!DOC_VALID(doc) || !insight_slang_source(doc->file_name) ||
    SSM(doc->editor->sci,SCI_GETLEXER,0,0)!=SCLEX_VERILOG ||
    g_object_get_data(G_OBJECT(doc->editor->sci),"insight-hdl-painted")) {
  hdl_color_timer=0;return G_SOURCE_REMOVE;
 }
 if(hdl_color_worker || hdl_snapshot_idle || wb.search_worker || (app->project && wb.restore_worker)) return G_SOURCE_CONTINUE;
 hdl_color_timer=0;
 gchar *service=insight_completion_slang_service();
 gboolean available=g_file_test(service,G_FILE_TEST_IS_EXECUTABLE);
 g_free(service);
 if(!available) return G_SOURCE_REMOVE;
 HdlCapture *c=g_new0(HdlCapture,1);c->job=hdl_job_new(NULL,FALSE,TRUE);
 if(c->job->paths->len>256) {hdl_capture_free(c);return G_SOURCE_REMOVE;}
 hdl_snapshot_idle=g_idle_add_full(G_PRIORITY_LOW,hdl_capture_step,c,hdl_capture_free);
 return G_SOURCE_REMOVE;
}

static void hdl_schedule_colors(void)
{
 if(!wb.alive) return;
 if(hdl_color_timer) {g_source_remove(hdl_color_timer);hdl_color_timer=0;}
 GeanyDocument *doc=document_get_current();
 if(DOC_VALID(doc) && insight_slang_source(doc->file_name))
  hdl_color_timer=g_timeout_add(650,hdl_auto_colors,NULL);
}

static void hdl_references(const gchar *query,gboolean diagnostics) {
 hdl_cancel_colors();
 HdlJob *j=hdl_snapshot(query,diagnostics);
 wb.search_worker=g_cancellable_new();GTask *task=g_task_new(NULL,wb.search_worker,hdl_job_done,NULL);g_task_set_task_data(task,j,hdl_job_free);g_task_run_in_thread(task,hdl_job_run);g_object_unref(task);set_usage_status("Preparing HDL semantic project index…");
 hdl_schedule_colors();
}
static void usage_start(GtkWidget *widget,gpointer data) {
 (void)data; stop_usage();
 /* Explicit searches must reveal their results, even when the last view was
  * a source route. Background tracking must leave that independent graph open. */
 if(widget && wb.relation_stack) {
  if(!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"routes")) gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"list");
  relation_view_update();
 }
 wb.usage_region=reference_region();
 gint search_region=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.search_region));wb.project_region=search_region==1 ? INSIGHT_REGION_SKIP_COMMENTS : search_region==2 ? INSIGHT_REGION_COMMENTS : search_region==3 ? INSIGHT_REGION_CODE : INSIGHT_REGION_ALL;
 const char *query=gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)); size_t total;
 gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)); wb.usage_mode=mode;
 wb.usage_target=mode==3 ? wb.search_matches : wb.matches;
 if(mode==8) {if(!insight_clang_available()) {set_usage_status("This build has no bundled Clang backend");return;}GeanyDocument *doc=document_get_current();InsightClangResult *semantic=DOC_VALID(doc) ? insight_clang_ui_result(doc->editor->sci):NULL;if(insight_clang_result_usable(semantic)) wb.usage_usr=insight_clang_symbol_at(semantic->relations,doc->file_name,(guint)sci_get_current_position(doc->editor->sci),(guint)sci_get_current_line(doc->editor->sci)+1,query);}

 if(mode==11 || mode==12) {hdl_references(query,mode==12);return;}
 if(mode==9 || mode==10) {semantic_call_tree(query,mode==10);return;}
 if(mode==6) { relation_calls(query);return; }
 if(mode==1 || mode==2 || mode==4 || mode==5) {
  refresh_index(); RelationJob *j=g_new0(RelationJob,1); j->index=insight_index_ref(wb.index); j->query=g_strdup(query); j->mode=mode; j->transitive=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_transitive)); j->epoch=workbench_epoch; j->generation=wb.usage_generation; j->index_version=wb.index_version;GeanyDocument *active=document_get_current();if(active) j->language=insight_language_filter_new(active->file_name,active->file_type);
  wb.search_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.search_worker,relation_job_done,NULL); g_task_set_task_data(task,j,relation_job_free); g_task_run_in_thread(task,relation_job_run); g_object_unref(task); set_usage_status("Searching symbol relations…"); return;
 }
 wb.usage_flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_case)) ? INSIGHT_SEARCH_CASE : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_word)) ? INSIGHT_SEARCH_WORD : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_regex)) ? INSIGHT_SEARCH_REGEX : 0);
 GError *query_error=NULL;
 wb.usage_keywords=mode==3 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.search_keywords)); wb.usage_context=(guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(wb.search_context));
 if(wb.usage_keywords && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.search_variations))) wb.usage_flags|=INSIGHT_KEYWORD_VARIATIONS;
 if(mode==3 && !(wb.usage_keywords ? insight_keyword_collect("",0,query,wb.usage_flags,wb.usage_context,NULL,0,&total,&query_error) : insight_context_collect("",0,query,wb.usage_flags,NULL,0,&total,&query_error))) { set_usage_status(query_error ? query_error->message : "Invalid text search query"); g_clear_error(&query_error); return; }
 if(mode!=3 && !insight_occurrences_find("",0,query,strlen(query),0,NULL,0,&total)) { set_usage_status("Enter one UTF-8 identifier"); return; }
 if(mode==3) insight_search_links_clear(wb.search_links);
 wb.usage_existing=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,symbol_iter_queue_free); wb.usage_collecting=TRUE;
 wb.usage_iter_valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(wb.usage_target),&wb.usage_iter);
 if(mode==3) { gtk_entry_set_text(GTK_ENTRY(wb.project_query),query); insight_search_history_record(wb.search_history,query); save_search_settings(); if(widget) focus_search_results(NULL,NULL); }
 wb.usage_name=g_strdup(query); wb.usage_position=wb.usage_shown=wb.usage_skipped=0;
 GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
 gint scope=mode==3 ? gtk_combo_box_get_active(GTK_COMBO_BOX(wb.search_scope)) : (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer)) ? 2 : 0);
 GPtrArray *paths=mode==3 && scope==3 ? wb.search_files : wb.files;
 if(scope==0 || scope==3) {
  for(guint i=0;i<paths->len && wb.usage_paths->len<10000;i++) {
   const char *path=g_ptr_array_index(paths,i);GeanyDocument *active=document_get_current();if(active && !insight_languages_search_path(active->file_name,active->file_type,path)) continue; if(g_hash_table_contains(seen,path) || (mode==3 && !search_path_matches(path))) continue;
   Location *next=g_new0(Location,1); next->path=g_strdup(path); g_ptr_array_add(wb.usage_paths,next); g_hash_table_add(seen,next->path);
  }
 }
 for(guint i=0;i<documents_array->len && wb.usage_paths->len<10000;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);
  if(!doc->is_valid || (scope==2 && doc!=document_get_current()) || (mode==3 && (scope==0 || scope==3)) || (mode==3 && doc->file_name && !search_path_matches(doc->file_name))) continue;
  GeanyDocument *active=document_get_current();if(active && doc->file_name && !insight_languages_search_path(active->file_name,active->file_type,doc->file_name)) continue;
  if(doc->file_name && g_hash_table_contains(seen,doc->file_name)) continue;
  Location *next=g_new0(Location,1); next->path=g_strdup(doc->file_name); next->id=doc->id; g_ptr_array_add(wb.usage_paths,next);
  if(next->path) g_hash_table_add(seen,next->path);
 }
 g_hash_table_destroy(seen); wb.usage_idle=g_idle_add(usage_step,NULL);
}
static void usage_cancel(GtkWidget *widget,gpointer data) { (void)widget; (void)data; stop_usage();if(wb.relation_tree) insight_relation_tree_cancel(wb.relation_tree); set_usage_status("Search cancelled; partial results retained"); }
static void usage_selected(GtkTreeSelection *selection,gpointer data) {
 (void)data; GtkTreeIter iter; GtkTreeModel *model;
 if(!gtk_tree_selection_get_selected(selection,&model,&iter)) return;
 if(wb.search_links && model==GTK_TREE_MODEL(wb.search_matches)) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter);insight_search_links_refresh(wb.search_links,row);gtk_tree_path_free(row); }
 gchar *path,*digest; gulong line,start,end; guint id;
 gtk_tree_model_get(model,&iter,USAGE_PATH,&path,USAGE_LINE,&line,USAGE_START,&start,USAGE_END,&end,USAGE_ID,&id,USAGE_DIGEST,&digest,-1);
 if(!path && !id && !line) {wb.source_line=0;navigation_state();g_free(digest);return;}
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 if(show_location(path,id,line,TRUE)) {
  gchar *text=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1),*actual=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
  if(!digest) { /* Definitions and members navigate by current tag location. */ }
  else if(!g_strcmp0(actual,digest)) { SSM(wb.preview,SCI_SETSEL,end,start); decorate_match(wb.preview,start,end); SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0); }
  else { decorate_match(wb.preview,0,0); wb.source_line=0; navigation_state(); ui_set_statusbar(TRUE,"Source changed after search; run the usage search again"); }
  g_free(actual); g_free(text);
 }
 g_free(path); g_free(digest);
}
static void usage_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data;
 if(view==GTK_TREE_VIEW(wb.search_view) && insight_search_links_open(wb.search_links,path)) return;
 GtkTreeSelection *selection=gtk_tree_view_get_selection(view); gtk_tree_selection_select_path(selection,path);
 usage_selected(selection,NULL); if(wb.source_line) open_context(NULL,NULL);
}
static void relation_controls(GtkWidget *widget,gpointer data) {
 (void)data; if(wb.relation_setting) return; wb.relation_dirty=TRUE; stop_usage();
 gint relation=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));
 /* A deliberate mode change selects the symbol browser, not the separate
  * source-route canvas. Filters and automatic tracking preserve that canvas. */
 if(widget==wb.relation_mode && wb.relation_stack && !g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"routes")) gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"list");
 gtk_widget_set_sensitive(wb.relation_transitive,relation==2 || relation==4 || relation==5);
 relation_view_update();
 gboolean references=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==0;
 gtk_widget_set_sensitive(wb.usage_code,references);gtk_widget_set_sensitive(wb.usage_comments,references); gtk_widget_set_sensitive(wb.usage_buffer,references || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==3 || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==7 || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==8);
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow))) insight_workbench_refresh();
 else if(relation==12 || *gtk_entry_get_text(GTK_ENTRY(wb.usage_entry))) usage_start(NULL,NULL);
 else { gtk_list_store_clear(wb.matches); set_usage_status("Enter a symbol for Relations"); }
}
/* Disabling tracking freezes the displayed result set. It must not be
 * treated as a query-options change, which clears and starts the query. */
static void relation_follow_changed(GtkToggleButton *button,gpointer data) {
 (void)data;if(wb.relation_setting) return;
 if(gtk_toggle_button_get_active(button)) { wb.relation_dirty=TRUE;insight_workbench_refresh(); }
 else if(wb.usage_mode!=3) stop_usage();
}
static void relation_refresh_command(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;focus_widget_panel(wb.usage_entry);
 /* A panel command never dispatches the unrelated project text search. */
 if(gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode))==3) {
  set_usage_status("Select a symbol relation mode before refreshing Relations");return;
 }
 usage_start(NULL,NULL);
}
static void relation_update_from_editor(gboolean dirty) {
 if((wb.usage_idle || wb.search_worker) && wb.usage_mode==3) return;
 if(!wb.relation_follow || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow))) return;
 GeanyDocument *doc=document_get_current(); gchar word[1025]={0};
 if(doc) {
  refresh_index();
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_enclosing))) {
   TMTag *tag=relation_enclosing_function(doc);if(tag && tag->name) g_strlcpy(word,tag->name,sizeof word);
  } else editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
  if(!*word && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_enclosing))) {
   gulong line=(gulong)sci_get_current_line(doc->editor->sci)+1;TMTag *selected=NULL;guint best=0;
   if(doc->tm_file && doc->tm_file->tags_array) for(guint i=0;i<doc->tm_file->tags_array->len;i++) {
    TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i);
    if(!tag || tag->line!=line || !tag->name || !g_utf8_validate(tag->name,-1,NULL) || strlen(tag->name)>=sizeof word) continue;
    guint rank=(tag->kind_letter && strchr("fmcsi",tag->kind_letter)) ? 2 : 1;
    if(!selected || rank>best) { selected=tag;best=rank; }
   }
   if(selected) g_strlcpy(word,selected->name,sizeof word);
  }
 }
 gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));
 if(!dirty && !wb.relation_dirty && mode==wb.relation_last_mode && !g_strcmp0(word,wb.relation_word)) return;
 wb.relation_dirty=FALSE; wb.relation_last_mode=mode; g_free(wb.relation_word); wb.relation_word=g_strdup(word);
 gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),word);
 if(*word) usage_start(NULL,NULL);
 else { stop_usage(); gtk_list_store_clear(wb.matches); set_usage_status("Relations — no selected identifier"); }
}
static gboolean relation_row_visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)data;const gchar *query=gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry));if(!*query) return TRUE;
 gboolean match=FALSE;const gint fields[]={USAGE_SYMBOL,USAGE_DISPLAY,USAGE_SNIPPET};
 for(guint i=0;i<G_N_ELEMENTS(fields) && !match;i++) {
  gchar *value=NULL;gtk_tree_model_get(model,iter,fields[i],&value,-1);
  match=insight_lookup_text_match(value,query);g_free(value);
 }
 return match;
}
static void relation_filter_changed(GtkEditable *entry,gpointer data) {
 (void)entry;(void)data;if(wb.relation_filter) gtk_tree_model_filter_refilter(wb.relation_filter);if(wb.relation_tree) insight_relation_tree_filter(wb.relation_tree,gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));
}
typedef struct { gchar *query,*filter,*usr,*source;gint mode,comment_mode,zoom;gboolean buffer,code,transitive,tree,graph,vertical; } RelationVisit;
static void relation_visit_free(gpointer data) { RelationVisit *visit=data;if(!visit) return;g_free(visit->query);g_free(visit->filter);g_free(visit->usr);g_free(visit->source);g_free(visit); }
static RelationVisit *relation_visit_current(void) {
 RelationVisit *visit=g_new0(RelationVisit,1);visit->query=g_strdup(gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)));visit->filter=g_strdup(gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));visit->mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));if(visit->mode==9 || visit->mode==10) {visit->usr=g_strdup(g_object_get_data(G_OBJECT(wb.relation_tree),"insight-call-root-usr"));visit->source=g_strdup(g_object_get_data(G_OBJECT(wb.relation_tree),"insight-call-root-source"));}visit->buffer=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer));visit->code=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code));visit->comment_mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.usage_comments));visit->transitive=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_transitive));visit->tree=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle));visit->graph=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle));
 const gchar *shown=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));insight_relation_graph_get_options(shown && !strcmp(shown,"treegraph") ? wb.relation_tree_graph : wb.relation_graph,&visit->vertical,&visit->zoom);return visit;
}
static void relation_history_state(void) {
 gtk_widget_set_sensitive(wb.relation_back,wb.relation_history->len>0);gtk_widget_set_sensitive(wb.relation_forward,wb.relation_future->len>0);
}
static void relation_visit_push(GPtrArray *stack,RelationVisit *visit) { if(stack->len>=32) g_ptr_array_remove_index(stack,0);g_ptr_array_add(stack,visit); }
static RelationVisit *relation_visit_pop(GPtrArray *stack) {
 RelationVisit *visit=g_ptr_array_index(stack,stack->len-1);g_ptr_array_index(stack,stack->len-1)=NULL;g_ptr_array_remove_index(stack,stack->len-1);return visit;
}
static void relation_visit_restore(RelationVisit *visit) {
 wb.relation_setting=TRUE;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);
 gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),visit->query);gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry),visit->filter);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),visit->mode);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_buffer),visit->buffer);gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments),visit->comment_mode);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_code),visit->code);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_transitive),visit->transitive);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle),visit->tree);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle),visit->graph);
 /* Toggle callbacks can synchronize the two canvases. Restore geometry last. */
 insight_relation_graph_set_options(wb.relation_graph,visit->vertical,visit->zoom);insight_relation_graph_set_options(wb.relation_tree_graph,visit->vertical,visit->zoom);
 if(visit->mode==9 || visit->mode==10) {g_object_set_data(G_OBJECT(wb.usage_entry),"insight-semantic-restore",GINT_TO_POINTER(1));g_object_set_data_full(G_OBJECT(wb.usage_entry),"insight-semantic-usr",g_strdup(visit->usr),g_free);g_object_set_data_full(G_OBJECT(wb.usage_entry),"insight-semantic-source",g_strdup(visit->source),g_free);}wb.relation_setting=FALSE;relation_visit_free(visit);relation_history_state();relation_controls(NULL,NULL);
}
