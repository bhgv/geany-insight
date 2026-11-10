/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Relation rows, searches and result controls.
 * RU: Строки связей, поиск и управление результатами. */

static gchar *relation_row_key(const gchar *display,const gchar *path,const gchar *name,const gchar *description,gulong line) {
 GString *key=g_string_new(NULL); const gchar *fields[]={display,path,name,description};
 for(guint k=0;k<G_N_ELEMENTS(fields);k++) {
  if(fields[k]) g_string_append_printf(key,"%" G_GSIZE_FORMAT ":%s",strlen(fields[k]),fields[k]);
  else g_string_append(key,"-1:");
 }
 g_string_append_printf(key,":%lu",line); return g_string_free(key,FALSE);
}
static gchar *relation_existing_key(GtkTreeModel *model,GtkTreeIter *iter) {
 gchar *display=NULL,*path=NULL,*name=NULL,*description=NULL,*digest=NULL; gulong line=0,start=0,end=0,column=0; guint id=0;
 gtk_tree_model_get(model,iter,USAGE_DISPLAY,&display,USAGE_PATH,&path,USAGE_SYMBOL,&name,USAGE_SNIPPET,&description,USAGE_LINE,&line,USAGE_START,&start,USAGE_END,&end,USAGE_ID,&id,USAGE_COLUMN,&column,USAGE_DIGEST,&digest,-1);
 gchar *key=relation_row_key(display,path,name,description,line);
 /* Reference rows also carry hit coordinates/digests and must not be reused
  * as definition rows even when their visible strings happen to coincide. */
 if(start || end || id || column || digest) { gchar *old=key; key=g_strconcat("reference:",old,NULL); g_free(old); }
 g_free(display); g_free(path); g_free(name); g_free(description); g_free(digest); return key;
}
static gboolean relation_results_apply(gpointer data) {
 GTask *task=data; RelationJob *j=g_task_get_task_data(task);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.usage_generation) return G_SOURCE_REMOVE;
 if(j->index_version!=wb.index_version) { wb.usage_idle=0; usage_start(NULL,NULL); return G_SOURCE_REMOVE; }
 gint64 deadline=g_get_monotonic_time()+3000;
 if(j->collecting) {
  while(j->iter_valid) {
   gchar *key=relation_existing_key(GTK_TREE_MODEL(wb.matches),&j->iter); GQueue *q=g_hash_table_lookup(j->existing,key);
   if(!q) { q=g_queue_new(); g_hash_table_insert(j->existing,key,q); } else g_free(key);
   GtkTreeIter *copy=g_new(GtkTreeIter,1); *copy=j->iter; g_queue_push_tail(q,copy);
   j->iter_valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(wb.matches),&j->iter);
   if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
  }
  j->collecting=FALSE;
 }
 while(j->apply_position<MIN(j->total,G_N_ELEMENTS(j->found))) {
  gsize n=j->apply_position++; const InsightSymbol *tag=j->found[n]; const gchar *description=g_ptr_array_index(j->descriptions,n);
  gchar *key=relation_row_key(tag->path,tag->path,tag->name,description,tag->line); GQueue *q=g_hash_table_lookup(j->existing,key); GtkTreeIter *old=q ? g_queue_pop_head(q) : NULL; g_free(key);
  if(old) g_free(old);
  else { GtkTreeIter iter; gtk_list_store_append(wb.matches,&iter); gtk_list_store_set(wb.matches,&iter,USAGE_DISPLAY,tag->path,USAGE_PATH,tag->path,USAGE_LINE,tag->line,USAGE_SYMBOL,tag->name,USAGE_SNIPPET,description,-1); }
  if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 GHashTableIter pending; gpointer value; g_hash_table_iter_init(&pending,j->existing);
 while(g_hash_table_iter_next(&pending,NULL,&value)) {
  GQueue *q=value; GtkTreeIter *old;
  while((old=g_queue_pop_head(q))) { gtk_list_store_remove(wb.matches,old); g_free(old); if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE; }
  g_hash_table_iter_remove(&pending);
 }
 gchar *label=g_strdup_printf("%zu tag-based %s%s",j->total,j->mode==1 ? "definitions/declarations" : (j->mode==2 ? (j->transitive ? "direct and inherited members" : "direct members") : (j->mode==4 ? "base definitions" : "derived types")),j->limited ? " • showing first 2000 matches" : (j->depth_limited ? " • inheritance depth limit reached" : (j->ambiguous ? " • ambiguous class names omitted" : ""))); set_usage_status(j->invalid ? "Enter a symbol name (or qualified owner for Members)" : label); g_free(label);
 wb.usage_idle=0; return G_SOURCE_REMOVE;
}
static void relation_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); RelationJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.usage_generation) return;
 g_clear_object(&wb.search_worker);
 if(!ok) return;
 if(j->index_version!=wb.index_version) { usage_start(NULL,NULL); return; }
 if(j->total==SIZE_MAX) { j->invalid=TRUE; j->total=0; }
 if((j->mode==2 || j->mode==4 || j->mode==5) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle))) {
  GeanyDocument *doc=document_get_current();insight_relation_tree_query(wb.relation_tree,j->index,j->query,j->mode==2 ? INSIGHT_RELATION_MEMBERS : (j->mode==4 ? INSIGHT_RELATION_BASES : INSIGHT_RELATION_DERIVED),doc ? doc->file_name : NULL,doc ? doc->file_type : NULL);
  insight_relation_tree_filter(wb.relation_tree,gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));
 }
 j->existing=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,symbol_iter_queue_free); j->collecting=TRUE; j->iter_valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(wb.matches),&j->iter); wb.usage_idle=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,relation_results_apply,g_object_ref(task),g_object_unref);
}
typedef struct {InsightIndex *base,*calls;GPtrArray *paths,*overlays;GHashTable *buffers;gchar *resources,*root,*query,*usr,*source;gboolean incoming;guint64 generation,epoch;} SemanticCallsJob;
static void semantic_calls_free(gpointer data) {SemanticCallsJob *j=data;insight_index_free(j->base);insight_index_free(j->calls);g_ptr_array_unref(j->paths);g_ptr_array_unref(j->overlays);g_hash_table_unref(j->buffers);g_free(j->resources);g_free(j->root);g_free(j->query);g_free(j->usr);g_free(j->source);g_free(j);}
static void semantic_calls_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;SemanticCallsJob *j=data;j->calls=insight_index_clone(j->base);if(!j->calls) {g_task_return_boolean(task,FALSE);return;}
 for(guint i=0;i<j->paths->len && !g_cancellable_is_cancelled(cancel);i++) {const gchar *path=g_ptr_array_index(j->paths,i);InsightIndex *overlay=g_ptr_array_index(j->overlays,i);InsightClangResult *parsed=NULL;
  /* Inactive open files may never have had a foreground Clang parse. Build
   * their overlay in this worker using the same captured unsaved headers. */
  if(!overlay && g_hash_table_lookup(j->buffers,path)) {gchar *root=j->root ? g_strdup(j->root):g_path_get_dirname(path);parsed=insight_clang_parse(path,g_hash_table_lookup(j->buffers,path),root,j->resources,j->buffers,cancel,NULL);g_free(root);if(insight_clang_result_usable(parsed)) overlay=parsed->relations;}
  gsize n=insight_index_count(overlay);InsightSymbol *rows=g_new(InsightSymbol,n);for(gsize k=0;k<n;k++) rows[k]=*insight_index_at(overlay,k);if(!insight_index_update(j->calls,g_ptr_array_index(j->paths,i),rows,n)) {g_free(rows);insight_clang_result_free(parsed);g_task_return_boolean(task,FALSE);return;}g_free(rows);insight_clang_result_free(parsed);}
 if(!j->usr) {const InsightSymbol *candidate=NULL;guint count=0;for(gsize i=0;i<insight_index_count(j->calls) && !g_cancellable_is_cancelled(cancel);i++) {const InsightSymbol *row=insight_index_at(j->calls,i);if(strcmp(row->kind,"@clangdef") || strcmp(row->signature,j->query) || !strchr(row->type,'(')) continue;if(!candidate || strcmp(candidate->name,row->name) || (!strcmp(row->scope,"internal") && strcmp(candidate->path,row->path))) {candidate=row;count++;}}if(count==1) j->usr=g_strdup(candidate->name);}
 g_task_return_boolean(task,!g_cancellable_is_cancelled(cancel));
}
static void semantic_calls_done(GObject *source,GAsyncResult *result,gpointer unused) {
 (void)source;(void)unused;SemanticCallsJob *j=g_task_get_task_data(G_TASK(result));gboolean ok=g_task_propagate_boolean(G_TASK(result),NULL);if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.usage_generation) return;g_clear_object(&wb.search_worker);if(!ok) {set_usage_status("Cannot prepare Clang call snapshot");return;}
 insight_relation_tree_calls(wb.relation_tree,j->calls,j->usr,j->query,j->incoming,j->source);insight_relation_tree_filter(wb.relation_tree,gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));set_usage_status(j->usr ? "Clang call snapshot; expand nodes for callers or callees":"Choose a function overload in the Clang tree");
 if(g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"routes")) gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle)) ? "treegraph":"tree");
}
static void semantic_call_tree(const gchar *query,gboolean incoming) {
 SemanticCallsJob *j=g_new0(SemanticCallsJob,1);j->base=insight_index_ref(wb.disk_calls);j->paths=g_ptr_array_new_with_free_func(g_free);j->overlays=g_ptr_array_new_with_free_func((GDestroyNotify)insight_index_free);j->query=g_strdup(query);j->incoming=incoming;j->generation=wb.usage_generation;j->epoch=workbench_epoch;
 j->buffers=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);j->resources=g_build_filename(app->datadir,"insight","clang","resources",NULL);if(app->project && wb.root) j->root=utils_get_utf8_from_locale(wb.root);gsize bytes=0;
 for(guint i=0;i<documents_array->len;i++) {GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!DOC_VALID(doc) || !doc->file_name) continue;gint length=sci_get_length(doc->editor->sci);
  if(length>=0 && length<=2*1024*1024 && bytes+(gsize)length<=32u*1024u*1024u && g_hash_table_size(j->buffers)<256) {gchar *text=sci_get_contents(doc->editor->sci,length+1);if(!memchr(text,0,length) && g_utf8_validate(text,length,NULL)) {g_hash_table_insert(j->buffers,g_strdup(doc->file_name),text);bytes+=length;}else g_free(text);}
  if(!insight_clang_source(doc->file_name)) continue;InsightClangResult *r=insight_clang_ui_result(doc->editor->sci);g_ptr_array_add(j->paths,g_strdup(doc->file_name));g_ptr_array_add(j->overlays,insight_clang_result_usable(r) ? insight_index_ref(r->relations):NULL);
 }
 gboolean fixed=g_object_steal_data(G_OBJECT(wb.usage_entry),"insight-semantic-restore")!=NULL;j->usr=g_object_steal_data(G_OBJECT(wb.usage_entry),"insight-semantic-usr");j->source=g_object_steal_data(G_OBJECT(wb.usage_entry),"insight-semantic-source");
 if(!fixed) {GeanyDocument *doc=document_get_current();InsightClangResult *r=DOC_VALID(doc) ? insight_clang_ui_result(doc->editor->sci):NULL;if(insight_clang_result_usable(r)) j->usr=insight_clang_symbol_at(r->relations,doc->file_name,sci_get_current_position(doc->editor->sci),sci_get_current_line(doc->editor->sci)+1,query);j->source=DOC_VALID(doc) ? g_strdup(doc->file_name):NULL;}
 wb.search_worker=g_cancellable_new();GTask *task=g_task_new(NULL,wb.search_worker,semantic_calls_done,NULL);g_task_set_task_data(task,j,semantic_calls_free);g_task_run_in_thread(task,semantic_calls_run);g_object_unref(task);set_usage_status("Preparing Clang call snapshot…");
}
typedef struct {GPtrArray *paths,*rows;GArray *colors;GHashTable *buffers;gchar *service,*root,*cache,*project,*query,*paint_path,*vivado;guint paint_doc;InsightSlangProject *result;guint64 epoch,generation,scan_generation;guint applied;gboolean valid,diagnostics,colors_only;} HdlJob;
static void hdl_text_cell(GtkTreeViewColumn *column,GtkCellRenderer *renderer,GtkTreeModel *model,GtkTreeIter *iter,gpointer field) {
 (void)column;gchar *value=NULL,*path=NULL;gulong line=0;gint which=GPOINTER_TO_INT(field);gtk_tree_model_get(model,iter,which,&value,USAGE_PATH,&path,USAGE_LINE,&line,-1);
 const gchar *text=value ? value:"";if(wb.usage_mode==12) {if(which==USAGE_SYMBOL) text=insight_tr(text);else if(!path && !line) text=insight_tr("Project diagnostic");}g_object_set(renderer,"text",text,NULL);g_free(value);g_free(path);
}
static void hdl_position_cell(GtkTreeViewColumn *column,GtkCellRenderer *renderer,GtkTreeModel *model,GtkTreeIter *iter,gpointer field) {
 (void)column;gulong value=0;gtk_tree_model_get(model,iter,GPOINTER_TO_INT(field),&value,-1);gchar *text=!value && wb.usage_mode==12 ? g_strdup("—"):g_strdup_printf("%lu",value);g_object_set(renderer,"text",text,NULL);g_free(text);
}
static void hdl_job_free(gpointer data) {
 HdlJob *j=data;if(j->result) {retire_index(j->result->index);j->result->index=NULL;insight_slang_project_free(j->result);}g_ptr_array_unref(j->paths);g_ptr_array_unref(j->rows);g_array_unref(j->colors);g_free(j->paint_path);g_hash_table_unref(j->buffers);g_free(j->service);g_free(j->vivado);g_free(j->root);g_free(j->cache);g_free(j->project);g_free(j->query);g_free(j);
}
/* Category zero deliberately leaves unsupported facts to the lexer. */
static guint hdl_color_role(const InsightSymbol *row)
{
 if(!strcmp(row->kind,"@hdldef")) {
  if(!strcmp(row->type,"Module") || !strcmp(row->type,"Instance")) return 1;
  if(!strcmp(row->type,"Port")) return 2;
  if(!strcmp(row->type,"Subroutine")) return 3;
  if(!strcmp(row->type,"Parameter") || !strcmp(row->type,"EnumValue")) return 4;
  if(!strcmp(row->type,"Net") || !strcmp(row->type,"Variable")) return 5;
 } else if(!strcmp(row->kind,"@hdlref")) {
  if(!strcmp(row->inheritance,"module") || !strcmp(row->inheritance,"instance")) return 1;
  if(!strcmp(row->inheritance,"port")) return 2;
  if(!strcmp(row->inheritance,"call")) return 3;
  if(!strcmp(row->inheritance,"value") || !strcmp(row->inheritance,"member") ||
     !strcmp(row->inheritance,"read") || !strcmp(row->inheritance,"write") ||
     !strcmp(row->inheritance,"read/write")) return 5;
 }
 return 0;
}

