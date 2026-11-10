/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Workspace selection and source lookups.
 * RU: Выбор рабочей области и поиск по исходникам. */

static void choose_root(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Open source workspace",GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); gboolean previous=wb.workspace_from_project; wb.workspace_from_project=FALSE; if(!start_scan(path)) { wb.workspace_from_project=previous; ui_set_statusbar(TRUE,"Cannot index source folder"); } g_free(path); }
 gtk_widget_destroy(dialog);
}
static void cancel_scan(GtkButton *button,gpointer data) { (void)button; (void)data;insight_master_sync_pause(wb.master_sync); stop_scan(); insight_workspace_watch_clear(wb.watch); wb.watch_changed=FALSE; if(wb.watch_idle) { g_source_remove(wb.watch_idle); wb.watch_idle=0; } refresh_index(); browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); file_types_refresh(); categories_filter(NULL,NULL); local_filter(NULL,NULL); insight_workbench_refresh(); ui_set_statusbar(TRUE,"Workspace scan cancelled; partial index retained"); }
static const InsightSymbol *word_definition(GeanyDocument *doc,const char *word,const char *preferred,gulong reference_line,ScintillaObject *reference_sci,gint reference_position) {
 if(!wb.definitions) return NULL;
 if(!preferred && doc) preferred=doc->file_name;
 refresh_index();
 GeanyFiletype *reference_type=reference_sci==wb.preview ? wb.preview_filetype : (doc ? doc->file_type : NULL);
 gboolean valid_word=word && *word && !definition_keyword(reference_type,word) && !insight_languages_keyword(reference_sci,word) && (!reference_sci || (sci_get_length(reference_sci)<=2*1024*1024 && definition_code(reference_sci,reference_position)));
 wb.populating=TRUE; g_ptr_array_set_size(wb.definitions,0); gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(wb.choices));
 const InsightSymbol *found[256],*chosen=NULL;gboolean imported=FALSE;gsize import_count=0;
 gchar *scope=NULL,*mask=NULL;gboolean uncertain_scope=FALSE;
 gboolean cached=reference_sci==wb.preview && wb.preview_links_text && reference_position>=0 && (gsize)reference_position<=wb.preview_links_length;
 gboolean python=python_language(reference_type);gboolean calls=member_calls_language(reference_type) && reference_sci && sci_get_length(reference_sci)<=2*1024*1024;gint reference_begin=(calls || python) ? 0 : MAX(0,reference_position-4096);
 gchar *reference=!cached && reference_sci ? ((calls || python) ? sci_get_contents(reference_sci,sci_get_length(reference_sci)+1) : sci_get_contents_range(reference_sci,reference_begin,reference_position)) : NULL;gboolean member=FALSE;
 gsize reference_length=reference ? ((calls || python) ? (gsize)sci_get_length(reference_sci) : (gsize)(reference_position-reference_begin)) : 0;
 if(reference && (!g_utf8_validate(reference,reference_length,NULL) || memchr(reference,0,reference_length))) valid_word=FALSE;
 ScopeLanguage language={preferred,reference_type};gchar *owner=NULL;
 if(python) {
  const gchar *bytes=cached ? wb.preview_links_text : reference;gsize length=cached ? wb.preview_links_length : reference_length;
  mask=!cached && bytes ? insight_source_code_mask(bytes,length,INSIGHT_CODE_PYTHON,NULL) : NULL;InsightNativeSource text={bytes,mask,length,NULL};
  if(cached) text=preview_python_source();else text.python=insight_native_python_bindings_new(wb.index,preferred,&text);
  scope=insight_native_python_scope(wb.index,preferred,&text,reference_position);
  import_count=insight_native_python_import_query(wb.index,preferred,scope,&text,reference_position,word,scope_language_eligible,&language,found,G_N_ELEMENTS(found),&imported);
  if(!imported) owner=insight_native_python_owner(wb.index,preferred,scope,reference_line,&text,reference_position,&member,scope_language_eligible,&language);
  if(!member && !imported) { gchar *binding=insight_native_python_binding_scope(wb.index,preferred,scope,word,&text);g_free(scope);scope=binding;if(!scope) valid_word=FALSE; }
  if(!cached) insight_native_python_bindings_free(text.python);
 } else if(cached) {
  scope=preview_function_scope(reference_line,reference_position,&uncertain_scope);InsightNativeSource text={wb.preview_links_text,wb.preview_code_mask,wb.preview_links_length,NULL};
  if(!uncertain_scope) owner=insight_native_member_owner_source_filtered(wb.index,preferred,scope,reference_line,&text,reference_position,&member,calls && wb.preview_code_mask!=NULL,scope_language_eligible,&language);
 } else if(calls && reference && valid_word) {
  mask=insight_source_code_mask(reference,reference_length,INSIGHT_CODE_C,NULL);InsightNativeSource text={reference,mask,reference_length,NULL};InsightNativeScopes *scopes=insight_native_scopes_new(wb.index,preferred,&text);scope=insight_native_scopes_at(scopes,reference_line,(gsize)reference_position,&uncertain_scope);
  if(!uncertain_scope) { owner=insight_native_member_owner_source_filtered(wb.index,preferred,scope,reference_line,&text,reference_position,&member,mask!=NULL,scope_language_eligible,&language); }insight_native_scopes_free(scopes);
 } else {
  scope=insight_native_function_scope(wb.index,preferred,reference_line);owner=insight_native_member_owner_calls_filtered(wb.index,preferred,scope,reference_line,reference,reference_position-reference_begin,&member,calls,scope_language_eligible,&language);
 }
 if(uncertain_scope) { valid_word=FALSE; }g_free(mask);g_free(reference);
 size_t count=valid_word ? (imported ? import_count : member ? insight_member_query(wb.index,owner,word,preferred,scope_language_eligible,&language,found,256,NULL,NULL) : insight_index_find(wb.index,word,TRUE,found,256)) : 0; gint selected=0;
 gboolean compiler_choice=FALSE;InsightClangResult *semantic=reference_sci ? insight_clang_ui_result(reference_sci):NULL;
 if(insight_clang_result_usable(semantic) && insight_clang_dependencies_fresh(semantic->relations,preferred) && word && *word) {gchar *usr=insight_clang_symbol_at(semantic->relations,preferred,(guint)MAX(0,reference_position),(guint)reference_line,word);const InsightSymbol *definition=NULL;
  if(usr) {gsize n=insight_index_query(semantic->relations,usr,TRUE,NULL,"@clangdef",preferred,0,&definition,1);if(n!=1) {n=insight_index_query(wb.disk_calls,usr,TRUE,NULL,"@clangdef",NULL,0,&definition,1);if(n!=1) definition=NULL;}}
  if(definition) {const InsightSymbol *candidates[256];gsize n=insight_index_find(wb.index,word,TRUE,candidates,256);for(gsize i=0;i<MIN(n,256u);i++) if(candidates[i]->line==definition->line && !strcmp(candidates[i]->path,definition->path)) {found[0]=candidates[i];count=1;compiler_choice=TRUE;break;}}
  if(usr && !compiler_choice) {const InsightSymbol *target=NULL;if(insight_index_query(semantic->relations,usr,TRUE,NULL,"@clangtarget",preferred,0,&target,1)>0 && target->scope && *target->scope) {static InsightSymbol resolved;resolved=(InsightSymbol){target->type,target->scope,"",target->inheritance,target->line,"","",""};found[0]=&resolved;count=1;compiler_choice=TRUE;}}
  g_free(usr);
 }

 for(size_t i=0;i<count && i<256;i++) {
  const InsightSymbol *tag=found[i]; if(!definition_language(preferred,reference_type,tag->path)) continue; if(!compiler_choice && (python && !member && !imported ? insight_native_python_rank(wb.index,tag,preferred,scope,reference_line) : insight_native_definition_rank_at(tag,preferred,scope,reference_line))<0) continue; gboolean duplicate=FALSE;
  for(guint j=0;j<wb.definitions->len;j++) { Location *old=g_ptr_array_index(wb.definitions,j); if(old->line==tag->line && !g_strcmp0(old->path,tag->path)) { duplicate=TRUE; break; } }
  if(duplicate) continue;
  Location *location=g_new0(Location,1); location->path=g_strdup(tag->path); location->line=tag->line;
  gchar *label=g_strdup_printf("%s %s%s • %s — %s:%lu",tag->type,tag->name,tag->signature,tag->scope,tag->path,tag->line);
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.choices),label); g_free(label);
  if(!chosen || (python && !member && !imported ? insight_native_python_rank(wb.index,tag,preferred,scope,reference_line) : insight_native_definition_rank_at(tag,preferred,scope,reference_line))>(python && !member && !imported ? insight_native_python_rank(wb.index,chosen,preferred,scope,reference_line) : insight_native_definition_rank_at(chosen,preferred,scope,reference_line))) { chosen=tag; selected=(gint)wb.definitions->len; }
  g_ptr_array_add(wb.definitions,location);
 }
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.choices),wb.definitions->len ? selected : -1);
 wb.populating=FALSE; g_free(scope); g_free(owner); return chosen;
}
static char *read_disk_source_full(const char *utf8,gboolean decode,gint preferred,GCancellable *cancel) {
 gchar *native=utils_get_locale_from_utf8(utf8); GFile *file=g_file_new_for_path(native); g_free(native);
 GFileInputStream *stream=g_file_read(file,cancel,NULL); g_object_unref(file);
 if(!stream) return NULL;
 GString *text=g_string_sized_new(16384); char chunk[16384]; gboolean ok=TRUE;
 for(;;) {
  gssize got=g_input_stream_read(G_INPUT_STREAM(stream),chunk,sizeof chunk,cancel,NULL);
  if(got<0 || (got>0 && text->len+(gsize)got>2u*1024u*1024u)) { ok=FALSE; break; }
  if(!got) break;
  g_string_append_len(text,chunk,got);
 }
 g_object_unref(stream);
 if(!ok) { g_string_free(text,TRUE); return NULL; }
 gsize length=text->len; gchar *buffer=g_string_free(text,FALSE);
 if(decode) {
  gboolean nul=FALSE;
  if(!(preferred==G_MININT ? encodings_convert_to_utf8_auto(&buffer,&length,NULL,NULL,NULL,&nul,NULL) : encodings_convert_to_utf8_worker(&buffer,&length,preferred,&nul)) || nul) { g_free(buffer); return NULL; }
 }
 if(length>2u*1024u*1024u || !g_utf8_validate(buffer,(gssize)length,NULL) || memchr(buffer,0,length)) { g_free(buffer); return NULL; }
 return buffer;
}
static char *read_disk_source(const char *utf8,gboolean decode) { return read_disk_source_full(utf8,decode,G_MININT,NULL); }
static void remember_location(const char *path,guint id,gulong line) {
 if(!wb.history || !line) return;
 if(wb.history_position>=0 && wb.history_position<(gint)wb.history->len) {
  Location *old=g_ptr_array_index(wb.history,(guint)wb.history_position);
  if(old->id==id && old->line==line && !g_strcmp0(old->path,path)) return;
 }
 while(wb.history->len && (gint)wb.history->len>wb.history_position+1) g_ptr_array_remove_index(wb.history,wb.history->len-1);
 Location *next=g_new0(Location,1); next->path=g_strdup(path); next->id=id; next->line=line;
 g_ptr_array_add(wb.history,next);
 if(wb.history->len>64) g_ptr_array_remove_index(wb.history,0);
 wb.history_position=(gint)wb.history->len-1;
 navigation_state();
}
static void reveal_context(void) {
 if(!wb.box || !wb.context_visible) return;
 if(gtk_widget_get_parent(wb.box)==main_widgets.message_window_notebook) {
  GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(main_widgets.window));if(focus) g_object_ref(focus);
  if(!gtk_widget_get_visible(main_widgets.message_window_notebook)) msgwin_show_hide(TRUE);
  gint page=gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.box);
  if(gtk_notebook_get_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook))!=page) gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),page);
  if(focus) { if(gtk_window_get_focus(GTK_WINDOW(main_widgets.window))!=focus && gtk_widget_get_mapped(focus) && gtk_widget_get_sensitive(focus)) gtk_widget_grab_focus(focus);g_object_unref(focus); }
 }
}
typedef struct { gchar *path,*text,*digest; guint id; gint encoding,length; guint64 epoch,generation,text_version; } FilePreviewJob;
static void request_file_preview(const char *);
static void stop_file_preview(void) { wb.preview_generation++; if(wb.preview_worker) { g_cancellable_cancel(wb.preview_worker); g_clear_object(&wb.preview_worker); } }
static void file_preview_free(gpointer data) { FilePreviewJob *j=data; g_free(j->path); g_free(j->text); g_free(j->digest); g_free(j); }
static void file_preview_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; FilePreviewJob *j=data;
 if(!j->id && !g_cancellable_is_cancelled(cancel)) j->text=read_disk_source_full(j->path,TRUE,j->encoding,cancel);
 if(j->text && !g_cancellable_is_cancelled(cancel) && g_utf8_validate(j->text,-1,NULL) && (!j->id || !memchr(j->text,0,(gsize)j->length))) j->digest=source_digest(j->text);
 g_task_return_boolean(task,TRUE);
}
static void file_preview_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); FilePreviewJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.preview_generation) return;
 g_clear_object(&wb.preview_worker); if(!ok) return;
 GeanyDocument *doc=document_find_by_filename(j->path);
 if((doc && (doc->id!=j->id || selection_state(doc->editor->sci)->text_version!=j->text_version)) || (!doc && j->id)) { request_file_preview(j->path); return; }
 if(!j->digest) { render_source("",NULL,1,NULL); gtk_label_set_text(GTK_LABEL(wb.title),"Context unavailable: requires a decodable text source up to 2 MiB"); g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; preview_links();navigation_state(); return; }
 if(wb.source_line) remember_location(wb.source_path,wb.source_id,(gulong)sci_get_current_line(wb.preview)+1);
 render_source(j->text,doc ? doc->file_type : preview_filetype(j->path),1,j->path);
 g_free(wb.source_digest); wb.source_digest=g_strdup(j->digest); g_free(wb.source_path); wb.source_path=g_strdup(j->path); wb.source_id=doc ? doc->id : 0; wb.source_line=1;preview_links();
 gchar *label=g_strdup_printf("Context — %s:1",j->path); gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label); remember_location(wb.source_path,wb.source_id,1); navigation_state();
}
static void request_file_preview(const char *path) {
 stop_file_preview(); FilePreviewJob *j=g_new0(FilePreviewJob,1); j->path=g_strdup(path); j->encoding=file_prefs.default_open_encoding; j->epoch=workbench_epoch; j->generation=wb.preview_generation;
 GeanyDocument *doc=document_find_by_filename(path);
 if(doc) { j->id=doc->id; j->text_version=selection_state(doc->editor->sci)->text_version; j->length=sci_get_length(doc->editor->sci); if(j->length>=0 && j->length<=2*1024*1024) j->text=sci_get_contents(doc->editor->sci,j->length+1); }
 wb.preview_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.preview_worker,file_preview_done,NULL); g_task_set_task_data(task,j,file_preview_free); g_task_run_in_thread(task,file_preview_run); g_object_unref(task);
}
static gboolean show_location(const char *path,guint id,gulong line,gboolean remember) {
 stop_file_preview();
 if(wb.context_toggle && !wb.context_visible) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),TRUE);
 GeanyDocument *doc=id ? document_find_by_id(id) : (path ? document_find_by_filename(path) : NULL);
 char *text=NULL; GeanyFiletype *ft=NULL;
 if(doc) {
  gint length=sci_get_length(doc->editor->sci);
  if(length>=0 && length<=2*1024*1024) text=sci_get_contents(doc->editor->sci,length+1);
  if(text && (!g_utf8_validate(text,length,NULL) || memchr(text,0,(gsize)length))) g_clear_pointer(&text,g_free);
  ft=doc->file_type;
 } else if(path) { text=read_disk_source(path,TRUE); ft=preview_filetype(path); }
 if(!text) {
  render_source("",NULL,1,NULL); gtk_label_set_text(GTK_LABEL(wb.title),"Context unavailable: requires a decodable text source up to 2 MiB");
  g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; preview_links();navigation_state(); return FALSE;
 }
 gchar *owned_path=g_strdup(path ? path : (doc ? doc->file_name : NULL));
 size_t begin,end,target_begin,target_end; unsigned long first;
 gboolean valid=insight_context_range(text,strlen(text),line,0,&begin,&end,&first,&target_begin,&target_end);
 if(remember && valid && wb.source_line) remember_location(wb.source_path,wb.source_id,(gulong)sci_get_current_line(wb.preview)+1);
 render_source(text,ft,valid ? line : 1,path ? path : (doc ? doc->file_name : NULL));
 g_free(wb.source_digest); wb.source_digest=source_digest(text); g_free(text);
 if(!valid) line=0;
 g_free(wb.source_path); wb.source_path=owned_path; wb.source_id=doc ? doc->id : 0; wb.source_line=line;preview_links();
 gchar *label=g_strdup_printf("Context — %s:%lu%s",owned_path ? owned_path : "untitled",line,line ? "" : " • definition line changed; reindex");
 gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label);
 if(remember) remember_location(wb.source_path,wb.source_id,line);
 navigation_state();
 return valid;
}
static void definition_changed(GtkComboBox *combo,gpointer data) {
 (void)data; if(wb.populating || !wb.definitions) return;
 gint active=gtk_combo_box_get_active(combo);
 if(active<0 || active>=(gint)wb.definitions->len) return;
 Location *location=g_ptr_array_index(wb.definitions,(guint)active);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 show_location(location->path,location->id,location->line,TRUE);
}
enum { SYMBOL_NAME,SYMBOL_SCOPE,SYMBOL_PATH,SYMBOL_LINE,SYMBOL_KIND,SYMBOL_COLUMNS };
static InsightLookupFilter *entry_filter(GtkWidget *entry,gboolean files) {
 GError *error=NULL; InsightLookupFilter *filter=insight_lookup_filter_new(gtk_entry_get_text(GTK_ENTRY(entry)),files,&error);
 gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry),GTK_ENTRY_ICON_SECONDARY,error ? "dialog-warning" : (*gtk_entry_get_text(GTK_ENTRY(entry)) ? "edit-clear-symbolic" : NULL));
 gtk_entry_set_icon_tooltip_text(GTK_ENTRY(entry),GTK_ENTRY_ICON_SECONDARY,error ? error->message : NULL);
 g_clear_error(&error); return filter;
}
typedef struct { gchar *name,*path,*directory; guint64 size; gboolean stat_valid; } FileListRow;
static void file_list_row_free(gpointer data) { FileListRow *r=data; g_free(r->name); g_free(r->path); g_free(r->directory); g_free(r); }
typedef struct { guint64 generation; GCancellable *cancel; guint apply; InsightIndex *index; const InsightSymbol **rows; GPtrArray *files; gchar *local_path; guint count,position; gboolean local,collecting,iter_valid,has_more; GtkTreeIter iter; GHashTable *existing; } SymbolListState;
typedef struct { GWeakRef store; InsightIndex *index; gchar *query,*path; const InsightSymbol **rows; GPtrArray *paths,*files,*tags,*outlines;gint lang,sort_column;GtkSortType sort_direction;guint type_mask; guint count,total,limit; guint64 generation,epoch; gboolean local,has_more;InsightLanguageFilter *language; } SymbolListJob;
static GtkWidget *symbol_list_view(GtkListStore *store) { return store==wb.file_store ? wb.file_view : (store==wb.local_symbols ? wb.local_view : wb.symbol_view); }
static void symbol_iter_queue_free(gpointer data) { GQueue *q=data; g_queue_free_full(q,g_free); }
static gchar *symbol_row_key(GtkTreeModel *model,GtkTreeIter *iter,gboolean files) {
 gchar *name=NULL,*path=NULL,*scope=NULL,*kind=NULL; gulong line=0;
 if(files) gtk_tree_model_get(model,iter,0,&name,1,&path,-1);
 else gtk_tree_model_get(model,iter,SYMBOL_NAME,&name,SYMBOL_SCOPE,&scope,SYMBOL_PATH,&path,SYMBOL_LINE,&line,SYMBOL_KIND,&kind,-1);
 /* Length-prefixed fields avoid collisions for paths containing separators. */
 GString *key=g_string_new(NULL); const gchar *fields[]={name,path,scope,kind};
 for(guint i=0;i<G_N_ELEMENTS(fields);i++) { const gchar *v=fields[i] ? fields[i] : ""; g_string_append_printf(key,"%" G_GSIZE_FORMAT ":%s",strlen(v),v); }
 g_string_append_printf(key,":%lu",line); g_free(name); g_free(path); g_free(scope); g_free(kind); return g_string_free(key,FALSE);
}
static void symbol_list_stop(SymbolListState *s) {
 s->generation++; if(s->cancel) { g_cancellable_cancel(s->cancel); g_clear_object(&s->cancel); }
 if(s->apply) { g_source_remove(s->apply); s->apply=0; }
 retire_index(s->index); s->index=NULL; g_clear_pointer(&s->rows,g_free); g_clear_pointer(&s->files,g_ptr_array_unref); g_clear_pointer(&s->local_path,g_free); s->count=s->position=0; g_clear_pointer(&s->existing,g_hash_table_destroy); s->collecting=s->iter_valid=FALSE;
}
static void symbol_list_state_free(gpointer data) { SymbolListState *s=data; symbol_list_stop(s); g_free(s); }
static SymbolListState *symbol_list_state(GtkListStore *store) {
 SymbolListState *s=g_object_get_data(G_OBJECT(store),"insight-symbol-worker"); if(!s) { s=g_new0(SymbolListState,1); g_object_set_data_full(G_OBJECT(store),"insight-symbol-worker",s,symbol_list_state_free); } return s;
}
static gboolean symbol_list_apply(gpointer data) {
 GtkListStore *store=data; SymbolListState *s=symbol_list_state(store); gint64 deadline=g_get_monotonic_time()+3000;
 if(!wb.alive) { s->apply=0; return G_SOURCE_REMOVE; }
 if(s->collecting) {
  while(s->iter_valid) {
   gchar *key=symbol_row_key(GTK_TREE_MODEL(store),&s->iter,s->files!=NULL);
   GQueue *q=g_hash_table_lookup(s->existing,key);
   if(!q) { q=g_queue_new(); g_hash_table_insert(s->existing,key,q); } else g_free(key);
   GtkTreeIter *copy=g_new(GtkTreeIter,1); *copy=s->iter; g_queue_push_tail(q,copy);
   s->iter_valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&s->iter);
   if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
  }
  s->collecting=FALSE;
 }
 while(s->position<s->count) {
  guint n=s->position++; GtkTreeIter iter;
  /* Build the same key without inserting a temporary row into the model. */
  const InsightSymbol *tag=s->files ? NULL : s->rows[n];
  FileListRow *file=s->files ? g_ptr_array_index(s->files,n) : NULL;
  const gchar *fields[]={file ? file->name : tag->name,file ? file->path : (s->local ? s->local_path : tag->path),tag ? tag->scope : NULL,tag ? tag->kind : NULL};
  GString *key=g_string_new(NULL);
  for(guint k=0;k<G_N_ELEMENTS(fields);k++) { const gchar *v=fields[k] ? fields[k] : ""; g_string_append_printf(key,"%" G_GSIZE_FORMAT ":%s",strlen(v),v); }
  g_string_append_printf(key,":%lu",tag ? tag->line : 0);
  GQueue *q=g_hash_table_lookup(s->existing,key->str); GtkTreeIter *old=q ? g_queue_pop_head(q) : NULL;
  if(old) { iter=*old; g_free(old); if(file) gtk_list_store_set(store,&iter,2,file->directory,3,file->size,4,file->stat_valid,-1); } /* Preserve the selected row while refreshing metadata. */
  else {
   gtk_list_store_append(store,&iter);
   if(file) gtk_list_store_set(store,&iter,0,file->name,1,file->path,2,file->directory,3,file->size,4,file->stat_valid,-1);
   else gtk_list_store_set(store,&iter,SYMBOL_NAME,tag->name,SYMBOL_SCOPE,tag->scope,SYMBOL_PATH,s->local ? s->local_path : tag->path,SYMBOL_LINE,(gulong)tag->line,SYMBOL_KIND,tag->kind,-1);
  }
  g_string_free(key,TRUE);
  if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 /* Delete only obsolete rows. The iter remains valid when the model is sorted. */
 GHashTableIter pending; gpointer value; g_hash_table_iter_init(&pending,s->existing);
 while(g_hash_table_iter_next(&pending,NULL,&value)) {
  GQueue *q=value; GtkTreeIter *old;
  while((old=g_queue_pop_head(q))) { gtk_list_store_remove(store,old); g_free(old); if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE; }
  g_hash_table_iter_remove(&pending);
 }
 g_clear_pointer(&s->existing,g_hash_table_destroy);
 GtkWidget *view=symbol_list_view(store); if(view) { insight_symbol_view_page_state(GTK_TREE_VIEW(view),s->has_more); gtk_widget_set_sensitive(view,TRUE); insight_symbol_view_loading(GTK_TREE_VIEW(view),FALSE); }
 if(store==wb.file_store && wb.file_status) { gchar *text=insight_i18n_printf(s->has_more ? "Files: %u+" : "Files: %u",s->count);gtk_label_set_text(GTK_LABEL(wb.file_status),text);g_free(text);g_object_set_data(G_OBJECT(wb.file_status),"insight-files-count",GUINT_TO_POINTER(s->count));g_object_set_data(G_OBJECT(wb.file_status),"insight-files-more",GINT_TO_POINTER(s->has_more)); }
 s->apply=0; retire_index(s->index); s->index=NULL; g_clear_pointer(&s->rows,g_free); g_clear_pointer(&s->files,g_ptr_array_unref); return G_SOURCE_REMOVE;
}
static void symbol_list_job_free(gpointer data) { SymbolListJob *j=data;insight_language_filter_free(j->language); g_weak_ref_clear(&j->store); retire_index(j->index); g_free(j->query); g_free(j->path); g_free(j->rows); g_clear_pointer(&j->paths,g_ptr_array_unref); g_clear_pointer(&j->files,g_ptr_array_unref); g_clear_pointer(&j->tags,g_ptr_array_unref);g_clear_pointer(&j->outlines,g_ptr_array_unref); g_free(j); }
static gint virtual_compare(gconstpointer aa,gconstpointer bb,gpointer data) {
 SymbolListJob *j=data;gint result=0;
 if(j->paths) {
  const FileListRow *a=*(FileListRow *const *)aa,*b=*(FileListRow *const *)bb;
  if(j->sort_column==3) result=(a->size>b->size)-(a->size<b->size);
  else result=g_utf8_collate(j->sort_column==1?a->path:j->sort_column==2?a->directory:a->name,j->sort_column==1?b->path:j->sort_column==2?b->directory:b->name);
  if(!result) result=g_strcmp0(a->path,b->path);
 }else {
  const InsightSymbol *a=*(const InsightSymbol *const *)aa,*b=*(const InsightSymbol *const *)bb;
  if(j->sort_column==SYMBOL_LINE) result=(a->line>b->line)-(a->line<b->line);
  else {const gchar *av=j->sort_column==SYMBOL_SCOPE?a->scope:j->sort_column==SYMBOL_PATH?a->path:j->sort_column==SYMBOL_KIND?a->kind:a->name;
   const gchar *bv=j->sort_column==SYMBOL_SCOPE?b->scope:j->sort_column==SYMBOL_PATH?b->path:j->sort_column==SYMBOL_KIND?b->kind:b->name;
   result=g_utf8_collate(av?av:"",bv?bv:"");}
  if(!result) result=g_strcmp0(a->path,b->path);
  if(!result) result=(a->line>b->line)-(a->line<b->line);
 }
 return j->sort_direction==GTK_SORT_DESCENDING?-result:result;
}
