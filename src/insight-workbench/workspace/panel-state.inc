/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Panel state, declarations and utility functions.
 * RU: Состояние панелей, объявления и служебные функции. */

static void file_action(guint);
static gboolean file_popup_key(GtkWidget *,gpointer);
static void symbol_action(GtkTreeView *,guint);
static void browser_symbol_info(GtkWidget *,gpointer);
static gboolean symbol_popup_key(GtkWidget *,gpointer);
static void set_usage_status(const char *text) {
 GtkWidget *label=wb.usage_mode==3 && wb.search_status ? wb.search_status : wb.usage_status;
 gtk_label_set_text(GTK_LABEL(label),text);
}
/* Private decoration slots; Geany's error/search/snippet indicators retain
 * their original indices and styles. */
enum { INSIGHT_HDL_COLOR=24, INSIGHT_MATCH_FILL=28, INSIGHT_MATCH_FRAME=29, INSIGHT_SAME_TEXT=30, INSIGHT_LINK=31 };
static void preview_links(void);
static void decorate_setup(ScintillaObject *sci) {
 SSM(sci,SCI_INDICSETSTYLE,INSIGHT_MATCH_FILL,INDIC_STRAIGHTBOX); SSM(sci,SCI_INDICSETFORE,INSIGHT_MATCH_FILL,0x00ffff);
 SSM(sci,SCI_INDICSETALPHA,INSIGHT_MATCH_FILL,110); SSM(sci,SCI_INDICSETOUTLINEALPHA,INSIGHT_MATCH_FILL,0);
 SSM(sci,SCI_INDICSETSTYLE,INSIGHT_MATCH_FRAME,INDIC_BOX); SSM(sci,SCI_INDICSETFORE,INSIGHT_MATCH_FRAME,0x0000ff);
 SSM(sci,SCI_INDICSETSTROKEWIDTH,INSIGHT_MATCH_FRAME,100); SSM(sci,SCI_INDICSETUNDER,INSIGHT_MATCH_FRAME,FALSE);
 SSM(sci,SCI_INDICSETSTYLE,INSIGHT_SAME_TEXT,INDIC_STRAIGHTBOX); SSM(sci,SCI_INDICSETFORE,INSIGHT_SAME_TEXT,0x00ffff);
 SSM(sci,SCI_INDICSETALPHA,INSIGHT_SAME_TEXT,90); SSM(sci,SCI_INDICSETOUTLINEALPHA,INSIGHT_SAME_TEXT,0); SSM(sci,SCI_INDICSETUNDER,INSIGHT_SAME_TEXT,TRUE);
}
static void decorate_match(ScintillaObject *sci,gsize begin,gsize end);
typedef struct {
 guint timer,apply; guint64 generation,text_version,requested_version;
 gint begin,end,length; gboolean alive,highlighted;
 gsize match_begin,match_end; guint64 match_version; gboolean match_ready;
 GCancellable *cancel; InsightSearchMatch *matches; gsize count,position;
} SelectionState;
typedef struct { GWeakRef sci; gchar *text,*query; InsightSearchMatch *matches; gsize count; guint64 generation; } SelectionJob;
static void selection_stop(SelectionState *s) {
 s->generation++;
 if(s->timer) { g_source_remove(s->timer); s->timer=0; }
 if(s->apply) { g_source_remove(s->apply); s->apply=0; }
 if(s->cancel) { g_cancellable_cancel(s->cancel); g_clear_object(&s->cancel); }
 g_clear_pointer(&s->matches,g_free); s->count=s->position=0;
}
static void selection_state_free(gpointer data) { SelectionState *s=data; selection_stop(s); g_free(s); }
static void selection_destroy(GtkWidget *widget,gpointer data) { (void)widget; SelectionState *s=data; s->alive=FALSE; selection_stop(s); }
static void selection_modified(ScintillaObject *sci,gint message,SCNotification *n,gpointer data) {
 (void)sci; (void)message; SelectionState *s=data;
 if(n->nmhdr.code==SCN_MODIFIED && (n->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) { s->text_version++; selection_stop(s); }
}
static SelectionState *selection_state(ScintillaObject *sci) {
 SelectionState *s=g_object_get_data(G_OBJECT(sci),"insight-selection-worker");
 if(!s) { s=g_new0(SelectionState,1); s->alive=TRUE; s->begin=s->end=-1; g_object_set_data_full(G_OBJECT(sci),"insight-selection-worker",s,selection_state_free); g_signal_connect(sci,"sci-notify",G_CALLBACK(selection_modified),s); g_signal_connect(sci,"destroy",G_CALLBACK(selection_destroy),s); }
 return s;
}
static void decorate_match(ScintillaObject *sci,gsize begin,gsize end) {
 SelectionState *state=selection_state(sci);
 if(state->match_ready && state->match_begin==begin && state->match_end==end && state->match_version==state->text_version) return;
 state->match_ready=TRUE;state->match_begin=begin;state->match_end=end;state->match_version=state->text_version;
 decorate_setup(sci);
 /* The normal blue selection must not tint a selected search hit green. */
 SSM(sci,SCI_SETSELBACK,TRUE,0x00ffff); SSM(sci,SCI_SETSELFORE,FALSE,0); SSM(sci,SCI_SETSELALPHA,SC_ALPHA_NOALPHA,0);
 for(gint indicator=INSIGHT_MATCH_FILL;indicator<=INSIGHT_MATCH_FRAME;indicator++) {
  SSM(sci,SCI_SETINDICATORCURRENT,indicator,0); SSM(sci,SCI_INDICATORCLEARRANGE,0,sci_get_length(sci));
  if(end>begin && end<=(gsize)sci_get_length(sci)) SSM(sci,SCI_INDICATORFILLRANGE,begin,end-begin);
 }
}
static gboolean selection_apply(gpointer data) {
 ScintillaObject *sci=data; SelectionState *s=selection_state(sci); gint64 deadline=g_get_monotonic_time()+3000;
 if(!s->alive) { s->apply=0; return G_SOURCE_REMOVE; }
 SSM(sci,SCI_SETINDICATORCURRENT,INSIGHT_SAME_TEXT,0);
 while(s->position<s->count) { InsightSearchMatch *hit=&s->matches[s->position++]; SSM(sci,SCI_INDICATORFILLRANGE,hit->start,hit->end-hit->start); s->highlighted=TRUE; if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE; }
 s->apply=0; g_clear_pointer(&s->matches,g_free); return G_SOURCE_REMOVE;
}
static void selection_job_free(gpointer data) { SelectionJob *j=data; g_weak_ref_clear(&j->sci); g_free(j->text); g_free(j->query); g_free(j->matches); g_free(j); }
static void selection_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; SelectionJob *j=data; j->matches=g_new(InsightSearchMatch,10000); gsize total=0;
 if(!g_cancellable_is_cancelled(cancel) && insight_context_collect(j->text,strlen(j->text),j->query,INSIGHT_SEARCH_CASE|INSIGHT_SEARCH_WORD,j->matches,10000,&total,NULL)) j->count=MIN(total,10000);
 g_task_return_boolean(task,TRUE);
}
static void selection_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); SelectionJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL); ScintillaObject *sci=g_weak_ref_get(&j->sci); if(!sci) return;
 SelectionState *s=selection_state(sci);
 if(ok && s->alive && s->generation==j->generation) {
  g_clear_object(&s->cancel); s->matches=j->matches; j->matches=NULL; s->count=j->count; s->position=0;
  s->apply=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,selection_apply,g_object_ref(sci),g_object_unref);
 }
 g_object_unref(sci);
}
static gboolean selection_timeout(gpointer data) {
 ScintillaObject *sci=data; SelectionState *s=selection_state(sci); s->timer=0;
 if(!s->alive) return G_SOURCE_REMOVE;
 SelectionJob *j=g_new0(SelectionJob,1); g_weak_ref_init(&j->sci,sci); j->generation=s->generation; j->text=sci_get_contents(sci,s->length+1); j->query=g_strndup(j->text+s->begin,(gsize)(s->end-s->begin));
 s->cancel=g_cancellable_new(); GTask *task=g_task_new(NULL,s->cancel,selection_job_done,NULL); g_task_set_task_data(task,j,selection_job_free); g_task_run_in_thread(task,selection_job_run); g_object_unref(task); return G_SOURCE_REMOVE;
}
static void decorate_selection(ScintillaObject *sci) {
 SelectionState *s=selection_state(sci); gint length=sci_get_length(sci),begin=(gint)SSM(sci,SCI_GETSELECTIONSTART,0,0),end=(gint)SSM(sci,SCI_GETSELECTIONEND,0,0);
 if(!s->alive || (s->length==length && s->begin==begin && s->end==end && s->requested_version==s->text_version)) return;
 selection_stop(s); s->length=length; s->begin=begin; s->end=end; s->requested_version=s->text_version;
 if(s->highlighted) { SSM(sci,SCI_SETINDICATORCURRENT,INSIGHT_SAME_TEXT,0); SSM(sci,SCI_INDICATORCLEARRANGE,0,MAX(0,length)); s->highlighted=FALSE; }
 if(length<0 || length>2*1024*1024 || begin<0 || end>length || end<=begin || end-begin>1024) return;
 decorate_setup(sci); s->timer=g_timeout_add_full(G_PRIORITY_DEFAULT,150,selection_timeout,g_object_ref(sci),g_object_unref);
}
static void editor_mark_symbol(GeanyDocument *doc,gulong line,const gchar *name) {
 ScintillaObject *sci=doc->editor->sci; SSM(sci,SCI_ENSUREVISIBLE,line-1,0);SSM(sci,SCI_GOTOLINE,line-1,0);
 gchar *text=sci_get_line(sci,(gint)line-1);InsightSearchMatch match;gsize total=0;gint start=sci_get_position_from_line(sci,(gint)line-1);
 if(name && *name && insight_context_collect(text,strlen(text),name,INSIGHT_SEARCH_CASE|INSIGHT_SEARCH_WORD,&match,1,&total,NULL) && total) { SSM(sci,SCI_SETSEL,start+match.start,start+match.end);decorate_match(sci,start+match.start,start+match.end); }else decorate_match(sci,0,0);g_free(text);
}
static void preview_mark_symbol(const char *name) {
 if(!name || !*name) return;
 gint line=sci_get_current_line(wb.preview),begin=sci_get_position_from_line(wb.preview,line); gchar *text=sci_get_line(wb.preview,line);
 InsightSearchMatch match; gsize total=0;
 if(insight_context_collect(text,strlen(text),name,INSIGHT_SEARCH_CASE|INSIGHT_SEARCH_WORD,&match,1,&total,NULL) && total) { SSM(wb.preview,SCI_SETSEL,(gsize)begin+match.start,(gsize)begin+match.end); decorate_match(wb.preview,(gsize)begin+match.start,(gsize)begin+match.end); }
 g_free(text);
}
static gchar *source_digest(const char *text) {
 /* Geany can remove a UTF-8 BOM and normalize CRLF while opening a file.
  * Compare logical source content; navigation here uses line numbers. */
 if(g_str_has_prefix(text,"\357\273\277")) text+=3;
 GString *normalized=g_string_sized_new(strlen(text));
 for(const char *p=text;*p;p++) if(*p!='\r' || p[1]!='\n') g_string_append_c(normalized,*p);
 gchar *digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,normalized->str,(gssize)normalized->len);
 g_string_free(normalized,TRUE); return digest;
}
static void editor_focus(void) {
 GeanyDocument *doc=document_get_current(); if(doc) document_grab_focus(doc);
}
static gboolean bookmark_key(GtkWidget *,GdkEventKey *,gpointer);
static void browser_activated(GtkTreeView *,GtkTreePath *,GtkTreeViewColumn *,gpointer);
static gboolean browser_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data;
 if(widget==wb.file_view) {
  guint modifiers=event->state&gtk_accelerator_get_default_mod_mask(),key=gdk_keyval_to_lower(event->keyval);
  if((key==GDK_KEY_Menu && !modifiers) || (key==GDK_KEY_F10 && modifiers==GDK_SHIFT_MASK)) return file_popup_key(widget,NULL);
  if(key==GDK_KEY_f && modifiers==GEANY_PRIMARY_MOD_MASK) { gtk_widget_grab_focus(wb.file_entry);return TRUE; }
  if(key==GDK_KEY_c && (modifiers==GEANY_PRIMARY_MOD_MASK || modifiers==(GEANY_PRIMARY_MOD_MASK|GDK_SHIFT_MASK))) { file_action(modifiers&GDK_SHIFT_MASK ? FP_PATH : FP_NAME);return TRUE; }
  if((key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) && modifiers==GEANY_PRIMARY_MOD_MASK) { file_action(FP_OPEN);return TRUE; }
 }
 if(widget==wb.symbol_view || widget==wb.local_view) {
  guint modifiers=event->state&gtk_accelerator_get_default_mod_mask(); guint key=gdk_keyval_to_lower(event->keyval);
  if((key==GDK_KEY_Menu && !modifiers) || (key==GDK_KEY_F10 && modifiers==GDK_SHIFT_MASK)) return symbol_popup_key(widget,NULL);
  if(key==GDK_KEY_f && modifiers==GEANY_PRIMARY_MOD_MASK) { gtk_widget_grab_focus(widget==wb.local_view ? wb.local_entry : wb.symbol_entry); return TRUE; }
  if(!modifiers && (key==GDK_KEY_space || key==GDK_KEY_Return || key==GDK_KEY_KP_Enter)) { GtkTreeModel *model=NULL;GtkTreeIter iter;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)),&model,&iter)) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter);browser_activated(GTK_TREE_VIEW(widget),row,NULL,NULL);gtk_tree_path_free(row); }return TRUE; }
  if(key==GDK_KEY_c && (modifiers==GEANY_PRIMARY_MOD_MASK || modifiers==(GEANY_PRIMARY_MOD_MASK|GDK_SHIFT_MASK))) { symbol_action(GTK_TREE_VIEW(widget),modifiers&GDK_SHIFT_MASK ? SP_LOCATION : SP_NAME); return TRUE; }
  if((key==GDK_KEY_Return || key==GDK_KEY_KP_Enter) && (modifiers==GEANY_PRIMARY_MOD_MASK || modifiers==GDK_MOD1_MASK)) { symbol_action(GTK_TREE_VIEW(widget),modifiers==GDK_MOD1_MASK ? SP_INFO : SP_OPEN); return TRUE; }
 }
 if(event->keyval==GDK_KEY_Escape) { editor_focus(); return TRUE; }
 if(event->keyval==GDK_KEY_Down && (widget==wb.symbol_entry || widget==wb.file_entry || widget==wb.local_entry)) {
  GtkWidget *view=widget==wb.local_entry ? wb.local_view : (widget==wb.symbol_entry ? wb.symbol_view : wb.file_view);
  if(!gtk_widget_get_sensitive(view)) return TRUE;
  GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view)); GtkTreeIter iter;
  if(gtk_tree_model_get_iter_first(model,&iter)) {
   GtkTreeIter child; while(gtk_tree_model_iter_children(model,&child,&iter)) iter=child;
   GtkTreePath *path=gtk_tree_model_get_path(model,&iter);
   gtk_tree_view_expand_to_path(GTK_TREE_VIEW(view),path); gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE); gtk_tree_path_free(path); gtk_widget_grab_focus(view);
  }
  return TRUE;
 }
 return FALSE;
}
static void navigation_state(void) {
 if(wb.back) gtk_widget_set_sensitive(wb.back,wb.history_position>0);
 if(wb.forward) gtk_widget_set_sensitive(wb.forward,wb.history && wb.history_position+1<(gint)wb.history->len);
 if(wb.open) gtk_widget_set_sensitive(wb.open,wb.source_line!=0);
}
/* Keep the lexer and language profile stable while following the same source. */
static GeanyFiletype *preview_filetype(const gchar *path) {
 GeanyFiletype *ft=path ? insight_languages_detect(path) : NULL;
 return ft ? ft : (path ? filetypes_detect_from_file(path) : NULL);
}
static void render_source(const char *text,GeanyFiletype *filetype,gulong line,const gchar *path) {
 gchar *previous=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1);
 gboolean unchanged=!strcmp(previous,text);
 gboolean restyle=!wb.preview_styles_ready || wb.preview_filetype!=filetype || g_strcmp0(wb.source_path,path);
 g_free(previous);
 if(restyle) {
  wb.preview_filetype=filetype;wb.preview_styles_ready=TRUE;wb.preview_links_valid=FALSE;
  highlighting_set_styles(wb.preview,filetype ? filetype : filetypes[GEANY_FILETYPES_NONE]);
  insight_languages_apply(wb.preview,path,filetype);
 }
 if(!unchanged) {
  sci_set_readonly(wb.preview,FALSE);sci_set_text(wb.preview,text);
  sci_set_line_numbers(wb.preview,TRUE);
  SSM(wb.preview,SCI_SETCARETLINEVISIBLE,TRUE,0);
  SSM(wb.preview,SCI_EMPTYUNDOBUFFER,0,0);sci_set_readonly(wb.preview,TRUE);
  decorate_match(wb.preview,0,0);
 }
 /* Colour the first chunk before symbol-link indexing, even for a large index.
  * Scintilla styles the remaining visible range on demand. */
 if(restyle || !unchanged) SSM(wb.preview,SCI_COLOURISE,0,MIN(sci_get_length(wb.preview),16384));
 gint target=(gint)MIN(line ? line-1 : 0,(gulong)MAX(0,sci_get_line_count(wb.preview)-1));
 if(unchanged && sci_get_current_line(wb.preview)==target) return;
 SSM(wb.preview,SCI_GOTOLINE,target,0);
 SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0);
}
static void refresh_index(void);
static void usage_start(GtkWidget *,gpointer);
static void relation_view_update(void);
static void relation_report_show(GtkWidget *,gpointer);
static void relation_print_show(GtkWidget *,gpointer);
typedef struct { InsightIndex *base,*overlay,*result,*previous; GPtrArray *paths; GHashTable *changed,*names,*all_names; guint64 epoch,generation; gboolean valid; } SnapshotJob;
static void retire_index_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) { (void)source; (void)cancel; insight_index_free(data); g_task_return_boolean(task,TRUE); }
static void retire_index(InsightIndex *index) { if(!index) return; GTask *task=g_task_new(NULL,NULL,NULL,NULL); g_task_set_task_data(task,index,NULL); g_task_run_in_thread(task,retire_index_run); g_object_unref(task); }
static void retire_names_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) { (void)source; (void)cancel; g_hash_table_unref(data); g_task_return_boolean(task,TRUE); }
static void retire_names(GHashTable *names) { if(!names) return; GTask *task=g_task_new(NULL,NULL,NULL,NULL); g_task_set_task_data(task,names,NULL); g_task_run_in_thread(task,retire_names_run); g_object_unref(task); }
static void snapshot_job_free(gpointer data) { SnapshotJob *j=data; retire_index(j->base); retire_index(j->overlay); retire_index(j->result); retire_index(j->previous); retire_names(j->changed); retire_names(j->names); retire_names(j->all_names); g_ptr_array_free(j->paths,TRUE); g_free(j); }
static void mark_index_dirty(void) { wb.index_generation++; wb.index_dirty=TRUE; if(wb.snapshot_worker) { g_cancellable_cancel(wb.snapshot_worker); g_clear_object(&wb.snapshot_worker); } }
/* Worker-owned per-file fingerprints: publication reports only files whose
 * symbol records actually changed, including removals and empty overlays. */
