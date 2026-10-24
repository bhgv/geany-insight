/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-compare-ui.h"
#include "geany.h"
#include "insight-editor-ai.h"
#include "insight-ai.h"
#include "insight-ai-ui.h"
#include "insight-diff.h"
#include "insight-project-report.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include "insight-ai-profiles.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "../insight/desktop/ai_client.h"
#include "../insight/desktop/ai_edit_session.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define STATE "insight-editor-ai-state"
typedef struct { gchar *path,*base;guint id;InsightAiEdits *edits; } Source;
typedef struct {
 GtkWidget *window,*panel,*file,*prompt,*requirements,*context,*status,*send,*cancel_button,*undo,*redo,*tasks,*recent,*frequent,*provider,*endpoint,*model,*key,*python;
 GPtrArray *files,*prompts;gchar *generated_context,*prompt_path;guint current;gboolean busy,closed,switching;GCancellable *cancel;InsightAiProfiles *profiles;
} Review;
typedef struct { gchar *endpoint,*model,*key,*python,*helper,*question,*context,*code,*location,*extra;InsightAiAnswer *answer; } Job;
static gboolean initialized;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *v=NULL;for(GList *p=list;p && !v;p=p->next) v=named(p->data,name);g_list_free(list);return v; }return NULL;
}
static gchar *text(GtkWidget *view) { GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));GtkTextIter a,z;gtk_text_buffer_get_bounds(b,&a,&z);return gtk_text_buffer_get_text(b,&a,&z,TRUE); }
static void set_text(GtkWidget *view,const gchar *s) { gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),s ? s:"",-1); }
static void message(Review *r,const gchar *s) { gtk_label_set_text(GTK_LABEL(r->status),insight_tr(s)); }
static gchar *config_path(const gchar *name) { return g_build_filename(app->configdir,name,NULL); }
static Source *current(Review *r) { return g_ptr_array_index(r->files,r->current); }
static void source_free(gpointer p) { Source *s=p;g_free(s->path);g_free(s->base);insight_ai_edits_free(s->edits);g_free(s); }
static void review_free(gpointer p) { Review *r=p;g_clear_object(&r->cancel);g_ptr_array_unref(r->files);g_ptr_array_unref(r->prompts);g_free(r->generated_context);g_free(r->prompt_path);g_free(r); }
static void destroyed(GtkWidget *w,gpointer p) { (void)w;Review *r=p;r->closed=TRUE;if(r->cancel) g_cancellable_cancel(r->cancel); }
static GeanyDocument *source_document(Source *s) { GeanyDocument *d=s->id ? document_find_by_id(s->id):NULL;if(d && !g_strcmp0(d->file_name ? d->file_name:"untitled",s->path)) return d;return NULL; }
static gchar *capture_source(Source *s,GError **error) {
 GeanyDocument *d=source_document(s);
 if(s->id && !d) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"Source was closed or renamed. Add it again as a new review.");return NULL; }
 if(!d) d=document_find_by_filename(s->path);
 gchar *code=NULL;gsize n=0;
 if(d) { sptr_t length=SEND(d->editor->sci,SCI_GETLENGTH,0,0);if(length<0 || length>INSIGHT_AI_SOURCE_LIMIT) goto invalid;code=sci_get_contents(d->editor->sci,-1);n=length;s->id=d->id; }
 else {
  gchar *native=g_filename_from_utf8(s->path,-1,NULL,NULL,error);if(!native) return NULL;GFile *file=g_file_new_for_path(native);g_free(native);
  GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);
  if(!info) { g_object_unref(file);return NULL; }if(g_file_info_get_file_type(info)!=G_FILE_TYPE_REGULAR) { g_object_unref(info);g_object_unref(file);goto invalid; }g_object_unref(info);
  GFileInputStream *in=g_file_read(file,NULL,error);g_object_unref(file);if(!in) return NULL;code=g_malloc(INSIGHT_AI_SOURCE_LIMIT+2);gboolean ok=g_input_stream_read_all(G_INPUT_STREAM(in),code,INSIGHT_AI_SOURCE_LIMIT+1,&n,NULL,error);g_object_unref(in);if(!ok) { g_free(code);return NULL; }code[n]=0;
 }
 if(n>INSIGHT_AI_SOURCE_LIMIT || memchr(code,0,n) || !g_utf8_validate(code,n,NULL)) { g_free(code);goto invalid; }return code;
