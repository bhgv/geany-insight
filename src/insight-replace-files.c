/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-replace-files.h"
#include "insight-project-report.h"
#include "insight-diff.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include "document.h"
#include "main.h"
#include "keybindings.h"
#include "filetypes.h"
#include "highlighting.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "../insight/desktop/replace_files.h"
#include <string.h>
#include <gdk/gdkkeysyms.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define STATE "insight-replace-files-state"
typedef struct {
 gchar *path,*filename,*native,*before,*after,*failure;guint id,count;gboolean readonly,selected,applied;
 InsightReplacePlan *plan;GeanyDocument *target;
} Source;
typedef struct {
 GPtrArray *sources;gchar *query,*replacement;InsightReplaceOptions options;guint64 serial;
} Job;
typedef struct {
 GtkWidget *window,*scope,*query,*replacement,*old_combo,*new_combo,*masks,*folder,*recursive,*region,*checks[6],*preview,*stop,*apply,*confirm,*confirm_hits,*status,*files,*hits,*labels[2];
 GtkListStore *file_model,*hit_model;ScintillaObject *code[2];GPtrArray *sources,*chosen;GCancellable *cancel;
 gboolean busy,closed,dirty,changing,applying;guint64 serial;gint selected;
} View;
enum { FILE_USE,FILE_PATH,FILE_COUNT,FILE_STATUS,FILE_INDEX,FILE_COLUMNS };
enum { HIT_USE,HIT_LINE,HIT_COLUMN,HIT_OLD,HIT_NEW,HIT_INDEX,HIT_COLUMNS };
static gboolean initialized;
static void choose_file(GtkTreeSelection *,gpointer);
static void save_settings(View *,Job *);
static void source_free(gpointer p) { Source *s=p;g_free(s->path);g_free(s->filename);g_free(s->native);g_free(s->before);g_free(s->after);g_free(s->failure);insight_replace_plan_free(s->plan);g_free(s); }
static void job_free(gpointer p) { Job *j=p;g_ptr_array_unref(j->sources);g_free(j->query);g_free(j->replacement);g_free(j); }
static void view_free(gpointer p) { View *v=p;g_clear_object(&v->cancel);g_ptr_array_unref(v->sources);g_ptr_array_unref(v->chosen);g_object_unref(v->file_model);g_object_unref(v->hit_model);g_free(v); }
static void destroyed(GtkWidget *w,gpointer p) { (void)w;View *v=p;v->closed=TRUE;if(v->cancel) g_cancellable_cancel(v->cancel); }
static void status(View *v,const gchar *s) { gtk_label_set_text(GTK_LABEL(v->status),insight_tr(s)); }
static void controls(View *v) {
 gint scope=gtk_combo_box_get_active(GTK_COMBO_BOX(v->scope));gtk_widget_set_sensitive(v->folder,scope==4&&!v->applying);gtk_widget_set_sensitive(v->recursive,scope==4&&!v->applying);gtk_widget_set_sensitive(v->checks[3],gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->checks[2]))&&!v->applying);
 gtk_widget_set_sensitive(v->preview,!v->busy&&!v->applying);gtk_widget_set_sensitive(v->stop,v->busy);gboolean selected=FALSE;for(guint i=0;i<v->sources->len;i++) { Source *s=g_ptr_array_index(v->sources,i);selected|=!s->applied&&s->selected&&s->count>0&&!s->failure&&!s->readonly; }
 gtk_widget_set_sensitive(v->apply,!v->busy&&!v->dirty&&!v->applying&&selected);g_object_set_data(G_OBJECT(v->window),"insight-replace-files-busy",GINT_TO_POINTER(v->busy));
}
static void changed(GtkWidget *w,gpointer p) { (void)w;View *v=p;if(v->changing) return;v->dirty=TRUE;v->serial++;if(v->cancel) g_cancellable_cancel(v->cancel);status(v,"Criteria changed. Preview again before applying; existing snapshots remain read-only.");controls(v); }
static void set_code(View *v,guint side,const gchar *text,const gchar *path) {
 ScintillaObject *sci=v->code[side];GeanyFiletype *ft=filetypes_detect_from_extension(path ? path:"");highlighting_set_styles(sci,ft ? ft:filetypes[GEANY_FILETYPES_NONE]);SEND(sci,SCI_SETREADONLY,FALSE,0);SEND(sci,SCI_SETUNDOCOLLECTION,FALSE,0);sci_set_text(sci,text ? text:"");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);SEND(sci,SCI_SETREADONLY,TRUE,0);sci_set_line_numbers(sci,TRUE);SEND(sci,SCI_SETMARGINWIDTHN,0,0);SEND(sci,SCI_SETMARGINWIDTHN,1,MAX(40,SEND(sci,SCI_GETMARGINWIDTHN,1,0)));SEND(sci,SCI_COLOURISE,0,-1);
}
static gchar *snippet(const gchar *text,gsize size) {
 const gchar *end=g_utf8_offset_to_pointer(text,MIN((glong)100,g_utf8_strlen(text,size)));gchar *s=g_strndup(text,MIN(size,(gsize)(end-text)));for(gchar *p=s;*p;p++) if(*p=='\n'||*p=='\r'||*p=='\t') *p=' ';return s;
}
static void populate_files(View *v) {
 v->changing=TRUE;gtk_list_store_clear(v->file_model);guint files=0,hits=0,errors=0;
 for(guint i=0;i<v->sources->len;i++) { Source *s=g_ptr_array_index(v->sources,i);GtkTreeIter it;gtk_list_store_append(v->file_model,&it);const gchar *state=s->applied ? insight_tr("Applied — use editor Undo"):s->failure ? s->failure:s->readonly ? insight_tr("Read-only — skipped"):!s->plan ? insight_tr("Not previewed"):!s->count ? insight_tr("No selected changes"):insight_tr("Ready — editor buffers only");gtk_list_store_set(v->file_model,&it,FILE_USE,s->selected,FILE_PATH,s->path,FILE_COUNT,s->count,FILE_STATUS,state,FILE_INDEX,i,-1);if(!s->applied&&s->selected&&s->count&&!s->failure&&!s->readonly) { files++;hits+=s->count; }errors+=s->failure!=NULL; }
 v->changing=FALSE;if(v->selected>=0&&(guint)v->selected<v->sources->len) { GtkTreePath *selected=gtk_tree_path_new_from_indices(v->selected,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(v->files),selected,NULL,FALSE);gtk_tree_path_free(selected); }gchar *text=g_strdup_printf("%u selected files, %u selected replacements, %u file errors. Apply changes buffers only; use normal Save/Save All afterwards.",files,hits,errors);status(v,text);g_free(text);controls(v);
}
static void show_source(View *v,guint index) {
 if(index>=v->sources->len) return;
 v->selected=index;Source *s=g_ptr_array_index(v->sources,index);gtk_list_store_clear(v->hit_model);
 if(s->plan) for(guint i=0;i<s->plan->hits->len;i++) { InsightReplaceHit *h=g_ptr_array_index(s->plan->hits,i);gchar *old=snippet(s->before+h->begin,h->end-h->begin),*replacement=snippet(h->replacement,strlen(h->replacement));GtkTreeIter it;gtk_list_store_append(v->hit_model,&it);gtk_list_store_set(v->hit_model,&it,HIT_USE,h->selected,HIT_LINE,h->line,HIT_COLUMN,h->column,HIT_OLD,old,HIT_NEW,replacement,HIT_INDEX,i,-1);g_free(old);g_free(replacement); }
 for(guint side=0;side<2;side++) { gtk_label_set_text(GTK_LABEL(v->labels[side]),side ? insight_tr("After selected replacements — read only"):s->path);set_code(v,side,side ? s->after:s->before,s->path); }
}
static void choose_file(GtkTreeSelection *selection,gpointer p) { View *v=p;if(v->changing) return;GtkTreeModel *model;GtkTreeIter it;if(gtk_tree_selection_get_selected(selection,&model,&it)) { guint i;gtk_tree_model_get(model,&it,FILE_INDEX,&i,-1);show_source(v,i); } }
static void hit_selected(GtkTreeSelection *selection,gpointer p) {
 View *v=p;if(v->selected<0||(guint)v->selected>=v->sources->len) return;Source *s=g_ptr_array_index(v->sources,v->selected);GtkTreeModel *model;GtkTreeIter it;if(!s->plan||!gtk_tree_selection_get_selected(selection,&model,&it)) return;guint i;gtk_tree_model_get(model,&it,HIT_INDEX,&i,-1);if(i>=s->plan->hits->len) return;InsightReplaceHit *h=g_ptr_array_index(s->plan->hits,i);SEND(v->code[0],SCI_SETSEL,h->begin,h->end);SEND(v->code[0],SCI_SCROLLCARET,0,0);if(h->selected) { SEND(v->code[1],SCI_SETSEL,h->after,h->after+strlen(h->replacement));SEND(v->code[1],SCI_SCROLLCARET,0,0); }
}
static void file_toggled(GtkCellRendererToggle *cell,gchar *path,gpointer p) {
 (void)cell;View *v=p;if(v->busy||v->dirty||v->applying) return;GtkTreeIter it;if(!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(v->file_model),&it,path)) return;guint i;gtk_tree_model_get(GTK_TREE_MODEL(v->file_model),&it,FILE_INDEX,&i,-1);Source *s=g_ptr_array_index(v->sources,i);if(s->applied||s->failure||s->readonly||!s->count) return;s->selected=!s->selected;populate_files(v);show_source(v,i);
}
static void update_render(View *v,Source *s) {
 GError *e=NULL;gchar *after=insight_replace_render(s->plan,&s->count,&e);if(!after) { g_free(s->failure);s->failure=g_strdup(e->message);g_clear_error(&e);s->selected=FALSE;populate_files(v);show_source(v,v->selected);return; }g_clear_pointer(&s->failure,g_free);g_free(s->after);s->after=after;if(!strcmp(s->before,s->after)) s->count=0;populate_files(v);show_source(v,v->selected);
}
static void hit_toggled(GtkCellRendererToggle *cell,gchar *path,gpointer p) {
 (void)cell;View *v=p;if(v->busy||v->dirty||v->applying||v->selected<0) return;GtkTreeIter it;if(!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(v->hit_model),&it,path)) return;Source *s=g_ptr_array_index(v->sources,v->selected);if(s->applied) return;guint i;gtk_tree_model_get(GTK_TREE_MODEL(v->hit_model),&it,HIT_INDEX,&i,-1);InsightReplaceHit *h=g_ptr_array_index(s->plan->hits,i);h->selected=!h->selected;update_render(v,s);
}
static void select_files(GtkWidget *w,gpointer p) {
 View *v=p;if(v->busy||v->dirty||v->applying) return;gboolean use=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),"use"));for(guint i=0;i<v->sources->len;i++) { Source *s=g_ptr_array_index(v->sources,i);s->selected=use&&!s->applied&&s->count&&!s->failure&&!s->readonly; }populate_files(v);
}
static void select_hits(GtkWidget *w,gpointer p) {
 View *v=p;if(v->busy||v->dirty||v->applying||v->selected<0) return;Source *s=g_ptr_array_index(v->sources,v->selected);if(s->applied||!s->plan) return;gboolean use=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),"use"));for(guint i=0;i<s->plan->hits->len;i++) ((InsightReplaceHit *)g_ptr_array_index(s->plan->hits,i))->selected=use;update_render(v,s);
}
/* Continue the immutable preview rather than searching changed buffers again. */
static void next_batch(GtkWidget *w,gpointer p) {
 (void)w;View *v=p;if(v->busy||v->dirty||v->applying) return;guint n=0;
 for(guint i=0;i<v->sources->len;i++) { Source *s=g_ptr_array_index(v->sources,i);s->selected=!s->applied&&s->count&&!s->failure&&!s->readonly&&n++<128; }
 populate_files(v);
}
static void navigate_match(GtkWidget *w,gpointer p) {
 View *v=p;if(v->busy||v->applying||!v->sources->len) return;
 gint direction=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),"direction"));gint source=MAX(v->selected,0),hit=-1;GtkTreeIter it;GtkTreeModel *model;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->hits)),&model,&it)) { guint index;gtk_tree_model_get(model,&it,HIT_INDEX,&index,-1);hit=index; }
 for(guint n=0;n<=v->sources->len;n++) {
  Source *s=g_ptr_array_index(v->sources,source);gint count=s->plan ? (gint)s->plan->hits->len:0;
  gint next=n==0 ? (hit<0 ? (direction>0 ? 0:count-1):hit+direction):(direction>0 ? 0:count-1);
  if(next>=0&&next<count) {
   GtkTreePath *path=gtk_tree_path_new_from_indices(source,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(v->files),path,NULL,FALSE);gtk_tree_path_free(path);
   path=gtk_tree_path_new_from_indices(next,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(v->hits),path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(v->hits),path,NULL,FALSE,0,0);gtk_tree_path_free(path);return;
  }
  source=(source+direction+(gint)v->sources->len)%v->sources->len;
 }
}
static gboolean match_key(GtkWidget *w,GdkEventKey *event,gpointer p) {
 if(event->keyval!=GDK_KEY_F3||(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK))) return FALSE;
 g_object_set_data(G_OBJECT(w),"direction",GINT_TO_POINTER(event->state&GDK_SHIFT_MASK ? -1:1));navigate_match(w,p);return TRUE;
}
static InsightCodeDialect dialect(const gchar *path,gboolean *known) {
 const gchar *ext=strrchr(path,'.');*known=TRUE;if(ext&&(!g_ascii_strcasecmp(ext,".py")||!g_ascii_strcasecmp(ext,".pyw"))) return INSIGHT_CODE_PYTHON;if(ext&&!g_ascii_strcasecmp(ext,".lua")) return INSIGHT_CODE_LUA;if(ext&&(!g_ascii_strcasecmp(ext,".js")||!g_ascii_strcasecmp(ext,".jsx")||!g_ascii_strcasecmp(ext,".ts")||!g_ascii_strcasecmp(ext,".tsx"))) return INSIGHT_CODE_JS;if(ext&&(!g_ascii_strcasecmp(ext,".sh")||!g_ascii_strcasecmp(ext,".bash")||!g_ascii_strcasecmp(ext,".zsh"))) return INSIGHT_CODE_SHELL;
 const gchar *c[]={".c",".h",".cc",".cpp",".cxx",".hpp",".hh",".m",".mm",".cs",".java"};for(guint i=0;ext&&i<G_N_ELEMENTS(c);i++) if(!g_ascii_strcasecmp(ext,c[i])) return INSIGHT_CODE_C;*known=FALSE;return INSIGHT_CODE_C;
}
static gboolean disk_readonly(const gchar *path) {
 GFile *file=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE "," G_FILE_ATTRIBUTE_UNIX_MODE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,NULL);g_object_unref(file);if(!info) return TRUE;
 gboolean readonly=(g_file_info_has_attribute(info,G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE)&&!g_file_info_get_attribute_boolean(info,G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE)) || (g_file_info_has_attribute(info,G_FILE_ATTRIBUTE_UNIX_MODE)&&!(g_file_info_get_attribute_uint32(info,G_FILE_ATTRIBUTE_UNIX_MODE)&0222));g_object_unref(info);return readonly;
}
static void worker(GTask *task,gpointer object,gpointer data,GCancellable *cancel) {
 (void)object;Job *j=data;GError *error=NULL;gsize total=0,hits=0,result_size=0;guint selected_files=0;
 /* Validate the query and captures even for an empty input scope. */
 InsightReplaceOptions probe=j->options;probe.region=INSIGHT_REGION_ALL;InsightReplacePlan *valid=insight_replace_plan("",j->query,j->replacement,&probe,cancel,&error);if(!valid) goto failed;insight_replace_plan_free(valid);
 for(guint i=0;i<j->sources->len;i++) {
  if(g_cancellable_set_error_if_cancelled(cancel,&error)) goto failed;
  Source *s=g_ptr_array_index(j->sources,i);GError *e=NULL;if(!s->before) s->before=insight_replace_read(s->native,cancel,&e);
  if(!s->before) { s->failure=g_strdup(e ? e->message:"Cannot read source.");g_clear_error(&e);continue; }
  if(!s->id&&s->native) s->readonly=disk_readonly(s->native);
  total+=strlen(s->before);if(total>16u*1024u*1024u) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Preview source total exceeds 16 MiB. Narrow the scope or masks.");goto failed; }
  InsightReplaceOptions options=j->options;gboolean known;options.dialect=dialect(s->path,&known);if(options.region!=INSIGHT_REGION_ALL&&!known) { s->failure=g_strdup("Comment-region filtering is unsupported for this file type.");continue; }
  s->plan=insight_replace_plan(s->before,j->query,j->replacement,&options,cancel,&e);if(!s->plan) { s->failure=g_strdup(e ? e->message:"Cannot plan replacements.");g_clear_error(&e);continue; }
  s->after=insight_replace_render(s->plan,&s->count,&e);if(!s->after) { s->failure=g_strdup(e->message);g_clear_error(&e);continue; }hits+=s->plan->hits->len;result_size+=strlen(s->after);
  if(hits>100000||result_size>32u*1024u*1024u) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Preview limit: 100,000 matches and 32 MiB results. Nothing changed.");goto failed; }
  if(!strcmp(s->before,s->after)) s->count=0;
  s->selected=s->count&&!s->readonly&&selected_files++<128;
 }
 g_task_return_boolean(task,TRUE);return;
failed:g_task_return_error(task,error);
}
static void done(GObject *object,GAsyncResult *result,gpointer data) {
 (void)data;View *v=g_object_get_data(object,STATE);Job *j=g_task_get_task_data(G_TASK(result));GError *e=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&e);if(v->closed) { g_clear_error(&e);return; }v->busy=FALSE;g_clear_object(&v->cancel);
 if(!ok||v->serial!=j->serial) { status(v,e ? e->message:"Preview superseded. Run Preview again.");g_clear_error(&e);controls(v);return; }
 g_ptr_array_unref(v->sources);v->sources=j->sources;j->sources=g_ptr_array_new_with_free_func(source_free);v->dirty=FALSE;v->selected=-1;save_settings(v,j);populate_files(v);gtk_list_store_clear(v->hit_model);for(guint side=0;side<2;side++) set_code(v,side,"",NULL);if(v->sources->len) { GtkTreePath *path=gtk_tree_path_new_first();gtk_tree_view_set_cursor(GTK_TREE_VIEW(v->files),path,NULL,FALSE);gtk_tree_path_free(path); }controls(v);
}
static gchar *document_text(GeanyDocument *d,GError **error) {
 sptr_t n=SEND(d->editor->sci,SCI_GETLENGTH,0,0);if(n<0||n>INSIGHT_REPLACE_FILE_LIMIT) goto invalid;gchar *text=sci_get_contents(d->editor->sci,-1);if(memchr(text,0,n)||!g_utf8_validate(text,n,NULL)) { g_free(text);goto invalid; }return text;
invalid:g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Open source exceeds 1 MiB or is not NUL-free UTF-8.");return NULL;
}
static gboolean add_input(GPtrArray *sources,GHashTable *seen,const gchar *filename,GeanyDocument *d,const gchar *masks,gsize *bytes,GError **error) {
 gchar *label=filename ? g_strdup(filename):g_strdup_printf("Untitled #%u",d->id);if(g_hash_table_contains(seen,label)) { g_free(label);return TRUE; }if(!insight_replace_mask(label,masks,error)) { g_free(label);return !error||!*error; }
 if(sources->len>=2000) { g_free(label);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Scope exceeds 2,000 files. Narrow masks or choose files.");return FALSE; }
 Source *s=g_new0(Source,1);s->path=label;s->filename=g_strdup(filename);if(!d&&filename) d=document_find_by_filename(filename);
 if(d) { s->id=d->id;s->readonly=d->readonly||SEND(d->editor->sci,SCI_GETREADONLY,0,0);s->before=document_text(d,error);if(!s->before) { source_free(s);return FALSE; }*bytes+=strlen(s->before);if(*bytes>16u*1024u*1024u) { source_free(s);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Open source snapshots exceed 16 MiB.");return FALSE; } }
 else { s->native=g_filename_from_utf8(filename,-1,NULL,NULL,error);if(!s->native) { source_free(s);return FALSE; } }
 g_hash_table_add(seen,g_strdup(label));g_ptr_array_add(sources,s);return TRUE;
}
static gboolean folder_inputs(GPtrArray *sources,GHashTable *seen,GFile *folder,gboolean recursive,const gchar *masks,guint depth,guint *visited,gsize *bytes,GError **error) {
 if(depth>32||*visited>10000) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Folder traversal limit: depth 32 and 10,000 entries.");return FALSE; }
 GFileEnumerator *en=g_file_enumerate_children(folder,G_FILE_ATTRIBUTE_STANDARD_NAME","G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);if(!en) return FALSE;GFileInfo *info;gboolean ok=TRUE;
 while(ok&&(info=g_file_enumerator_next_file(en,NULL,error))) { (*visited)++;GFile *child=g_file_get_child(folder,g_file_info_get_name(info));GFileType type=g_file_info_get_file_type(info);if(*visited>10000) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Folder traversal exceeds 10,000 entries.");ok=FALSE; }
  else if(type==G_FILE_TYPE_DIRECTORY&&recursive&&strcmp(g_file_info_get_name(info),".git")&&strcmp(g_file_info_get_name(info),".hg")&&strcmp(g_file_info_get_name(info),".svn")) ok=folder_inputs(sources,seen,child,recursive,masks,depth+1,visited,bytes,error);
  else if(type==G_FILE_TYPE_REGULAR) { gchar *native=g_file_get_path(child),*utf8=native ? g_filename_to_utf8(native,-1,NULL,NULL,error):NULL;if(!utf8) ok=FALSE;else ok=add_input(sources,seen,utf8,NULL,masks,bytes,error);g_free(utf8);g_free(native); }
  g_object_unref(child);g_object_unref(info);
 }if(error&&*error) ok=FALSE;g_object_unref(en);return ok;
}
static void preview(GtkWidget *w,gpointer p) {
 (void)w;View *v=p;if(v->busy||v->applying) return;Job *j=g_new0(Job,1);j->sources=g_ptr_array_new_with_free_func(source_free);j->query=g_strdup(gtk_entry_get_text(GTK_ENTRY(v->query)));j->replacement=g_strdup(gtk_entry_get_text(GTK_ENTRY(v->replacement)));j->serial=++v->serial;gboolean flags[6];for(guint i=0;i<6;i++) flags[i]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->checks[i]));j->options=(InsightReplaceOptions){flags[0],flags[1],flags[2],flags[3],flags[4],flags[5],gtk_combo_box_get_active(GTK_COMBO_BOX(v->region)),INSIGHT_CODE_C};
 const gchar *masks=gtk_entry_get_text(GTK_ENTRY(v->masks));GHashTable *seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);gsize bytes=0;GError *e=NULL;gboolean ok=TRUE;gint scope=gtk_combo_box_get_active(GTK_COMBO_BOX(v->scope));
 if(scope==0) { InsightReportSnapshot *snapshot=insight_project_report_capture(&e);if(!snapshot) ok=FALSE;else { for(guint i=0;ok&&i<snapshot->paths->len;i++) ok=add_input(j->sources,seen,g_ptr_array_index(snapshot->paths,i),NULL,masks,&bytes,&e);insight_report_snapshot_unref(snapshot); } }
 else if(scope==1||scope==2) { guint i;foreach_document(i) { GeanyDocument *d=documents[i];if(scope==2&&d!=document_get_current()) continue;ok=add_input(j->sources,seen,d->file_name,d,masks,&bytes,&e);if(!ok) break; } }
 else if(scope==3) { for(guint i=0;ok&&i<v->chosen->len;i++) ok=add_input(j->sources,seen,g_ptr_array_index(v->chosen,i),NULL,masks,&bytes,&e); }
 else { gchar *native=g_filename_from_utf8(gtk_entry_get_text(GTK_ENTRY(v->folder)),-1,NULL,NULL,&e);if(!native||!*native) ok=FALSE;else { GFile *folder=g_file_new_for_path(native);guint visited=0;ok=folder_inputs(j->sources,seen,folder,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->recursive)),masks,0,&visited,&bytes,&e);g_object_unref(folder); }g_free(native); }
 g_hash_table_unref(seen);if(!ok||!j->sources->len) { status(v,e ? e->message:"No input files. Choose a scope and matching masks first.");g_clear_error(&e);job_free(j);v->dirty=TRUE;controls(v);return; }
 v->dirty=TRUE;v->busy=TRUE;v->cancel=g_cancellable_new();status(v,"Building replacement preview. No editor buffers or disk files are being changed…");controls(v);GTask *task=g_task_new(v->window,v->cancel,done,NULL);g_task_set_task_data(task,j,job_free);g_task_run_in_thread(task,worker);g_object_unref(task);
}
static void stop(GtkWidget *w,gpointer p) { (void)w;View *v=p;if(v->cancel) g_cancellable_cancel(v->cancel); }
static gboolean source_matches(Source *s,gboolean opened,GError **error) {
 s->target=NULL;GeanyDocument *d=s->id ? document_find_by_id(s->id):document_find_by_filename(s->filename);if(s->id&&(!d||g_strcmp0(d->file_name,s->filename))) goto stale;
 gchar *now=NULL;if(d) { if(d->readonly||SEND(d->editor->sci,SCI_GETREADONLY,0,0)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_PERMISSION_DENIED,"A selected buffer is read-only. Deselect it and preview again.");return FALSE; }now=document_text(d,error);s->target=d; }
 else { if(opened) goto stale;if(disk_readonly(s->native)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_PERMISSION_DENIED,"A selected source is read-only or unavailable.");return FALSE; }now=insight_replace_read(s->native,NULL,error); }
 if(!now) return FALSE;
 gboolean same=!strcmp(now,s->before);g_free(now);if(!same) goto stale;return TRUE;