typedef struct {guint line,start,length,role;gboolean declaration,painted;} HdlSpan;

static gboolean hdl_identifier_byte(guchar c)
{
 return g_ascii_isalnum(c) || c=='_' || c=='$' || c>=128;
}

static gint hdl_span_order(gconstpointer a,gconstpointer b)
{
 const HdlSpan *x=a,*y=b;
 if(x->start!=y->start) return x->start<y->start ? -1:1;
 return (gint)x->declaration-(gint)y->declaration;
}

static void hdl_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel)
{
 (void)source;HdlJob *j=data;
 GHashTableIter iter;gpointer value;
 g_hash_table_iter_init(&iter,j->buffers);
 while(j->valid && g_hash_table_iter_next(&iter,NULL,&value))
  j->valid=g_utf8_validate(value,-1,NULL);
 if(j->valid) j->result=insight_slang_project_configured(j->service,j->paths,j->buffers,j->root,j->cache,j->vivado,cancel);
 const gchar *text=j->paint_path ? g_hash_table_lookup(j->buffers,j->paint_path):NULL;
 GArray *lines=g_array_new(FALSE,FALSE,sizeof(guint));
 if(text) {
  guint offset=0;g_array_append_val(lines,offset);
  for(;text[offset];offset++) if(text[offset]=='\n' || (text[offset]=='\r' && text[offset+1]!='\n')) {
   guint next=offset+1;g_array_append_val(lines,next);
  }
  g_array_append_val(lines,offset);
 }
 if(j->result) for(gsize i=0;i<insight_index_count(j->result->index) && !g_cancellable_is_cancelled(cancel);i++) {
  const InsightSymbol *row=insight_index_at(j->result->index,i);
  if(!j->colors_only && j->rows->len<2000 && !strcmp(row->kind,j->diagnostics ? "@hdldiag":"@hdlref") && (j->diagnostics || !*j->query || !strcmp(row->name,j->query))) g_ptr_array_add(j->rows,(gpointer)row);
  if(!text || !row->line || row->line>=lines->len || strcmp(row->path,j->paint_path)) continue;
  guint role=hdl_color_role(row);if(!role) continue;
  guint64 column=g_ascii_strtoull(row->signature,NULL,10);
  guint start=g_array_index(lines,guint,row->line-1),end=g_array_index(lines,guint,row->line);
  if(!column || column-1>=end-start) continue;
  guint pos=start+column-1;gsize n=strlen(row->name);
  if(!n || n>end-pos || memcmp(text+pos,row->name,n) ||
     (pos>start && (hdl_identifier_byte(text[pos-1]) || text[pos-1]=='\\')) ||
     (pos+n<end && hdl_identifier_byte(text[pos+n]))) continue;
  HdlSpan span={.line=row->line-1,.start=pos,.length=n,.role=role,.declaration=!strcmp(row->kind,"@hdldef")};
  g_array_append_val(j->colors,span);
 }
 g_array_unref(lines);
 g_array_sort(j->colors,hdl_span_order);
 g_task_return_boolean(task,j->result!=NULL);
}