static GHashTable *index_file_fingerprints(const InsightIndex *index) {
 GHashTable *table=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_checksum_free);
 for(gsize i=0;i<insight_index_count(index);i++) {
  const InsightSymbol *s=insight_index_at(index,i); GChecksum *sum=g_hash_table_lookup(table,s->path);
  if(!sum) { sum=g_checksum_new(G_CHECKSUM_SHA256); g_hash_table_insert(table,g_strdup(s->path),sum); }
  const gchar *fields[]={s->name,s->scope,s->kind,s->signature,s->type,s->inheritance};
  for(guint k=0;k<G_N_ELEMENTS(fields);k++) { const gchar *v=fields[k] ? fields[k] : ""; g_checksum_update(sum,(const guchar *)v,strlen(v)+1); }
  gchar line[32]; g_snprintf(line,sizeof line,"%lu",s->line); g_checksum_update(sum,(const guchar *)line,strlen(line)+1);
 }
 return table;
}
static void index_collect_changes(SnapshotJob *j) {
 GHashTable *old=index_file_fingerprints(j->previous),*next=index_file_fingerprints(j->result);
 j->changed=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 GHashTableIter it; gpointer path,value; g_hash_table_iter_init(&it,next);
 while(g_hash_table_iter_next(&it,&path,&value)) { GChecksum *before=g_hash_table_lookup(old,path); if(!before || strcmp(g_checksum_get_string(before),g_checksum_get_string(value))) g_hash_table_add(j->changed,g_strdup(path)); }
 g_hash_table_iter_init(&it,old); while(g_hash_table_iter_next(&it,&path,&value)) if(!g_hash_table_contains(next,path)) g_hash_table_add(j->changed,g_strdup(path));
 j->names=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 j->all_names=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 const InsightIndex *snapshots[]={j->previous,j->result};
 for(guint k=0;k<G_N_ELEMENTS(snapshots);k++) for(gsize n=0;n<insight_index_count(snapshots[k]);n++) {
  const InsightSymbol *tag=insight_index_at(snapshots[k],n);
  if(k==1 && !g_hash_table_contains(j->all_names,tag->name)) g_hash_table_add(j->all_names,g_strdup(tag->name));
  if(g_hash_table_contains(j->changed,tag->path)) g_hash_table_add(j->names,g_strdup(tag->name));
 }
 g_hash_table_destroy(old); g_hash_table_destroy(next);
}
/* Coalesced change event. Consumers read the latest published snapshot;
 * a local-file view is not invalidated by changes to another file. */