invalid:g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Use a regular UTF-8 source of at most 128 KiB without NUL bytes.");return NULL;
}
static Source *source_new(const gchar *path,GeanyDocument *doc,GError **error) {
 Source *s=g_new0(Source,1);s->path=g_strdup(path);if(doc) s->id=doc->id;s->base=capture_source(s,error);if(!s->base) { source_free(s);return NULL; }s->edits=insight_ai_edits_new(s->base);return s;
}
static void controls(Review *r) {
 Source *s=current(r);GtkWidget *fields[]={r->prompt,r->tasks,r->recent,r->frequent,r->provider,r->endpoint,r->model,r->key,r->python};for(guint i=0;i<G_N_ELEMENTS(fields);i++) gtk_widget_set_sensitive(fields[i],!r->busy);gtk_widget_set_sensitive(r->send,!r->busy);gtk_widget_set_sensitive(r->cancel_button,r->busy);gtk_widget_set_sensitive(r->file,!r->busy);gtk_widget_set_sensitive(r->undo,!r->busy && s->edits->position>0);gtk_widget_set_sensitive(r->redo,!r->busy && s->edits->position+1<s->edits->variants->len);g_object_set_data(G_OBJECT(r->window),"insight-editor-ai-busy",GINT_TO_POINTER(r->busy));
}
static gboolean stage_manual(Review *r) {
 gchar *code=insight_diff_review_result(r->window);if(!code) { message(r,"Wait for the comparison to finish before continuing.");return FALSE; }
 Source *s=current(r);const InsightAiVariant *v=insight_ai_edits_current(s->edits);gboolean ok=TRUE;if(strcmp(code,v->code)) { GError *e=NULL;ok=insight_ai_edits_push(s->edits,code,v->requirements,"Manual Result correction.",&e);if(e) { message(r,e->message);g_clear_error(&e); } }g_free(code);return ok;
}
static void display(Review *r) {
 insight_diff_review_choices(r->window,NULL,NULL,NULL,NULL,0,0);Source *s=current(r);const InsightAiVariant *v=insight_ai_edits_current(s->edits);set_text(r->requirements,v->requirements);if(!insight_diff_review_update(r->window,s->base,v->code,v->explanation,v->code)) message(r,"Wait for the comparison to finish before continuing.");controls(r);
}
static void move_variant(GtkWidget *w,gpointer p) {
 Review *r=p;if(r->busy || !stage_manual(r)) return;gint step=w==r->undo ? -1:1;if(insight_ai_edits_move(current(r)->edits,step)) { display(r);message(r,"Variant restored: code, accumulated requirements and explanation."); }
}
static void retry_comparison(GtkWidget *w,gpointer p) {
 (void)w;Review *r=p;if(r->busy) return;gchar *result=insight_diff_review_result(r->window);if(result) { g_free(result);if(!stage_manual(r)) return; }display(r);
}
static gboolean apply_result(const gchar *code,gpointer p,GError **error) {
 Review *r=p;Source *s=current(r);if(r->busy) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"Wait for the AI request to finish.");return FALSE; }
 gchar *now=capture_source(s,error);if(!now) return FALSE;gboolean same=!strcmp(now,s->base);g_free(now);if(!same) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"Source changed. Refresh source, compare with the retained Result, then apply explicitly.");return FALSE; }
 GeanyDocument *d=source_document(s);if(!d) { gchar *path=g_filename_from_utf8(s->path,-1,NULL,NULL,error);if(!path) return FALSE;d=document_open_file(path,FALSE,NULL,NULL);g_free(path);if(d) s->id=d->id; }
 if(!d || d->readonly || SEND(d->editor->sci,SCI_GETREADONLY,0,0)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_PERMISSION_DENIED,"Source is unavailable or read-only.");return FALSE; }
 /* Recheck the opened/decoded buffer too, before any mutation. */
 now=capture_source(s,error);if(!now) return FALSE;same=!strcmp(now,s->base);g_free(now);if(!same || strlen(code)>INSIGHT_AI_SOURCE_LIMIT) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"Opened source differs or Result exceeds 128 KiB. Refresh before applying.");return FALSE; }
 if(strcmp(code,s->base)) { SEND(d->editor->sci,SCI_BEGINUNDOACTION,0,0);SEND(d->editor->sci,SCI_SETTARGETRANGE,0,strlen(s->base));SEND(d->editor->sci,SCI_REPLACETARGET,strlen(code),(sptr_t)code);SEND(d->editor->sci,SCI_ENDUNDOACTION,0,0); }return TRUE;
}
static void open_source(GtkWidget *w,gpointer p) {
 (void)w;Review *r=p;if(r->busy) return;Source *s=current(r);GeanyDocument *d=source_document(s);if(s->id && !d) { message(r,"Source was closed or renamed. Add it again as a new review.");return; }
 if(!d) { gchar *path=g_filename_from_utf8(s->path,-1,NULL,NULL,NULL);if(path) { d=document_open_file(path,FALSE,NULL,NULL);g_free(path); }if(d) s->id=d->id; }
 if(!d) { message(r,"Cannot open the source in the editor.");return; }gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.notebook),document_get_notebook_page(d));gtk_window_present(GTK_WINDOW(main_widgets.window));gtk_widget_grab_focus(GTK_WIDGET(d->editor->sci));message(r,"Source opened. AI Diff stays open; use Refresh source after editor changes.");
}
static void build_context(GtkWidget *,gpointer);
static void refresh_source(GtkWidget *w,gpointer p) {
 (void)w;Review *r=p;if(r->busy || !stage_manual(r)) return;GPtrArray *fresh=g_ptr_array_new_with_free_func(g_free);GError *e=NULL;
 for(guint i=0;i<r->files->len;i++) { Source *s=g_ptr_array_index(r->files,i);if(s->id && !document_find_by_id(s->id) && strcmp(s->path,"untitled")) s->id=0;gchar *code=capture_source(s,&e);if(!code) { message(r,e->message);g_clear_error(&e);g_ptr_array_unref(fresh);return; }g_ptr_array_add(fresh,code); }
 for(guint i=0;i<r->files->len;i++) { Source *s=g_ptr_array_index(r->files,i);g_free(s->base);s->base=g_strdup(g_ptr_array_index(fresh,i)); }g_ptr_array_unref(fresh);
 gchar *context=text(r->context);gboolean rebuild=r->generated_context && !strcmp(context,r->generated_context);g_free(context);if(rebuild) build_context(NULL,r);
 display(r);message(r,"Selected sources refreshed from open editor buffers or disk. Result and variants were retained; compare before applying. Custom context is retained.");
}
static void switch_file(GtkComboBox *combo,gpointer p) {
 Review *r=p;if(r->switching) return;gint i=gtk_combo_box_get_active(combo);if(i<0 || (guint)i==r->current) return;
 if(r->busy || !stage_manual(r)) { r->switching=TRUE;gtk_combo_box_set_active(combo,r->current);r->switching=FALSE;return; }r->current=i;display(r);
}
static gboolean add_source(Review *r,const gchar *path,GeanyDocument *doc) {
 for(guint i=0;i<r->files->len;i++) if(!g_strcmp0(((Source *)g_ptr_array_index(r->files,i))->path,path)) return TRUE;
 if(r->files->len>=16) { message(r,"A review holds at most 16 files; choose a smaller project group.");return FALSE; }
 GError *e=NULL;Source *s=source_new(path,doc,&e);if(!s) { message(r,e->message);g_clear_error(&e);return FALSE; }g_ptr_array_add(r->files,s);gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(r->file),path);return TRUE;
}
static void add_current(GtkWidget *w,gpointer p) { (void)w;Review *r=p;if(r->busy) return;GeanyDocument *d=document_get_current();if(d) add_source(r,d->file_name ? d->file_name:"untitled",d); }
static void remove_source(GtkWidget *w,gpointer p) {
 (void)w;Review *r=p;if(r->busy || r->files->len<2 || !stage_manual(r)) return;r->switching=TRUE;g_ptr_array_remove_index(r->files,r->current);r->current=0;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(r->file));for(guint i=0;i<r->files->len;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(r->file),((Source *)g_ptr_array_index(r->files,i))->path);gtk_combo_box_set_active(GTK_COMBO_BOX(r->file),0);r->switching=FALSE;display(r);message(r,"File removed from this review. Rebuild project context if it contained that source.");
}
static void project_files(GtkWidget *w,gpointer p) {
 (void)w;Review *r=p;if(r->busy) return;GError *e=NULL;InsightReportSnapshot *snapshot=insight_project_report_capture(&e);if(!snapshot) { message(r,e->message);g_clear_error(&e);return; }
 GtkWidget *d=gtk_dialog_new_with_buttons(insight_tr("Choose project files for AI review"),GTK_WINDOW(r->window),GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Add selected files"),GTK_RESPONSE_ACCEPT,NULL);g_object_ref_sink(d);gtk_widget_set_name(d,"insight-editor-ai-project-files");gtk_window_set_default_size(GTK_WINDOW(d),800,450);
 GtkListStore *model=gtk_list_store_new(1,G_TYPE_STRING);for(guint i=0;i<snapshot->paths->len;i++) { GtkTreeIter it;gtk_list_store_append(model,&it);gtk_list_store_set(model,&it,0,(gchar *)g_ptr_array_index(snapshot->paths,i),-1); }
 GtkWidget *tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));gtk_widget_set_name(tree,"insight-editor-ai-project-list");g_object_unref(model);gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree),-1,insight_tr("Project files — select up to 16"),gtk_cell_renderer_text_new(),"text",0,NULL);GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));gtk_tree_selection_set_mode(selection,GTK_SELECTION_MULTIPLE);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),tree);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))),scroll,TRUE,TRUE,0);gtk_widget_show_all(d);
 g_object_ref(r->window);if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT && !r->closed) { GtkTreeModel *m;GList *paths=gtk_tree_selection_get_selected_rows(selection,&m);for(GList *it=paths;it;it=it->next) { GtkTreeIter row;if(gtk_tree_model_get_iter(m,&row,it->data)) { gchar *path=NULL;gtk_tree_model_get(m,&row,0,&path,-1);if(!add_source(r,path,NULL)) { g_free(path);break; }g_free(path); } }g_list_free_full(paths,(GDestroyNotify)gtk_tree_path_free); }
 gtk_widget_destroy(d);g_object_unref(d);insight_report_snapshot_unref(snapshot);g_object_unref(r->window);
}
static void build_context(GtkWidget *w,gpointer p) {
 (void)w;Review *r=p;if(r->busy) return;GString *context=g_string_new("Explicitly selected project files (captured UTF-8 snapshots; no commands have run):\n");
 for(guint i=0;i<r->files->len;i++) { Source *s=g_ptr_array_index(r->files,i);const gchar *code=s->base;if(i==r->current) continue;if(context->len+strlen(s->path)+strlen(code)+32>65536) { g_string_free(context,TRUE);message(r,"Selected project context exceeds 64 KiB. Remove files or edit a smaller context; no partial context was installed.");return; }g_string_append_printf(context,"\nFILE: %s\n%s\nEND FILE\n",s->path,code); }
 set_text(r->context,context->str);g_free(r->generated_context);r->generated_context=g_strdup(context->str);g_string_free(context,TRUE);message(r,"Project context prepared. Review it before Send; it is sent only on explicit requests.");
}
static void job_free(gpointer p) { Job *j=p;g_free(j->endpoint);g_free(j->model);if(j->key) { memset(j->key,0,strlen(j->key));g_free(j->key); }g_free(j->python);g_free(j->helper);g_free(j->question);g_free(j->context);g_free(j->code);g_free(j->location);g_free(j->extra);insight_ai_answer_free(j->answer);g_free(j); }
static void worker(GTask *task,gpointer object,gpointer data,GCancellable *cancel) { (void)object;Job *j=data;InsightAiRequest request={j->endpoint,j->model,j->key,j->question,j->context,j->code,"",j->location};GError *e=NULL;j->answer=insight_ai_request(j->python,j->helper,&request,cancel,&e);if(j->answer) g_task_return_boolean(task,TRUE);else g_task_return_error(task,e); }
static gint recent_order(gconstpointer a,gconstpointer b) { const InsightAiPrompt *x=*(InsightAiPrompt *const *)a,*y=*(InsightAiPrompt *const *)b;return x->last>y->last ? -1:x->last<y->last ? 1:strcmp(x->text,y->text); }
static gint frequent_order(gconstpointer a,gconstpointer b) { const InsightAiPrompt *x=*(InsightAiPrompt *const *)a,*y=*(InsightAiPrompt *const *)b;return x->count>y->count ? -1:x->count<y->count ? 1:recent_order(a,b); }
static void prompt_lists(Review *r) {
 GPtrArray *latest=insight_ai_prompts_load(r->prompt_path);g_ptr_array_unref(r->prompts);r->prompts=latest;
 GtkWidget *combos[]={r->recent,r->frequent};for(guint k=0;k<2;k++) { gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(combos[k]));GPtrArray *sorted=g_ptr_array_new();for(guint i=0;i<r->prompts->len;i++) g_ptr_array_add(sorted,g_ptr_array_index(r->prompts,i));g_ptr_array_sort(sorted,k ? frequent_order:recent_order);
  for(guint i=0;i<MIN(sorted->len,100);i++) { InsightAiPrompt *q=g_ptr_array_index(sorted,i);const gchar *end=g_utf8_offset_to_pointer(q->text,MIN((glong)55,g_utf8_strlen(q->text,-1)));gchar *short_text=g_strndup(q->text,end-q->text);for(gchar *c=short_text;*c;c++) if(*c=='\n'||*c=='\r') *c=' ';gchar *label=g_strdup_printf("%s%s",short_text,strlen(short_text)<strlen(q->text) ? "…":"");gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combos[k]),q->text,label);g_free(label);g_free(short_text); }g_ptr_array_unref(sorted);
 }
}
static void delete_history(GtkWidget *widget,gpointer data) {
 Review *r=data;if(r->busy || r->closed) return;gboolean all=!strcmp(gtk_widget_get_name(widget),"insight-editor-ai-history-clear");const gchar *selected=gtk_combo_box_get_active_id(GTK_COMBO_BOX(r->recent));if(!selected) selected=gtk_combo_box_get_active_id(GTK_COMBO_BOX(r->frequent));if(!all && !selected) { message(r,"Select a saved request to delete first.");return; }gchar *stable=g_strdup(selected);GError *error=NULL;if(insight_ai_prompts_remove(r->prompts,r->prompt_path,all ? NULL:stable,&error)) { prompt_lists(r);message(r,all ? "History for this context cleared; other contexts are unchanged.":"Selected request deleted from this context history."); }else { message(r,error->message);g_clear_error(&error); }g_free(stable);
}
static void done(GObject *object,GAsyncResult *result,gpointer data) {
 (void)data;Review *r=g_object_get_data(object,STATE);Job *j=g_task_get_task_data(G_TASK(result));GError *e=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&e);if(r->closed) { g_clear_error(&e);return; }r->busy=FALSE;g_clear_object(&r->cancel);insight_diff_review_lock(r->window,FALSE);
 if(!ok) { message(r,e->message);g_clear_error(&e);controls(r);return; }
 Source *s=current(r);ok=insight_ai_edits_push(s->edits,j->answer->has_replacement ? j->answer->replacement:j->code,j->question,j->answer->explanation,&e);
 if(ok) { display(r);insight_ai_profiles_install_choices(r->window,r->window,s->base,s->path);set_text(r->prompt,"");message(r,"New AI variant staged in Result. Earlier requirements and manual corrections were included; source is unchanged."); }
 else { message(r,e->message); }
 g_clear_error(&e);controls(r);
}
static void save_config(Review *r) {
 insight_ai_profiles_save_current(r->profiles);
 GKeyFile *k=g_key_file_new();g_key_file_set_integer(k,"assistant","provider",gtk_combo_box_get_active(GTK_COMBO_BOX(r->provider)));const gchar *names[]={"endpoint","model","python"};GtkWidget *fields[]={r->endpoint,r->model,r->python};for(guint i=0;i<3;i++) g_key_file_set_string(k,"assistant",names[i],gtk_entry_get_text(GTK_ENTRY(fields[i])));gsize n;gchar *raw=g_key_file_to_data(k,&n,NULL),*path=config_path("insight-ai.conf");GFile *file=g_file_new_for_path(path);g_file_replace_contents(file,raw,n,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,NULL);g_object_unref(file);g_free(raw);g_free(path);g_key_file_unref(k);
}
static void send_request(GtkWidget *w,gpointer p) {
 (void)w;Review *r=p;if(r->busy || !stage_manual(r)) return;GError *e=NULL;gchar *extra=text(r->prompt);const InsightAiVariant *v=insight_ai_edits_current(current(r)->edits);gchar *all=insight_ai_requirements_append(v->requirements,extra,&e);if(!all) { message(r,e->message);g_clear_error(&e);g_free(extra);return; }
 if(!*gtk_entry_get_text(GTK_ENTRY(r->model)) && !insight_ai_profiles_multiple(r->profiles)) { message(r,"Enter a model ID and a question first.");g_free(all);g_free(extra);return; }
 Job *j=g_new0(Job,1);j->endpoint=g_strdup(gtk_entry_get_text(GTK_ENTRY(r->endpoint)));j->model=g_strdup(gtk_entry_get_text(GTK_ENTRY(r->model)));j->key=g_strdup(gtk_entry_get_text(GTK_ENTRY(r->key)));j->python=g_strdup(gtk_entry_get_text(GTK_ENTRY(r->python)));j->helper=g_build_filename(app->datadir,"insight","tools","ai_client.py",NULL);j->question=all;j->extra=extra;j->code=g_strdup(v->code);j->context=text(r->context);j->location=g_strdup(current(r)->path);
 if(strlen(j->context)>65536) { message(r,"Project context exceeds 64 KiB; edit it before Send.");job_free(j);return; }if(!insight_ai_prompts_record(r->prompts,r->prompt_path,j->extra,&e)) { message(r,e->message);g_clear_error(&e);job_free(j);return; }prompt_lists(r);save_config(r);r->busy=TRUE;r->cancel=g_cancellable_new();insight_diff_review_lock(r->window,TRUE);controls(r);message(r,"Sending accumulated requirements, current Result and reviewed project context to the API…");GTask *task=g_task_new(r->window,r->cancel,done,NULL);g_task_set_task_data(task,j,job_free);InsightAiRequest request={j->endpoint,j->model,j->key,j->question,j->context,j->code,"",j->location};if(!insight_ai_profiles_dispatch(r->profiles,task,&request,&j->answer)) g_task_run_in_thread(task,worker);g_object_unref(task);
}
static void cancel_request(GtkWidget *w,gpointer p) { (void)w;Review *r=p;if(r->cancel) g_cancellable_cancel(r->cancel); }
static void provider_changed(GtkComboBox *combo,gpointer p) { Review *r=p;gint n=gtk_combo_box_get_active(combo);if(n<0 || n>4) return;gtk_entry_set_text(GTK_ENTRY(r->endpoint),insight_ai_profiles_endpoint(n));const gchar *key=*insight_ai_profiles_key_env(n) ? g_getenv(insight_ai_profiles_key_env(n)):NULL;gtk_entry_set_text(GTK_ENTRY(r->key),key ? key:""); }
static void append_prompt(Review *r,const gchar *request) { if(!request) return;gchar *old=text(r->prompt);gchar *next=*old ? g_strconcat(old,"\n",request,NULL):g_strdup(request);set_text(r->prompt,next);g_free(old);g_free(next); }
static void task_chosen(GtkWidget *w,gpointer p) { (void)w;Review *r=p;if(r->busy) return;gint n=gtk_combo_box_get_active(GTK_COMBO_BOX(r->tasks));if(n>=0) append_prompt(r,insight_tr(insight_ai_task_prompt(n))); }
static void history_chosen(GtkComboBox *combo,gpointer p) { Review *r=p;if(!r->busy) append_prompt(r,gtk_combo_box_get_active_id(combo)); }
static GtkWidget *button(GtkWidget *box,const gchar *label,const gchar *name,GCallback cb,Review *r) { GtkWidget *b=gtk_button_new_with_label(insight_tr(label));gtk_widget_set_name(b,name);gtk_box_pack_start(GTK_BOX(box),b,FALSE,FALSE,0);g_signal_connect(b,"clicked",cb,r);return b; }
static GtkWidget *row(GtkWidget *box) { GtkWidget *r=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),r,FALSE,FALSE,0);return r; }
static GtkWidget *view(GtkWidget *box,const gchar *name,gboolean editable,gint height) { GtkWidget *v=gtk_text_view_new(),*scroll=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_name(v,name);gtk_text_view_set_editable(GTK_TEXT_VIEW(v),editable);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(v),GTK_WRAP_WORD_CHAR);gtk_widget_set_size_request(scroll,-1,height);gtk_container_add(GTK_CONTAINER(scroll),v);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);return v; }
static GtkWidget *expander(GtkWidget *box,const gchar *label) { GtkWidget *e=gtk_expander_new(insight_tr(label)),*b=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_container_add(GTK_CONTAINER(e),b);gtk_box_pack_start(GTK_BOX(box),e,FALSE,FALSE,0);return b; }
static void api_controls(Review *r,GtkWidget *box) {
 GtkWidget *bar=row(box);r->provider=gtk_combo_box_text_new();gtk_widget_set_name(r->provider,"insight-editor-ai-provider");const gchar *labels[]={"OpenAI / ChatGPT API","xAI / Grok","DeepSeek","Qwen / Model Studio","Custom compatible API"};for(guint i=0;i<5;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(r->provider),labels[i]);gtk_box_pack_start(GTK_BOX(bar),r->provider,FALSE,FALSE,0);
 r->endpoint=gtk_entry_new();gtk_widget_set_name(r->endpoint,"insight-editor-ai-endpoint");gtk_entry_set_placeholder_text(GTK_ENTRY(r->endpoint),"Complete chat/completions URL");gtk_box_pack_start(GTK_BOX(bar),r->endpoint,TRUE,TRUE,0);r->model=gtk_entry_new();gtk_widget_set_name(r->model,"insight-editor-ai-model");gtk_entry_set_placeholder_text(GTK_ENTRY(r->model),"Model ID from your API account");gtk_box_pack_start(GTK_BOX(bar),r->model,TRUE,TRUE,0);
 bar=row(box);gtk_box_pack_start(GTK_BOX(bar),gtk_label_new(insight_tr("API key (shared profile)")),FALSE,FALSE,0);r->key=gtk_entry_new();gtk_widget_set_name(r->key,"insight-editor-ai-key");gtk_entry_set_visibility(GTK_ENTRY(r->key),FALSE);gtk_box_pack_start(GTK_BOX(bar),r->key,TRUE,TRUE,0);r->python=gtk_entry_new();gtk_widget_set_name(r->python,"insight-editor-ai-python");gtk_box_pack_start(GTK_BOX(bar),gtk_label_new("Python 3"),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(bar),r->python,TRUE,TRUE,0);
#ifdef G_OS_WIN32
 gtk_entry_set_text(GTK_ENTRY(r->python),"python");
#else
 gtk_entry_set_text(GTK_ENTRY(r->python),"python3");
#endif
 insight_ai_ui_entry(r->endpoint,insight_tr("Complete API chat/completions URL"),18);insight_ai_ui_entry(r->model,insight_tr("Model ID supplied by your AI API provider"),12);insight_ai_ui_entry(r->key,insight_tr("Secret API key for the selected connection"),12);insight_ai_ui_entry(r->python,insight_tr("Python 3 executable used by the AI network helper"),12);gtk_widget_set_tooltip_text(r->provider,insight_tr("AI service or a custom compatible API"));g_signal_connect(r->provider,"changed",G_CALLBACK(provider_changed),r);gtk_combo_box_set_active(GTK_COMBO_BOX(r->provider),0);GKeyFile *k=g_key_file_new();gchar *path=config_path("insight-ai.conf");if(g_key_file_load_from_file(k,path,G_KEY_FILE_NONE,NULL)) { gtk_combo_box_set_active(GTK_COMBO_BOX(r->provider),CLAMP(g_key_file_get_integer(k,"assistant","provider",NULL),0,4));const gchar *names[]={"endpoint","model","python"};GtkWidget *fields[]={r->endpoint,r->model,r->python};for(guint i=0;i<3;i++) { gchar *s=g_key_file_get_string(k,"assistant",names[i],NULL);if(s) gtk_entry_set_text(GTK_ENTRY(fields[i]),s);g_free(s); } }g_free(path);g_key_file_unref(k);r->profiles=insight_ai_profiles_bind(box,r->provider,r->endpoint,r->model,r->key,r->python);
}
static void open_review(GtkWidget *w,gpointer data) {
 (void)w;(void)data;GeanyDocument *d=document_get_current();if(!d) return;GError *e=NULL;Source *s=source_new(d->file_name ? d->file_name:"untitled",d,&e);if(!s) { ui_set_statusbar(TRUE,"%s",e->message);g_clear_error(&e);return; }
 Review *r=g_new0(Review,1);r->files=g_ptr_array_new_with_free_func(source_free);g_ptr_array_add(r->files,s);r->prompt_path=insight_ai_history_path("editor-project");r->prompts=insight_ai_prompts_load(r->prompt_path);r->window=insight_diff_review_create(main_widgets.window,s->base,s->base,"Choose a standard task or type a request. Nothing is sent until Send.",apply_result,NULL,r,NULL);insight_diff_review_language(r->window,s->path);gtk_widget_set_name(r->window,"insight-editor-ai-window");g_object_set_data_full(G_OBJECT(r->window),STATE,r,review_free);g_signal_connect(r->window,"destroy",G_CALLBACK(destroyed),r);
 GtkWidget *root=gtk_bin_get_child(GTK_BIN(r->window));r->panel=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);g_object_set_data(G_OBJECT(r->panel),"insight-editor-ai-controls",GINT_TO_POINTER(1));gtk_box_pack_start(GTK_BOX(root),r->panel,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(root),r->panel,1);insight_panel_help_attach(r->panel,"editor-ai.md");
 GtkWidget *bar=row(r->panel);r->file=gtk_combo_box_text_new();gtk_widget_set_name(r->file,"insight-editor-ai-file");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(r->file),s->path);gtk_combo_box_set_active(GTK_COMBO_BOX(r->file),0);GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(r->file));for(GList *c=cells;c;c=c->next) g_object_set(c->data,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,"width-chars",24,"max-width-chars",32,NULL);g_list_free(cells);gtk_box_pack_start(GTK_BOX(bar),r->file,TRUE,TRUE,0);g_signal_connect(r->file,"changed",G_CALLBACK(switch_file),r);button(bar,"Add current file","insight-editor-ai-add",G_CALLBACK(add_current),r);button(bar,"Project files…","insight-editor-ai-project",G_CALLBACK(project_files),r);button(bar,"Open source in editor","insight-editor-ai-open",G_CALLBACK(open_source),r);button(bar,"Refresh sources","insight-editor-ai-refresh",G_CALLBACK(refresh_source),r);button(bar,"Remove file","insight-editor-ai-remove",G_CALLBACK(remove_source),r);
 api_controls(r,expander(r->panel,"API settings"));bar=row(r->panel);r->tasks=gtk_combo_box_text_new();gtk_widget_set_name(r->tasks,"insight-editor-ai-task");for(guint i=0;i<insight_ai_task_count();i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(r->tasks),insight_tr(insight_ai_task_name(i)));gtk_combo_box_set_active(GTK_COMBO_BOX(r->tasks),0);gtk_box_pack_start(GTK_BOX(bar),r->tasks,FALSE,FALSE,0);button(bar,"Add standard task","insight-editor-ai-add-task",G_CALLBACK(task_chosen),r);
 r->recent=gtk_combo_box_text_new();r->frequent=gtk_combo_box_text_new();gtk_widget_set_name(r->recent,"insight-editor-ai-recent");gtk_widget_set_name(r->frequent,"insight-editor-ai-frequent");GtkWidget *recent_box=expander(r->panel,"Recent and frequent requests (100 per project and context)"),*history_row=row(recent_box);gtk_box_pack_start(GTK_BOX(history_row),gtk_label_new(insight_tr("Recent")),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(history_row),r->recent,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(history_row),gtk_label_new(insight_tr("Frequent")),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(history_row),r->frequent,TRUE,TRUE,0);g_signal_connect(r->recent,"changed",G_CALLBACK(history_chosen),r);g_signal_connect(r->frequent,"changed",G_CALLBACK(history_chosen),r);
 GtkWidget *history_buttons=row(recent_box);button(history_buttons,"Delete selected","insight-editor-ai-history-delete",G_CALLBACK(delete_history),r);button(history_buttons,"Clear history","insight-editor-ai-history-clear",G_CALLBACK(delete_history),r);gtk_widget_set_tooltip_text(history_buttons,r->prompt_path);

 gtk_box_pack_start(GTK_BOX(r->panel),gtk_label_new(insight_tr("Additional AI request — appended to earlier requirements")),FALSE,FALSE,0);r->prompt=view(r->panel,"insight-editor-ai-prompt",TRUE,55);bar=row(r->panel);r->send=button(bar,"Add request and generate variant","insight-editor-ai-send",G_CALLBACK(send_request),r);r->cancel_button=button(bar,"Cancel request","insight-editor-ai-cancel",G_CALLBACK(cancel_request),r);r->undo=button(bar,"Undo variant","insight-editor-ai-undo",G_CALLBACK(move_variant),r);r->redo=button(bar,"Redo variant","insight-editor-ai-redo",G_CALLBACK(move_variant),r);button(bar,"Recompare variant","insight-editor-ai-recompare",G_CALLBACK(retry_comparison),r);button(bar,"Prepare project context","insight-editor-ai-context-build",G_CALLBACK(build_context),r);
 r->requirements=view(expander(r->panel,"Accumulated requirements for this variant"),"insight-editor-ai-requirements",FALSE,90);r->context=view(expander(r->panel,"Project context sent with each request — review or edit before Send"),"insight-editor-ai-context",TRUE,100);r->status=gtk_label_new(insight_tr("New requests build on Result and earlier requirements. Review staged code before Apply; files are never saved automatically."));gtk_widget_set_name(r->status,"insight-editor-ai-status");gtk_label_set_line_wrap(GTK_LABEL(r->status),TRUE);gtk_box_pack_start(GTK_BOX(r->panel),r->status,FALSE,FALSE,0);
 GtkWidget *question=named(r->window,"insight-diff-ai-question"),*ask=named(r->window,"insight-diff-ai-ask");gtk_widget_hide(question);gtk_widget_set_no_show_all(question,TRUE);gtk_widget_hide(ask);gtk_widget_set_no_show_all(ask,TRUE);insight_diff_review_compact(r->window);prompt_lists(r);controls(r);
 GtkWidget *tabs=named(r->window,"insight-diff-toolbar-tabs");
 GtkWidget *request_page=insight_compare_ui_page(tabs,"AI request"),*files_page=insight_compare_ui_page(tabs,"AI files"),*settings_page=insight_compare_ui_page(tabs,"AI connection"),*history_page=insight_compare_ui_page(tabs,"AI request history");
 GtkWidget *pages[]={request_page,files_page,settings_page,history_page};
 for(guint i=0;i<G_N_ELEMENTS(pages);i++) g_object_set_data(G_OBJECT(gtk_widget_get_ancestor(pages[i],GTK_TYPE_SCROLLED_WINDOW)),"insight-editor-ai-controls",GINT_TO_POINTER(1));
 GList *children=gtk_container_get_children(GTK_CONTAINER(r->panel));
 GtkWidget *file_row=insight_compare_ui_row(r->file,r->panel),*context_row=insight_compare_ui_row(r->context,r->panel),*api_row=insight_compare_ui_row(r->provider,r->panel),*recent_row=insight_compare_ui_row(r->recent,r->panel),*requirements_row=insight_compare_ui_row(r->requirements,r->panel);
 for(GList *p=children;p;p=p->next) {
  GtkWidget *child=p->data,*page=child==file_row || child==context_row || !g_strcmp0(gtk_widget_get_name(child),"insight-panel-help-header") ? files_page:child==api_row ? settings_page:child==recent_row || child==requirements_row ? history_page:request_page;
  insight_compare_ui_move(child,child==r->status ? root:page);
 }
 g_list_free(children);gtk_widget_destroy(r->panel);r->panel=request_page;
 for(guint i=0;i<G_N_ELEMENTS(pages);i++) gtk_widget_show_all(gtk_widget_get_ancestor(pages[i],GTK_TYPE_SCROLLED_WINDOW));
 insight_ai_ui_fold(tabs,"Collapse");insight_ai_ui_window(r->window,"Minimize window","Maximize / restore window","Close window");gtk_widget_set_tooltip_text(r->prompt,insight_tr("Additional editable request included with earlier requirements; Send generates a reviewed code variant"));insight_compare_ui_reveal(r->prompt);
}
void insight_editor_ai_init(void) {
 if(initialized) { return; }
 initialized=TRUE;GtkWidget *menu=ui_lookup_widget(main_widgets.window,"tools1_menu");if(!menu) menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"tools1")));GtkWidget *item=gtk_menu_item_new_with_label(insight_tr("Editor / Project AI Assistant…"));gtk_widget_set_name(item,"insight-editor-ai-menu");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(open_review),NULL);gtk_widget_show(item);
}