stale:g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"A selected source changed, closed or was renamed. Nothing further was changed; preview again.");return FALSE;
}
/* Decisions are staged independently of both source and visible plan. Cancel
 * therefore leaves every buffer and every occurrence checkbox unchanged. */
typedef struct { Source *source;GArray *choices;gchar *after;guint count; } Review;
static void review_free(gpointer data) {
 Review *r=data;g_array_unref(r->choices);g_free(r->after);g_free(r);
}
static gint review_dialog(View *v,const gchar *name,const gchar *message,gboolean occurrence) {
 GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(v->window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"%s",message);
 gtk_widget_set_name(d,name);gtk_dialog_add_buttons(GTK_DIALOG(d),insight_tr("Cancel batch"),GTK_RESPONSE_CANCEL,insight_tr(occurrence ? "Skip replacement":"Skip file"),GTK_RESPONSE_NO,insight_tr(occurrence ? "Replace occurrence":"Replace in buffer"),GTK_RESPONSE_YES,NULL);
 if(occurrence) gtk_dialog_add_button(GTK_DIALOG(d),insight_tr("Replace remaining"),GTK_RESPONSE_APPLY);
 g_object_ref_sink(d);gint response=gtk_dialog_run(GTK_DIALOG(d));gtk_widget_destroy(d);g_object_unref(d);return response;
}
static gboolean review_valid(View *v,guint64 serial,GError **error) {
 if(v->closed) return FALSE;
 if(v->dirty||v->serial!=serial) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"Criteria changed during review. Preview again; no further replacements applied.");return FALSE; }
 return TRUE;
}
static void apply(GtkWidget *w,gpointer p) {
 (void)w;View *v=p;if(v->busy||v->dirty||v->applying) return;
 GPtrArray *selected=g_ptr_array_new(),*reviews=g_ptr_array_new_with_free_func(review_free);GError *e=NULL;gboolean held=FALSE;guint applied=0;
 for(guint i=0;i<v->sources->len;i++) {
  Source *s=g_ptr_array_index(v->sources,i);if(s->applied||!s->selected||!s->count||s->failure||s->readonly) continue;
  g_ptr_array_add(selected,s);
  if(selected->len>128) { status(v,"Apply at most 128 files at once. Use Select next 128 files or deselect files.");goto out; }
  if(!source_matches(s,FALSE,&e)) goto out;
 }
 if(!selected->len) goto out;
 guint64 serial=v->serial;v->applying=TRUE;held=TRUE;controls(v);g_object_ref(v->window);
 gboolean ask_files=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->confirm));
 gboolean ask_hits=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->confirm_hits)),remaining=FALSE;gsize bytes=0;
 /* Finish ALL file choices before any occurrence choices or buffer edits. */
 if(ask_files) for(guint i=0;i<selected->len;i++) {
  Source *s=g_ptr_array_index(selected,i);gchar *message=g_strdup_printf("%s\n%u replacements",s->path,s->count);
  gint response=review_dialog(v,"insight-replace-files-confirm",message,FALSE);g_free(message);
  if(!review_valid(v,serial,&e)) goto out;
  if(response==GTK_RESPONSE_NO) { g_ptr_array_remove_index(selected,i--);continue; }
  if(response!=GTK_RESPONSE_YES) goto cancelled;
 }
 for(guint i=0;i<selected->len;i++) {
  Source *s=g_ptr_array_index(selected,i);Review *r=g_new0(Review,1);r->source=s;r->choices=g_array_sized_new(FALSE,FALSE,sizeof(gboolean),s->plan->hits->len);g_ptr_array_add(reviews,r);
  for(guint j=0;j<s->plan->hits->len;j++) {
   InsightReplaceHit *h=g_ptr_array_index(s->plan->hits,j);gboolean use=h->selected;
   if(use&&ask_hits&&!remaining) {
    /* Highlight the exact candidate in the real colored/numbered preview. */
    for(guint k=0;k<v->sources->len;k++) if(g_ptr_array_index(v->sources,k)==s) { GtkTreePath *path=gtk_tree_path_new_from_indices(k,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(v->files),path,NULL,FALSE);gtk_tree_path_free(path);break; }
    GtkTreePath *path=gtk_tree_path_new_from_indices(j,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(v->hits),path,NULL,FALSE);gtk_tree_path_free(path);
    gchar *old=snippet(s->before+h->begin,h->end-h->begin),*replacement=snippet(h->replacement,strlen(h->replacement));
    gchar *message=g_strdup_printf("%s:%u:%u\n%s → %s",s->path,h->line,h->column,old,replacement);g_free(old);g_free(replacement);
    gint response=review_dialog(v,"insight-replace-files-occurrence-confirm",message,TRUE);g_free(message);
    if(!review_valid(v,serial,&e)) goto out;
    if(response==GTK_RESPONSE_APPLY) remaining=TRUE;
    else if(response==GTK_RESPONSE_NO) use=FALSE;
    else if(response!=GTK_RESPONSE_YES) goto cancelled;
   }
   g_array_append_val(r->choices,use);
  }
  /* Independent review vector: no checkbox or offset mutation before Apply. */
  r->after=insight_replace_review_render(s->plan,(const gboolean *)r->choices->data,r->choices->len,&r->count,&e);
  if(!r->after) goto out;
  if(!strcmp(r->after,s->before)) r->count=0;
  bytes+=strlen(r->after);if(bytes>32u*1024u*1024u) { g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Reviewed results exceed 32 MiB. Nothing applied; select fewer files.");goto out; }
 }
 if(!review_valid(v,serial,&e)) goto out;
 for(guint i=0;i<reviews->len;i++) {
  Review *r=g_ptr_array_index(reviews,i);if(!r->count) continue;Source *s=r->source;
  if(!source_matches(s,FALSE,&e)) goto out;
  if(!s->target) { s->target=document_open_file(s->native,FALSE,NULL,NULL);if(!s->target) { g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot open a selected source. No planned replacements applied.");goto out; } }
  if(!review_valid(v,serial,&e)) goto out;
 }
 /* Recheck the entire accepted batch after dialogs and file opening. */
 GHashTable *ids=g_hash_table_new(g_direct_hash,g_direct_equal);gboolean ready=TRUE;
 for(guint i=0;i<reviews->len;i++) {
  Review *r=g_ptr_array_index(reviews,i);if(!r->count) continue;Source *s=r->source;
  if(!source_matches(s,TRUE,&e)||g_hash_table_contains(ids,GUINT_TO_POINTER(s->target->id))) { if(!e) g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_EXISTS,"Selected paths resolve to the same editor buffer. Select it only once.");ready=FALSE;break; }
  g_hash_table_add(ids,GUINT_TO_POINTER(s->target->id));
 }
 g_hash_table_unref(ids);if(!ready) goto out;
 for(guint i=0;i<reviews->len;i++) {
  if(!review_valid(v,serial,&e)) break;
  Review *r=g_ptr_array_index(reviews,i);Source *s=r->source;if(!r->count) { s->selected=FALSE;continue; }
  if(!source_matches(s,TRUE,&e)) break;
  ScintillaObject *sci=s->target->editor->sci;SEND(sci,SCI_BEGINUNDOACTION,0,0);SEND(sci,SCI_SETTARGETRANGE,0,strlen(s->before));SEND(sci,SCI_REPLACETARGET,strlen(r->after),(sptr_t)r->after);SEND(sci,SCI_ENDUNDOACTION,0,0);
  for(guint j=0;j<s->plan->hits->len;j++) ((InsightReplaceHit *)g_ptr_array_index(s->plan->hits,j))->selected=g_array_index(r->choices,gboolean,j);
  g_free(s->after);s->after=insight_replace_render(s->plan,&s->count,NULL);s->applied=TRUE;s->selected=FALSE;applied++;
 }
 if(!v->closed) { populate_files(v);gchar *message=g_strdup_printf("Applied to %u buffers, one Undo per file. Disk unchanged. %s",applied,e ? e->message:"Select next 128 files to continue the remaining plan; normal Save/Save All saves buffers.");status(v,message);g_free(message); }goto out;
cancelled:
 status(v,"Replacement batch cancelled before buffer changes. Occurrence choices unchanged.");
out:
 if(e&&!v->closed) { gchar *message=g_strdup_printf("%s Applied buffers: %u. Undo those individually if needed.",e->message,applied);status(v,message);g_free(message);v->dirty=TRUE; }
 g_clear_error(&e);g_ptr_array_unref(reviews);g_ptr_array_unref(selected);v->applying=FALSE;if(!v->closed) controls(v);if(held) g_object_unref(v->window);
}
static void open_source(GtkWidget *w,gpointer p) {
 (void)w;View *v=p;if(v->busy||v->applying||v->selected<0) return;Source *s=g_ptr_array_index(v->sources,v->selected);g_object_ref(v->window);
 GeanyDocument *d=s->id ? document_find_by_id(s->id):document_find_by_filename(s->filename);
 if(s->id&&(!d||g_strcmp0(d->file_name,s->filename))) { status(v,"Source closed or renamed. Preview again before navigating.");goto out; }
 if(!d) d=document_open_file(s->native,FALSE,NULL,NULL);
 if(v->closed) goto out;
 if(!d) { status(v,"Cannot open preview source in the editor.");goto out; }
 guint id=d->id;document_show_tab(d);
 /* Tab activation can synchronously close the source or this review window. */
 if(v->closed) goto out;
 d=document_find_by_id(id);if(!d) { status(v,"Source closed during tab activation. Preview again before navigating.");goto out; }
 gchar *text=document_text(d,NULL);const gchar *expected=s->applied ? s->after:s->before;
 if(text&&expected&&!strcmp(text,expected)) {
  GtkTreeModel *model;GtkTreeIter it;guint index=0;
  if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->hits)),&model,&it)) gtk_tree_model_get(model,&it,HIT_INDEX,&index,-1);
  if(s->plan&&index<s->plan->hits->len) { InsightReplaceHit *h=g_ptr_array_index(s->plan->hits,index);gsize pos=s->applied ? h->after:h->begin;SEND(d->editor->sci,SCI_GOTOPOS,MIN(pos,strlen(text)),0);d=document_find_by_id(id);if(v->closed||!d) { g_free(text);goto out; }SEND(d->editor->sci,SCI_SCROLLCARET,0,0); }
  if(v->closed) { g_free(text);goto out; }
  status(v,"Source opened. This preview stays open; Preview again after manual source edits.");
 }else status(v,"Source differs from preview. Opened without using stale match offsets; Preview again before applying.");
 g_free(text);d=document_find_by_id(id);if(!v->closed&&d) gtk_widget_grab_focus(GTK_WIDGET(d->editor->sci));