static gboolean hdl_paint_visible(gpointer data)
{
 (void)data;
 GeanyDocument *doc=wb.alive ? document_get_current():NULL;
 if(!DOC_VALID(doc)) {hdl_paint_idle=0;return G_SOURCE_REMOVE;}
 ScintillaObject *sci=doc->editor->sci;
 GArray *spans=g_object_get_data(G_OBJECT(sci),"insight-hdl-spans");
 if(!spans) {hdl_paint_idle=0;return G_SOURCE_REMOVE;}
 gint top=SSM(sci,SCI_GETFIRSTVISIBLELINE,0,0),height=SSM(sci,SCI_LINESONSCREEN,0,0);
 guint first=MAX(0,SSM(sci,SCI_DOCLINEFROMVISIBLE,MAX(0,top-8),0));
 guint last=MAX(first,SSM(sci,SCI_DOCLINEFROMVISIBLE,top+height+8,0));
 guint lo=0,hi=spans->len;
 while(lo<hi) {guint mid=lo+(hi-lo)/2;if(g_array_index(spans,HdlSpan,mid).line<first) lo=mid+1;else hi=mid;}
 guint bg=SSM(sci,SCI_STYLEGETBACK,STYLE_DEFAULT,0);
 const guint light[]={0,0x8b3c66,0x405d9c,0x9b5b18,0x925c77,0x467c35};
 const guint dark[]={0,0xd9a0d5,0x8fc9ed,0xe8cf8c,0xd5a7bd,0x8fcaa0};
 const guint *palette=((bg&255)+((bg>>8)&255)+((bg>>16)&255))<384 ? dark:light;
 if(hdl_paint_doc!=doc->id || hdl_paint_first!=first || hdl_paint_last!=last) hdl_paint_cursor=0;
 hdl_paint_doc=doc->id;hdl_paint_first=first;hdl_paint_last=last;
 gint64 deadline=g_get_monotonic_time()+1500;
 SSM(sci,SCI_SETINDICATORCURRENT,INSIGHT_HDL_COLOR,0);
 for(guint i=MAX(lo,hdl_paint_cursor);i<spans->len;i++) {
  if((i&31)==0 && g_get_monotonic_time()>=deadline) {hdl_paint_cursor=i;return G_SOURCE_CONTINUE;}
  HdlSpan *span=&g_array_index(spans,HdlSpan,i);
  if(span->line>last) break;
  if(span->painted || !SSM(sci,SCI_GETLINEVISIBLE,span->line,0)) continue;
  if(span->start>=(guint)SSM(sci,SCI_GETENDSTYLED,0,0)) break;
  span->painted=TRUE;
  if(SSM(sci,SCI_GETSTYLEAT,span->start,0)==SCE_V_IDENTIFIER) {
   SSM(sci,SCI_SETINDICATORVALUE,SC_INDICVALUEBIT|palette[span->role],0);
   SSM(sci,SCI_INDICATORFILLRANGE,span->start,span->length);
  }
 }
 hdl_paint_idle=0;return G_SOURCE_REMOVE;
}