static gboolean index_event_dispatch(gpointer data) {
 (void)data; wb.index_event_idle=0; if(!wb.alive) return G_SOURCE_REMOVE;
 GHashTable *paths=wb.index_event_paths,*names=wb.index_event_names; wb.index_event_paths=wb.index_event_names=NULL;
 if(paths && g_hash_table_size(paths)) {
  browser_filter(NULL,NULL); categories_filter(NULL,NULL);
  GeanyDocument *doc=document_get_current();
  if(doc && doc->file_name && g_hash_table_contains(paths,doc->file_name)) local_filter(NULL,NULL);
  wb.preview_links_position=0; preview_links();
  /* Definitions depend on the queried name, not every project file.
   * Owner/inheritance queries retain broader invalidation until their
   * dependency sets are recorded explicitly. Python aliases likewise require
   * broad invalidation until imported-module dependencies are recorded. */
  gint mode=wb.relation_mode ? gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)) : -1;
  const gchar *relation=wb.usage_entry ? gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)) : NULL;
  while(relation && g_str_has_prefix(relation,"::")) relation+=2;
  if(mode!=1 || (relation && names && g_hash_table_contains(names,relation))) wb.relation_dirty=TRUE;
  gchar *query=doc ? cursor_query(doc) : NULL;
  gboolean definition_changed=(doc && doc->file_name && g_hash_table_contains(paths,doc->file_name)) || (query && names && g_hash_table_contains(names,query)) || (doc && python_language(doc->file_type));
  g_free(query);
  if(definition_changed) { wb.follow_doc=0; queue_definition(TRUE); }
  if(wb.relation_dirty || definition_changed) insight_workbench_refresh();
  /* A manual tag query may have completed against the previous snapshot
   * while the asynchronous document overlay was still being published. */
  if(wb.relation_dirty && relation && *relation && (mode==1 || mode==2 || mode==4 || mode==5) &&
     !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow))) { wb.relation_dirty=FALSE;usage_start(NULL,NULL); }
 }
 retire_names(paths);
 retire_names(names);
 return G_SOURCE_REMOVE;
}