out:g_object_unref(v->window);
}
static void choose_inputs(GtkWidget *w,gpointer p) {
 View *v=p;if(v->busy||v->applying) return;gboolean folder=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),"folder"));GtkWidget *d=gtk_file_chooser_dialog_new(insight_tr(folder ? "Choose replacement folder":"Choose files for replacement"),GTK_WINDOW(v->window),folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER:GTK_FILE_CHOOSER_ACTION_OPEN,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Choose"),GTK_RESPONSE_ACCEPT,NULL);gtk_widget_set_name(d,"insight-replace-files-chooser");gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(d),!folder);g_object_ref_sink(d);g_object_ref(v->window);
 if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT&&!v->closed) { GSList *paths=gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(d));if(!folder) g_ptr_array_set_size(v->chosen,0);for(GSList *it=paths;it;it=it->next) { gchar *s=g_filename_to_utf8(it->data,-1,NULL,NULL,NULL);if(s) { if(folder) { gtk_entry_set_text(GTK_ENTRY(v->folder),s);g_free(s); }else g_ptr_array_add(v->chosen,s); } }g_slist_free_full(paths,g_free);gtk_combo_box_set_active(GTK_COMBO_BOX(v->scope),folder ? 4:3);changed(NULL,v); }gtk_widget_destroy(d);g_object_unref(d);g_object_unref(v->window);
}
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=list;p&&!found;p=p->next) found=named(p->data,name);g_list_free(list);return found; }return NULL; }
static void compare_preview(GtkWidget *w,gpointer p) {
 (void)w;View *v=p;if(v->busy||v->selected<0) return;Source *s=g_ptr_array_index(v->sources,v->selected);if(!s->before||!s->after) return;GtkWidget *d=insight_diff_review_create(v->window,s->before,s->after,"Replace Files snapshot preview. Change patterns or individual choices in Replace Files, then Preview again. No edits from this window are applied to source.",NULL,NULL,NULL,NULL);if(!d) return;
 const gchar *hide[]={"insight-diff-ai-apply","insight-diff-ai-ask","insight-diff-ai-question"};for(guint i=0;i<3;i++) { GtkWidget *item=named(d,hide[i]);gtk_widget_hide(item);gtk_widget_set_no_show_all(item,TRUE); }gtk_text_view_set_editable(GTK_TEXT_VIEW(named(d,"insight-diff-result")),FALSE);gtk_window_set_title(GTK_WINDOW(d),insight_tr("Replace Files — Diff preview"));
}
static GKeyFile *settings_load(void) {
 GKeyFile *k=g_key_file_new();gchar *path=g_build_filename(app->configdir,"insight-replace-files.conf",NULL),*raw=insight_replace_read(path,NULL,NULL);if(raw) g_key_file_load_from_data(k,raw,strlen(raw),G_KEY_FILE_NONE,NULL);g_free(raw);g_free(path);return k;
}
static void restore_settings(View *v) {
 GKeyFile *k=settings_load();v->changing=TRUE;const gchar *names[]={"old","new"};GtkWidget *combos[]={v->old_combo,v->new_combo};for(guint i=0;i<2;i++) { gsize n=0;gchar **list=g_key_file_get_string_list(k,"history",names[i],&n,NULL);for(guint j=0;j<MIN(n,20);j++) if(strlen(list[j])<=8192&&g_utf8_validate(list[j],-1,NULL)) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combos[i]),list[j]);if(n&&strlen(list[0])<=8192) gtk_entry_set_text(GTK_ENTRY(i ? v->replacement:v->query),list[0]);g_strfreev(list); }
 gchar *masks=g_key_file_get_string(k,"options","masks",NULL);if(masks&&strlen(masks)<=4096) gtk_entry_set_text(GTK_ENTRY(v->masks),masks);g_free(masks);for(guint i=0;i<6;i++) { gchar *name=g_strdup_printf("flag-%u",i);GError *e=NULL;gboolean value=g_key_file_get_boolean(k,"options",name,&e);if(!e) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->checks[i]),value);g_clear_error(&e);g_free(name); }
 GError *e=NULL;gint region=g_key_file_get_integer(k,"options","region",&e);if(!e&&region>=0&&region<4) gtk_combo_box_set_active(GTK_COMBO_BOX(v->region),region);g_clear_error(&e);v->changing=FALSE;g_key_file_unref(k);
}
static void save_settings(View *v,Job *j) {
 GKeyFile *k=settings_load();const gchar *names[]={"old","new"},*values[]={j->query,j->replacement};GtkWidget *combos[]={v->old_combo,v->new_combo};v->changing=TRUE;
 for(guint i=0;i<2;i++) { GPtrArray *next=g_ptr_array_new_with_free_func(g_free);g_ptr_array_add(next,g_strdup(values[i]));gsize n=0;gchar **old=g_key_file_get_string_list(k,"history",names[i],&n,NULL);for(guint x=0;x<n&&next->len<20;x++) if(strcmp(old[x],values[i])&&strlen(old[x])<=8192) g_ptr_array_add(next,g_strdup(old[x]));g_strfreev(old);g_key_file_set_string_list(k,"history",names[i],(const gchar *const *)next->pdata,next->len);gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(combos[i]));for(guint x=0;x<next->len;x++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combos[i]),g_ptr_array_index(next,x));gtk_entry_set_text(GTK_ENTRY(i ? v->replacement:v->query),values[i]);g_ptr_array_unref(next); }
 g_key_file_set_string(k,"options","masks",gtk_entry_get_text(GTK_ENTRY(v->masks)));for(guint i=0;i<6;i++) { gchar *name=g_strdup_printf("flag-%u",i);g_key_file_set_boolean(k,"options",name,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->checks[i])));g_free(name); }g_key_file_set_integer(k,"options","region",gtk_combo_box_get_active(GTK_COMBO_BOX(v->region)));gsize n;gchar *raw=g_key_file_to_data(k,&n,NULL),*path=g_build_filename(app->configdir,"insight-replace-files.conf",NULL);GFile *file=g_file_new_for_path(path);g_file_replace_contents(file,raw,n,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,NULL);g_object_unref(file);g_free(raw);g_free(path);g_key_file_unref(k);v->changing=FALSE;
}
static GtkWidget *row(GtkWidget *box) { GtkWidget *r=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),r,FALSE,FALSE,0);return r; }
static GtkWidget *button(GtkWidget *box,const gchar *label,const gchar *name,GCallback cb,View *v) { GtkWidget *b=gtk_button_new_with_label(insight_tr(label));gtk_widget_set_name(b,name);gtk_box_pack_start(GTK_BOX(box),b,FALSE,FALSE,0);g_signal_connect(b,"clicked",cb,v);return b; }
static GtkWidget *scroll(GtkWidget *widget) { GtkWidget *s=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(s),widget);return s; }
void insight_replace_files_command(void) {
 View *v=g_new0(View,1);v->sources=g_ptr_array_new_with_free_func(source_free);v->chosen=g_ptr_array_new_with_free_func(g_free);v->selected=-1;v->dirty=TRUE;v->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(v->window,"insight-replace-files-window");gtk_window_set_title(GTK_WINDOW(v->window),insight_tr("Replace Files"));gtk_window_set_default_size(GTK_WINDOW(v->window),1200,850);gtk_window_set_transient_for(GTK_WINDOW(v->window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(v->window),TRUE);g_object_set_data_full(G_OBJECT(v->window),STATE,v,view_free);g_signal_connect(v->window,"destroy",G_CALLBACK(destroyed),v);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_container_set_border_width(GTK_CONTAINER(box),6);gtk_container_add(GTK_CONTAINER(v->window),box);insight_panel_help_attach(box,"replace-files.md");GtkWidget *bar=row(box);gtk_box_pack_start(GTK_BOX(bar),gtk_label_new(insight_tr("Old")),FALSE,FALSE,0);v->old_combo=gtk_combo_box_text_new_with_entry();v->query=gtk_bin_get_child(GTK_BIN(v->old_combo));gtk_widget_set_name(v->query,"insight-replace-files-query");gtk_box_pack_start(GTK_BOX(bar),v->old_combo,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(bar),gtk_label_new(insight_tr("New")),FALSE,FALSE,0);v->new_combo=gtk_combo_box_text_new_with_entry();v->replacement=gtk_bin_get_child(GTK_BIN(v->new_combo));gtk_widget_set_name(v->replacement,"insight-replace-files-new");gtk_box_pack_start(GTK_BOX(bar),v->new_combo,TRUE,TRUE,0);
 GtkWidget *combos[]={v->old_combo,v->new_combo};for(guint i=0;i<2;i++) { GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(combos[i]));for(GList *it=cells;it;it=it->next) g_object_set(it->data,"ellipsize",PANGO_ELLIPSIZE_END,"max-width-chars",40,NULL);g_list_free(cells); }gtk_entry_set_max_width_chars(GTK_ENTRY(v->query),60);gtk_entry_set_max_width_chars(GTK_ENTRY(v->replacement),60);
 bar=row(box);v->scope=gtk_combo_box_text_new();gtk_widget_set_name(v->scope,"insight-replace-files-scope");const gchar *scopes[]={"Project files","Open documents","Current document","Chosen files","Folder"};for(guint i=0;i<5;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(v->scope),insight_tr(scopes[i]));gtk_combo_box_set_active(GTK_COMBO_BOX(v->scope),app->project ? 0:1);gtk_box_pack_start(GTK_BOX(bar),v->scope,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(bar),gtk_label_new(insight_tr("Filename masks (; separated)")),FALSE,FALSE,0);v->masks=gtk_entry_new();gtk_widget_set_name(v->masks,"insight-replace-files-masks");gtk_entry_set_text(GTK_ENTRY(v->masks),"*");gtk_box_pack_start(GTK_BOX(bar),v->masks,TRUE,TRUE,0);button(bar,"Choose files…","insight-replace-files-choose",G_CALLBACK(choose_inputs),v);GtkWidget *folder_button=button(bar,"Choose folder…","insight-replace-files-folder-button",G_CALLBACK(choose_inputs),v);g_object_set_data(G_OBJECT(folder_button),"folder",GINT_TO_POINTER(1));
 bar=row(box);v->folder=gtk_entry_new();gtk_widget_set_name(v->folder,"insight-replace-files-folder");gtk_entry_set_placeholder_text(GTK_ENTRY(v->folder),insight_tr("Folder path for Folder scope"));gtk_box_pack_start(GTK_BOX(bar),v->folder,TRUE,TRUE,0);v->recursive=gtk_check_button_new_with_label(insight_tr("Include subdirectories"));gtk_widget_set_name(v->recursive,"insight-replace-files-recursive");gtk_box_pack_start(GTK_BOX(bar),v->recursive,FALSE,FALSE,0);v->region=gtk_combo_box_text_new();gtk_widget_set_name(v->region,"insight-replace-files-region");const gchar *regions[]={"All text","Code only (skip comments and strings)","Skip comments","Comments only"};for(guint i=0;i<4;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(v->region),insight_tr(regions[i]));gtk_combo_box_set_active(GTK_COMBO_BOX(v->region),0);gtk_box_pack_start(GTK_BOX(bar),v->region,FALSE,FALSE,0);
 bar=row(box);const gchar *labels[]={"Case sensitive","Whole words only","Regular expressions","Dot matches line endings","One occurrence per line","Preserve old case"};for(guint i=0;i<6;i++) { v->checks[i]=gtk_check_button_new_with_label(insight_tr(labels[i]));gchar *name=g_strdup_printf("insight-replace-files-option-%u",i);gtk_widget_set_name(v->checks[i],name);g_free(name);gtk_box_pack_start(GTK_BOX(bar),v->checks[i],FALSE,FALSE,0);g_signal_connect(v->checks[i],"toggled",G_CALLBACK(changed),v); }
 bar=row(box);v->preview=button(bar,"Preview replacements","insight-replace-files-preview",G_CALLBACK(preview),v);v->stop=button(bar,"Stop preview","insight-replace-files-stop",G_CALLBACK(stop),v);v->apply=button(bar,"Apply selected to buffers","insight-replace-files-apply",G_CALLBACK(apply),v);v->confirm=gtk_check_button_new_with_label(insight_tr("Confirm each file before any changes"));gtk_widget_set_name(v->confirm,"insight-replace-files-confirm-option");gtk_box_pack_start(GTK_BOX(bar),v->confirm,FALSE,FALSE,0);button(bar,"Diff / Merge preview","insight-replace-files-diff",G_CALLBACK(compare_preview),v);
 bar=row(box);v->confirm_hits=gtk_check_button_new_with_label(insight_tr("Confirm each replacement before any changes"));gtk_widget_set_name(v->confirm_hits,"insight-replace-files-confirm-hits");gtk_box_pack_start(GTK_BOX(bar),v->confirm_hits,FALSE,FALSE,0);
 v->file_model=gtk_list_store_new(FILE_COLUMNS,G_TYPE_BOOLEAN,G_TYPE_STRING,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_UINT);v->files=gtk_tree_view_new_with_model(GTK_TREE_MODEL(v->file_model));gtk_widget_set_name(v->files,"insight-replace-files-list");GtkCellRenderer *toggle=gtk_cell_renderer_toggle_new();g_signal_connect(toggle,"toggled",G_CALLBACK(file_toggled),v);gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(v->files),-1,insight_tr("Use"),toggle,"active",FILE_USE,NULL);const gchar *titles[]={"File","Replacements","State"};for(guint i=0;i<3;i++) gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(v->files),-1,insight_tr(titles[i]),gtk_cell_renderer_text_new(),"text",i+1,NULL);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->files)),"changed",G_CALLBACK(choose_file),v);GtkWidget *files_scroll=scroll(v->files);gtk_widget_set_size_request(files_scroll,-1,100);gtk_box_pack_start(GTK_BOX(box),files_scroll,TRUE,TRUE,0);
 bar=row(box);GtkWidget *all=button(bar,"Select all files","insight-replace-files-all",G_CALLBACK(select_files),v);g_object_set_data(G_OBJECT(all),"use",GINT_TO_POINTER(1));button(bar,"Deselect files","insight-replace-files-none",G_CALLBACK(select_files),v);all=button(bar,"Select all matches","insight-replace-files-hits-all",G_CALLBACK(select_hits),v);g_object_set_data(G_OBJECT(all),"use",GINT_TO_POINTER(1));button(bar,"Deselect matches","insight-replace-files-hits-none",G_CALLBACK(select_hits),v);
 bar=row(box);button(bar,"Select next 128 files","insight-replace-files-next-batch",G_CALLBACK(next_batch),v);GtkWidget *prev=button(bar,"Previous match","insight-replace-files-previous",G_CALLBACK(navigate_match),v);g_object_set_data(G_OBJECT(prev),"direction",GINT_TO_POINTER(-1));GtkWidget *next=button(bar,"Next match","insight-replace-files-next",G_CALLBACK(navigate_match),v);g_object_set_data(G_OBJECT(next),"direction",GINT_TO_POINTER(1));button(bar,"Open source in editor","insight-replace-files-open",G_CALLBACK(open_source),v);g_signal_connect(v->window,"key-press-event",G_CALLBACK(match_key),v);
 v->hit_model=gtk_list_store_new(HIT_COLUMNS,G_TYPE_BOOLEAN,G_TYPE_UINT,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT);v->hits=gtk_tree_view_new_with_model(GTK_TREE_MODEL(v->hit_model));gtk_widget_set_name(v->hits,"insight-replace-files-hits");toggle=gtk_cell_renderer_toggle_new();g_signal_connect(toggle,"toggled",G_CALLBACK(hit_toggled),v);gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(v->hits),-1,insight_tr("Replace"),toggle,"active",HIT_USE,NULL);const gchar *hit_titles[]={"Line","Column","Old","New"};for(guint i=0;i<4;i++) gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(v->hits),-1,insight_tr(hit_titles[i]),gtk_cell_renderer_text_new(),"text",i+1,NULL);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->hits)),"changed",G_CALLBACK(hit_selected),v);GtkWidget *hit_scroll=scroll(v->hits);gtk_widget_set_size_request(hit_scroll,-1,100);gtk_box_pack_start(GTK_BOX(box),hit_scroll,TRUE,TRUE,0);
 GtkWidget *panes=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);gtk_box_pack_start(GTK_BOX(box),panes,TRUE,TRUE,0);for(guint i=0;i<2;i++) { GtkWidget *col=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);v->labels[i]=gtk_label_new("");gtk_label_set_ellipsize(GTK_LABEL(v->labels[i]),PANGO_ELLIPSIZE_MIDDLE);gtk_box_pack_start(GTK_BOX(col),v->labels[i],FALSE,FALSE,0);v->code[i]=SCINTILLA(scintilla_new());gtk_widget_set_name(GTK_WIDGET(v->code[i]),i ? "insight-replace-files-after":"insight-replace-files-before");SEND(v->code[i],SCI_SETCODEPAGE,SC_CP_UTF8,0);sci_set_symbol_margin(v->code[i],FALSE);SEND(v->code[i],SCI_SETMARGINWIDTHN,2,0);gtk_widget_set_size_request(GTK_WIDGET(v->code[i]),100,150);gtk_box_pack_start(GTK_BOX(col),GTK_WIDGET(v->code[i]),TRUE,TRUE,0);if(i) gtk_paned_pack2(GTK_PANED(panes),col,TRUE,FALSE);else gtk_paned_pack1(GTK_PANED(panes),col,TRUE,FALSE);set_code(v,i,"",NULL); }
 v->status=gtk_label_new(insight_tr("Preview first. Select files and individual replacements, then Apply to buffers. No automatic Save."));gtk_widget_set_name(v->status,"insight-replace-files-status");gtk_label_set_line_wrap(GTK_LABEL(v->status),TRUE);gtk_box_pack_start(GTK_BOX(box),v->status,FALSE,FALSE,0);
 restore_settings(v);GtkWidget *fields[]={v->query,v->replacement,v->masks,v->folder,v->scope,v->region};for(guint i=0;i<6;i++) g_signal_connect(fields[i],"changed",G_CALLBACK(changed),v);g_signal_connect(v->recursive,"toggled",G_CALLBACK(changed),v);g_signal_connect(v->query,"activate",G_CALLBACK(preview),v);g_signal_connect(v->masks,"activate",G_CALLBACK(preview),v);controls(v);gtk_widget_show_all(v->window);
}
static void menu_command(GtkMenuItem *item,gpointer p) { (void)item;(void)p;insight_replace_files_command(); }
void insight_replace_files_init(void) {
 if(initialized) { return; }
 initialized=TRUE;GtkWidget *menu=ui_lookup_widget(main_widgets.window,"search1_menu"),*item=gtk_menu_item_new_with_label(insight_tr("Replace Files…"));gtk_widget_set_name(item,"insight-replace-files-menu");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(menu_command),NULL);keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_REPLACEFILES,item);gtk_widget_show(item);
}