static void hdl_queue_paint(ScintillaObject *sci)
{
 if(!hdl_paint_idle && g_object_get_data(G_OBJECT(sci),"insight-hdl-spans")) {
  hdl_paint_cursor=0;hdl_paint_idle=g_idle_add_full(G_PRIORITY_LOW,hdl_paint_visible,NULL,NULL);
 }
}

static void hdl_paint(const HdlJob *j)
{
 GeanyDocument *doc=j->paint_path ? document_find_by_id(j->paint_doc):NULL;
 if(!DOC_VALID(doc) || g_strcmp0(doc->file_name,j->paint_path)) return;
 ScintillaObject *sci=doc->editor->sci;
 if(!g_hash_table_contains(j->buffers,j->paint_path) || SSM(sci,SCI_GETLEXER,0,0)!=SCLEX_VERILOG) return;
 hdl_clear_document_colors(sci);hdl_paint_cursor=0;
 g_object_set_data(G_OBJECT(sci),"insight-hdl-painted",GINT_TO_POINTER(1));
 g_object_set_data_full(G_OBJECT(sci),"insight-hdl-spans",g_array_ref(j->colors),(GDestroyNotify)g_array_unref);
 SSM(sci,SCI_INDICSETSTYLE,INSIGHT_HDL_COLOR,INDIC_TEXTFORE);
 SSM(sci,SCI_INDICSETFLAGS,INSIGHT_HDL_COLOR,SC_INDICFLAG_VALUEFORE);
 hdl_queue_paint(sci);
}
static gboolean hdl_results_apply(gpointer data) {
 GTask *task=data;HdlJob *j=g_task_get_task_data(task);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.usage_generation || g_strcmp0(j->project,app->project ? app->project->file_name:NULL)) return G_SOURCE_REMOVE;
 if(!j->applied) {
  GPtrArray *paths=insight_workbench_source_paths(insight_slang_source,257);GHashTable *current=g_hash_table_new(g_str_hash,g_str_equal);for(guint i=0;i<paths->len;i++) g_hash_table_add(current,g_ptr_array_index(paths,i));for(guint i=0;i<documents_array->len;i++) {GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(DOC_VALID(doc) && insight_slang_source(doc->file_name)) g_hash_table_add(current,doc->file_name);}
  gboolean same=g_hash_table_size(current)==j->paths->len && (!j->project || j->scan_generation==wb.scan_generation);for(guint i=0;same && i<j->paths->len;i++) same=g_hash_table_contains(current,g_ptr_array_index(j->paths,i));g_hash_table_unref(current);g_ptr_array_unref(paths);
  if(!same) {wb.usage_idle=0;set_usage_status("HDL sources changed; repeat the query");return G_SOURCE_REMOVE;}
  GHashTableIter iter;gpointer file,text;g_hash_table_iter_init(&iter,j->buffers);while(g_hash_table_iter_next(&iter,&file,&text)) {GeanyDocument *doc=document_find_by_filename(file);gchar *now=DOC_VALID(doc) ? sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1):NULL;gboolean same=now && !strcmp(now,text);g_free(now);if(!same) {wb.usage_idle=0;set_usage_status("HDL sources changed; repeat the query");return G_SOURCE_REMOVE;}}
  hdl_paint(j);
  gtk_list_store_clear(wb.matches);
 }
 gint64 deadline=g_get_monotonic_time()+3000;
 while(j->applied<j->rows->len) {const InsightSymbol *row=g_ptr_array_index(j->rows,j->applied++);GtkTreeIter iter;gboolean global=j->diagnostics && !strcmp(row->inheritance,"global");gchar *description=j->diagnostics ? g_strdup_printf("%s [%s]",row->type,row->scope):g_strdup_printf("%s → %s:%s",row->inheritance,row->scope,row->type);
  gtk_list_store_append(wb.matches,&iter);gtk_list_store_set(wb.matches,&iter,USAGE_DISPLAY,global ? "Project diagnostic":row->path,USAGE_PATH,global ? NULL:row->path,USAGE_LINE,global ? 0:row->line,USAGE_COLUMN,(gulong)g_ascii_strtoull(row->signature,NULL,10),USAGE_SYMBOL,row->name,USAGE_SNIPPET,description,-1);g_free(description);if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 gchar *status=g_strdup_printf(j->diagnostics ? "slang: %u messages • %zu declarations • diagnostics: %u • %s%s":"slang: %u references • %zu declarations • diagnostics: %u • %s%s",j->rows->len,insight_index_query(j->result->index,"",FALSE,NULL,"@hdldef",NULL,0,NULL,0),j->result->diagnostics,insight_tr(j->result->restored ? "Restored HDL project index":"HDL project index rebuilt"),insight_tr(j->result->persisted ? " • saved":" • memory only"));set_usage_status(status);g_free(status);wb.usage_idle=0;return G_SOURCE_REMOVE;
}
static void hdl_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source;(void)data;GTask *task=G_TASK(result);HdlJob *j=g_task_get_task_data(task);gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.usage_generation) return;g_clear_object(&wb.search_worker);
 if(ok) wb.usage_idle=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,hdl_results_apply,g_object_ref(task),g_object_unref);else set_usage_status("HDL semantic index unavailable or exceeds its limits");
}
static HdlJob *hdl_job_new(const gchar *query,gboolean diagnostics,gboolean colors_only) {
 HdlJob *j=g_new0(HdlJob,1);j->paths=insight_workbench_source_paths(insight_slang_source,257);j->rows=g_ptr_array_new();j->colors=g_array_new(FALSE,FALSE,sizeof(HdlSpan));j->buffers=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);j->service=insight_completion_slang_service();j->query=g_strdup(query);j->project=g_strdup(app->project ? app->project->file_name:NULL);j->vivado=insight_slang_vivado_config(j->project);j->root=wb.root && app->project ? utils_get_utf8_from_locale(wb.root):NULL;j->cache=app->project && wb.cache_path ? g_strconcat(wb.cache_path,".hdl",NULL):NULL;j->valid=TRUE;j->diagnostics=diagnostics;j->colors_only=colors_only;j->epoch=workbench_epoch;j->generation=colors_only ? hdl_color_revision:wb.usage_generation;j->scan_generation=wb.scan_generation;
 GeanyDocument *active=document_get_current();
 if(DOC_VALID(active) && insight_slang_source(active->file_name)) {j->paint_path=g_strdup(active->file_name);j->paint_doc=active->id;}
 return j;
}

static HdlJob *hdl_snapshot(const gchar *query,gboolean diagnostics) {
 HdlJob *j=hdl_job_new(query,diagnostics,FALSE);
 GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);for(guint i=0;i<j->paths->len;i++) g_hash_table_add(seen,g_ptr_array_index(j->paths,i));gsize total=0;
 for(guint i=0;i<documents_array->len;i++) {GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!DOC_VALID(doc) || !insight_slang_source(doc->file_name)) continue;
  if(!g_hash_table_contains(seen,doc->file_name)) {g_ptr_array_add(j->paths,g_strdup(doc->file_name));g_hash_table_add(seen,doc->file_name);}gint length=sci_get_length(doc->editor->sci);
  if(length<0 || length>2*1024*1024 || total+(gsize)length>32u*1024u*1024u || g_hash_table_size(j->buffers)>=256) {j->valid=FALSE;continue;}
  gchar *text=sci_get_contents(doc->editor->sci,length+1);if(memchr(text,0,length) || !g_utf8_validate(text,length,NULL)) {j->valid=FALSE;g_free(text);}else {g_hash_table_insert(j->buffers,g_strdup(doc->file_name),text);total+=length;}
 }
 g_hash_table_unref(seen);if(!j->root && j->paths->len) j->root=g_path_get_dirname(g_ptr_array_index(j->paths,0));
 return j;
}
