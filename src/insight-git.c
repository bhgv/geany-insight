/* SPDX-License-Identifier: GPL-2.0-or-later */
#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-git.h"
#include "../insight/desktop/git_project.h"
#include "app.h"
#include "document.h"
#include "project.h"
#include "toolbar.h"
#include "ui_utils.h"
#include "utils.h"
#include <string.h>
#include <glib/gstdio.h>

static gchar *git_root;
static GCallback refresh_project;
static gboolean busy;
typedef enum { JOB_STATUS,JOB_FILES,JOB_ADD,JOB_REMOVE,JOB_COMMIT,JOB_INIT,JOB_CLONE } JobKind;
typedef struct { JobKind kind;gchar *root,*remote,*message;GPtrArray *paths,*rows;GError *error;GtkWidget *dialog;gboolean ok,done; } GitJob;
static void job_worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel)
{
 (void)source;GitJob *j=data;
 if(j->kind==JOB_STATUS) j->rows=insight_git_status(j->root,cancel,&j->error);
 else if(j->kind==JOB_FILES) j->rows=insight_git_files(j->root,cancel,&j->error);
 else if(j->kind==JOB_ADD || j->kind==JOB_REMOVE) j->ok=insight_git_paths(j->root,j->paths,j->kind==JOB_ADD,cancel,&j->error);
 else if(j->kind==JOB_COMMIT) j->ok=insight_git_commit(j->root,j->paths,j->message,&j->error);
 else {
  const gchar *init[]={"init","--initial-branch=main",NULL};
  const gchar *clone[]={"clone","--",j->remote,j->root,NULL};
  GBytes *out=insight_git_run(j->kind==JOB_INIT ? j->root : NULL,NULL,j->kind==JOB_INIT ? init : clone,cancel,&j->error);j->ok=out!=NULL;g_clear_pointer(&out,g_bytes_unref);
 }
 g_task_return_boolean(task,TRUE);
}
static void job_done(GObject *source,GAsyncResult *result,gpointer data)
{ (void)source;(void)result;GitJob *j=data;j->done=TRUE;gtk_dialog_response(GTK_DIALOG(j->dialog),GTK_RESPONSE_OK); }
static gboolean run_job(GitJob *j,GtkWindow *parent)
{
 if(busy) return FALSE;
 busy=TRUE;
 j->dialog=gtk_dialog_new_with_buttons("Git",parent,GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,NULL,NULL);
 gtk_widget_set_name(j->dialog,"insight-git-progress");
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(j->dialog)),*spinner=gtk_spinner_new();gtk_box_pack_start(GTK_BOX(box),spinner,FALSE,FALSE,12);gtk_spinner_start(GTK_SPINNER(spinner));
 gtk_box_pack_start(GTK_BOX(box),gtk_label_new(j->kind==JOB_CLONE ? "Cloning repository…" : "Working with Git…"),FALSE,FALSE,12);
 /* Mutating commands are allowed to finish: killing Git after HEAD changes can
  * make the outcome ambiguous. Network clone can be cancelled. */
 GCancellable *cancel=g_cancellable_new();if(j->kind==JOB_CLONE) gtk_dialog_add_button(GTK_DIALOG(j->dialog),"Cancel",GTK_RESPONSE_CANCEL);
 GTask *task=g_task_new(NULL,cancel,job_done,j);g_task_set_task_data(task,j,NULL);g_task_run_in_thread(task,job_worker);g_object_unref(task);
 gtk_widget_show_all(j->dialog);
 while(!j->done) { gint response=gtk_dialog_run(GTK_DIALOG(j->dialog));if(!j->done && response!=GTK_RESPONSE_OK && j->kind==JOB_CLONE) { g_cancellable_cancel(cancel);gtk_dialog_set_response_sensitive(GTK_DIALOG(j->dialog),GTK_RESPONSE_CANCEL,FALSE); } }
 gtk_widget_destroy(j->dialog);g_object_unref(cancel);busy=FALSE;
 return !j->error && (j->rows || j->ok);
}
static void show_error(GtkWindow *parent,const gchar *message)
{ GtkWidget *d=gtk_message_dialog_new(parent,GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_ERROR,GTK_BUTTONS_CLOSE,"%s",insight_tr(message));gtk_dialog_run(GTK_DIALOG(d));gtk_widget_destroy(d); }
static void refresh(void)
{ if(refresh_project) ((void (*)(GtkWidget *,gpointer))refresh_project)(NULL,NULL); }
gboolean insight_git_active(void) { return git_root!=NULL; }
void insight_git_close(void) { g_clear_pointer(&git_root,g_free); }
void insight_git_bind(GKeyFile *config,const gchar *root)
{ insight_git_close();if(config && g_key_file_get_boolean(config,"project","git_based",NULL)) git_root=g_strdup(root); }
InsightProjectFiles *insight_git_membership(GError **error)
{
 if(!git_root) return NULL;
 GitJob j={0};j.kind=JOB_FILES;j.root=g_strdup(git_root);
 gboolean ok=run_job(&j,GTK_WINDOW(main_widgets.window));InsightProjectFiles *policy=NULL;
 if(ok) {
  policy=insight_project_files_new();policy->automatic=FALSE;
  for(guint i=0;i<j.rows->len;i++) { gchar *native=g_build_filename(j.root,(gchar *)g_ptr_array_index(j.rows,i),NULL),*utf8=utils_get_utf8_from_locale(native);g_ptr_array_add(policy->files,utf8);g_free(native); }
 }
 if(j.error) g_propagate_error(error,j.error);
 g_clear_pointer(&j.rows,g_ptr_array_unref);g_free(j.root);return policy;
}
static gboolean require_project(void)
{ if(git_root) return TRUE;show_error(GTK_WINDOW(main_widgets.window),"Open or create a Git-based project first.");return FALSE; }
static gboolean save_changed(void)
{
 /* Never silently commit an older disk version of an edited project file. */
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!doc->is_valid || !doc->changed || !doc->file_name) continue;
  gchar *native=utils_get_locale_from_utf8(doc->file_name);GFile *base=g_file_new_for_path(git_root),*file=g_file_new_for_path(native);gboolean inside=g_file_has_prefix(file,base);g_object_unref(base);g_object_unref(file);g_free(native);
  if(inside) {
   GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"Save modified project documents before reviewing the commit?");
   gtk_dialog_add_buttons(GTK_DIALOG(d),"Cancel",GTK_RESPONSE_CANCEL,"Save project documents",GTK_RESPONSE_ACCEPT,NULL);gint answer=gtk_dialog_run(GTK_DIALOG(d));gtk_widget_destroy(d);if(answer!=GTK_RESPONSE_ACCEPT) return FALSE;
   for(guint k=i;k<documents_array->len;k++) { GeanyDocument *p=g_ptr_array_index(documents_array,k);if(!p->is_valid || !p->changed || !p->file_name) continue;gchar *name=utils_get_locale_from_utf8(p->file_name);GFile *b=g_file_new_for_path(git_root),*f=g_file_new_for_path(name);gboolean member=g_file_has_prefix(f,b);g_object_unref(b);g_object_unref(f);g_free(name);if(member && !document_save_file(p,FALSE)) return FALSE; }
   break;
  }
 }
 return TRUE;
}
static const gchar *status_label(InsightGitChange *c)
{ if(c->x=='U' || c->y=='U' || (c->x=='A' && c->y=='A') || (c->x=='D' && c->y=='D')) return "Conflict";if(c->x=='?' || c->x=='A') return "Added";if(c->x=='D' || c->y=='D') return "Removed";return "Modified"; }
typedef struct { GtkListStore *store;GtkWidget *message,*dialog,*count;GPtrArray *rows; } CommitDialog;
static GPtrArray *selected_rows(CommitDialog *d)
{
 GPtrArray *rows=g_ptr_array_new();GtkTreeIter iter;gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(d->store),&iter);
 while(valid) { gboolean checked;guint number;gtk_tree_model_get(GTK_TREE_MODEL(d->store),&iter,0,&checked,3,&number,-1);if(checked) g_ptr_array_add(rows,g_ptr_array_index(d->rows,number));valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(d->store),&iter); }return rows;
}
static void selection_changed(CommitDialog *d)
{
 GPtrArray *rows=selected_rows(d);gchar *text=g_strdup_printf("%u of %u files selected. Commit uses the listed disk changes.",rows->len,d->rows->len);gtk_label_set_text(GTK_LABEL(d->count),text);g_free(text);
 gtk_dialog_set_response_sensitive(GTK_DIALOG(d->dialog),GTK_RESPONSE_ACCEPT,rows->len>0);g_ptr_array_unref(rows);
}
static void toggle_file(GtkCellRendererToggle *renderer,gchar *path,gpointer data)
{ (void)renderer;CommitDialog *d=data;GtkTreeIter iter;if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(d->store),&iter,path)) { gboolean value;gtk_tree_model_get(GTK_TREE_MODEL(d->store),&iter,0,&value,-1);gtk_list_store_set(d->store,&iter,0,!value,-1);selection_changed(d); } }
static void suggest_message(GtkWidget *widget,gpointer data)
{ (void)widget;CommitDialog *d=data;GPtrArray *rows=selected_rows(d);gchar *message=insight_git_message(rows);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->message)),message,-1);g_free(message);g_ptr_array_unref(rows); }
static void commit_command(GtkWidget *widget,gpointer data)
{
 (void)widget;(void)data;if(!require_project() || !save_changed()) return;
 GitJob status={0};status.kind=JOB_STATUS;status.root=g_strdup(git_root);
 if(!run_job(&status,GTK_WINDOW(main_widgets.window))) { show_error(GTK_WINDOW(main_widgets.window),status.error ? status.error->message : "Git is busy");g_clear_error(&status.error);g_free(status.root);return; }
 if(!status.rows->len) { ui_set_statusbar(TRUE,"Git: no changes to commit");g_ptr_array_unref(status.rows);g_free(status.root);return; }
 CommitDialog d={0};d.rows=status.rows;d.dialog=gtk_dialog_new_with_buttons("Commit project changes",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Commit",GTK_RESPONSE_ACCEPT,NULL);
 gtk_widget_set_name(d.dialog,"insight-git-commit-dialog");gtk_window_set_default_size(GTK_WINDOW(d.dialog),1000,560);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d.dialog)),*paned=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
 d.count=gtk_label_new(NULL);gtk_box_pack_start(GTK_BOX(box),d.count,FALSE,FALSE,6);gtk_box_pack_start(GTK_BOX(box),paned,TRUE,TRUE,6);
 d.store=gtk_list_store_new(4,G_TYPE_BOOLEAN,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT);GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(d.store));gtk_widget_set_name(view,"insight-git-commit-files");
 GtkCellRenderer *toggle=gtk_cell_renderer_toggle_new();g_signal_connect(toggle,"toggled",G_CALLBACK(toggle_file),&d);
 gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Commit",toggle,"active",0,NULL));
 gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Status",gtk_cell_renderer_text_new(),"text",1,NULL));
 gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("File",gtk_cell_renderer_text_new(),"text",2,NULL));
 for(guint i=0;i<d.rows->len;i++) { InsightGitChange *c=g_ptr_array_index(d.rows,i);GtkTreeIter iter;gchar *name=g_filename_display_name(c->path);gtk_list_store_append(d.store,&iter);gtk_list_store_set(d.store,&iter,0,TRUE,1,status_label(c),2,name,3,i,-1);g_free(name); }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_paned_pack1(GTK_PANED(paned),scroll,TRUE,FALSE);gtk_paned_set_position(GTK_PANED(paned),500);
 GtkWidget *right=gtk_box_new(GTK_ORIENTATION_VERTICAL,6),*suggest=gtk_button_new_with_label("Suggest message from selected files");gtk_box_pack_start(GTK_BOX(right),gtk_label_new("Commit message (editable)"),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(right),suggest,FALSE,FALSE,0);g_signal_connect(suggest,"clicked",G_CALLBACK(suggest_message),&d);
 d.message=gtk_text_view_new();gtk_widget_set_name(d.message,"insight-git-commit-message");gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(d.message),GTK_WRAP_WORD_CHAR);scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),d.message);gtk_box_pack_start(GTK_BOX(right),scroll,TRUE,TRUE,0);gtk_paned_pack2(GTK_PANED(paned),right,TRUE,FALSE);
 selection_changed(&d);suggest_message(NULL,&d);gtk_widget_show_all(d.dialog);
 while(gtk_dialog_run(GTK_DIALOG(d.dialog))==GTK_RESPONSE_ACCEPT) {
  GPtrArray *selected=selected_rows(&d);GtkTextIter first,last;GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(d.message));gtk_text_buffer_get_bounds(buffer,&first,&last);gchar *message=gtk_text_buffer_get_text(buffer,&first,&last,FALSE);g_strstrip(message);
  if(!*message || !selected->len) { show_error(GTK_WINDOW(d.dialog),"Select at least one file and enter a commit message.");g_free(message);g_ptr_array_unref(selected);continue; }
  /* Refuse a stale review, including changes made by external Git clients. */
  GitJob check={0};check.kind=JOB_STATUS;check.root=status.root;gboolean same=run_job(&check,GTK_WINDOW(d.dialog)) && check.rows->len==status.rows->len;
 for(guint i=0;same && i<check.rows->len;i++) { InsightGitChange *a=g_ptr_array_index(check.rows,i),*b=g_ptr_array_index(status.rows,i);same=a->x==b->x && a->y==b->y && !strcmp(a->path,b->path) && !g_strcmp0(a->snapshot,b->snapshot); }
  g_clear_pointer(&check.rows,g_ptr_array_unref);g_clear_error(&check.error);
  if(!same) { show_error(GTK_WINDOW(d.dialog),"Git status changed. Reopen the commit dialog to review the current files.");g_free(message);g_ptr_array_unref(selected);break; }
  GitJob commit={0};commit.kind=JOB_COMMIT;commit.root=status.root;commit.paths=selected;commit.message=message;gboolean ok=run_job(&commit,GTK_WINDOW(d.dialog));g_free(message);g_ptr_array_unref(selected);
  if(ok) { ui_set_statusbar(TRUE,"Git: selected changes committed");break; }show_error(GTK_WINDOW(d.dialog),commit.error ? commit.error->message : "Git is busy");g_clear_error(&commit.error);
 }
 gtk_widget_destroy(d.dialog);g_object_unref(d.store);g_ptr_array_unref(status.rows);g_free(status.root);refresh();
}
static gboolean paths_dialog(GtkWindow *parent,gboolean add,gboolean tree)
{
 GtkWidget *chooser=gtk_file_chooser_dialog_new(add ? "Add files to Git" : "Remove files from Git (keep on disk)",parent,tree ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,add ? "Add to Git" : "Remove from Git",GTK_RESPONSE_ACCEPT,NULL);
 gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser),TRUE);gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser),git_root);gboolean ok=FALSE;
 if(gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT) {
  GSList *files=gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(chooser));GitJob j={0};j.kind=add ? JOB_ADD : JOB_REMOVE;j.root=g_strdup(git_root);j.paths=g_ptr_array_new_with_free_func(g_free);GFile *base=g_file_new_for_path(git_root);gboolean valid=TRUE;
  for(GSList *p=files;p;p=p->next) { GFile *file=g_file_new_for_path(p->data);gchar *relative=g_file_get_relative_path(base,file);g_object_unref(file);if(!relative) valid=FALSE;else g_ptr_array_add(j.paths,relative); }
  if(!valid) show_error(parent,"All selected files must be inside the Git project.");
  else { ok=run_job(&j,parent);if(!ok) show_error(parent,j.error ? j.error->message : "Git is busy"); }
  g_object_unref(base);g_slist_free_full(files,g_free);g_ptr_array_unref(j.paths);g_free(j.root);g_clear_error(&j.error);
 }
 gtk_widget_destroy(chooser);return ok;
}
static void add_command(GtkWidget *widget,gpointer data)
{ (void)widget;(void)data;if(require_project() && paths_dialog(GTK_WINDOW(main_widgets.window),TRUE,FALSE)) refresh(); }
void insight_git_manage(GtkWidget *widget,gpointer data)
{
 (void)widget;(void)data;if(!require_project()) return;
 GtkWidget *d=gtk_dialog_new_with_buttons("Add and Remove Git Project Files",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Close",GTK_RESPONSE_CLOSE,"Add files…",1,"Add folder…",3,"Remove selected",2,NULL);gtk_window_set_default_size(GTK_WINDOW(d),780,500);gtk_widget_set_name(d,"insight-git-membership");
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d));gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Git is the project file store. Removal stages a deletion; files stay on disk."),FALSE,FALSE,6);
 GtkListStore *store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING);GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));gtk_widget_set_name(view,"insight-git-membership-files");gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Tracked file",gtk_cell_renderer_text_new(),"text",0,NULL));gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),GTK_SELECTION_MULTIPLE);gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),0,GTK_SORT_ASCENDING);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 gboolean changed=FALSE;
 for(;;) {
  GitJob list={0};list.kind=JOB_FILES;list.root=g_strdup(git_root);if(!run_job(&list,GTK_WINDOW(d))) { show_error(GTK_WINDOW(d),list.error ? list.error->message : "Git is busy");g_clear_error(&list.error);g_free(list.root);break; }
  gtk_list_store_clear(store);for(guint i=0;i<list.rows->len;i++) { const gchar *p=g_ptr_array_index(list.rows,i);gchar *name=g_filename_display_name(p);GtkTreeIter iter;gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,name,1,p,-1);g_free(name); }g_ptr_array_unref(list.rows);g_free(list.root);gtk_widget_show_all(d);
  gint response=gtk_dialog_run(GTK_DIALOG(d));if(response==1 || response==3) changed=paths_dialog(GTK_WINDOW(d),TRUE,response==3)||changed;
  else if(response==2) {
   GList *rows=gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL);GitJob j={0};j.kind=JOB_REMOVE;j.root=g_strdup(git_root);j.paths=g_ptr_array_new_with_free_func(g_free);
   for(GList *p=rows;p;p=p->next) { GtkTreeIter iter;if(gtk_tree_model_get_iter(GTK_TREE_MODEL(store),&iter,p->data)) { gchar *name;gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,1,&name,-1);g_ptr_array_add(j.paths,name); } }g_list_free_full(rows,(GDestroyNotify)gtk_tree_path_free);
   if(j.paths->len) { if(run_job(&j,GTK_WINDOW(d))) changed=TRUE;else show_error(GTK_WINDOW(d),j.error ? j.error->message : "Git is busy"); }g_clear_error(&j.error);g_free(j.root);g_ptr_array_unref(j.paths);
  } else break;
 }
 gtk_widget_destroy(d);g_object_unref(store);if(changed) refresh();
}
static void create_project(GtkWidget *widget,gpointer data)
{
 (void)widget;gboolean clone=GPOINTER_TO_INT(data);
 GtkWidget *d=gtk_dialog_new_with_buttons(clone ? "Create Project from Git" : "Create Git Project",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Create",GTK_RESPONSE_ACCEPT,NULL);gtk_widget_set_name(d,clone ? "insight-git-clone" : "insight-git-create");
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d)),*name=gtk_entry_new(),*path=gtk_entry_new(),*remote=gtk_entry_new();gtk_widget_set_name(name,"insight-git-project-name");gtk_widget_set_name(path,"insight-git-project-path");gtk_widget_set_name(remote,"insight-git-project-remote");gtk_entry_set_placeholder_text(GTK_ENTRY(name),"Project name");gtk_entry_set_placeholder_text(GTK_ENTRY(path),clone ? "New destination folder (absolute path)" : "Repository folder (absolute path)");gtk_entry_set_placeholder_text(GTK_ENTRY(remote),"Git URL or local repository path");gtk_entry_set_width_chars(GTK_ENTRY(path),64);
 gtk_box_pack_start(GTK_BOX(box),name,FALSE,FALSE,6);gtk_box_pack_start(GTK_BOX(box),path,FALSE,FALSE,6);if(clone) gtk_box_pack_start(GTK_BOX(box),remote,FALSE,FALSE,6);else g_object_ref_sink(remote);
 gtk_box_pack_start(GTK_BOX(box),gtk_label_new(clone ? "Clone uses your installed Git and its credential helpers." : "An existing repository is reused; an ordinary folder is initialized."),FALSE,FALSE,6);gtk_widget_show_all(d);
 while(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT) {
  const gchar *project_name=gtk_entry_get_text(GTK_ENTRY(name)),*directory=gtk_entry_get_text(GTK_ENTRY(path)),*url=gtk_entry_get_text(GTK_ENTRY(remote));
  if(!*project_name || !g_path_is_absolute(directory) || (clone && !*url)) { show_error(GTK_WINDOW(d),"Enter a project name, an absolute destination path and (for cloning) a repository address.");continue; }
  gchar *native=utils_get_locale_from_utf8(directory),*file=g_build_filename(native,"insight.geany",NULL);
  if(g_file_test(file,G_FILE_TEST_EXISTS)) { show_error(GTK_WINDOW(d),"insight.geany already exists in this folder. Open that project instead.");g_free(native);g_free(file);continue; }
  if(!clone && g_mkdir_with_parents(native,0700)) { show_error(GTK_WINDOW(d),"Cannot create the repository folder.");g_free(native);g_free(file);continue; }
  GitJob j={0};j.kind=clone ? JOB_CLONE : JOB_INIT;j.root=native;j.remote=(gchar *)url;
  gboolean ok=FALSE;
  if(!clone) { const gchar *probe[]={"rev-parse","--show-toplevel",NULL};GBytes *out=insight_git_run(native,NULL,probe,NULL,NULL);if(out) { gsize n;const gchar *p=g_bytes_get_data(out,&n);gchar *top=g_strndup(p,n);g_strchomp(top);gchar *canonical=g_canonicalize_filename(native,NULL);ok=!g_strcmp0(top,canonical);g_free(top);g_free(canonical);g_bytes_unref(out);if(!ok) { show_error(GTK_WINDOW(d),"Choose the root of the existing repository, not a subfolder.");g_free(native);g_free(file);continue; } } }
  if(!ok) ok=run_job(&j,GTK_WINDOW(d));
  if(ok) {
   GKeyFile *config=g_key_file_new();g_key_file_set_string(config,"project","name",project_name);g_key_file_set_string(config,"project","base_path","./");g_key_file_set_boolean(config,"project","git_based",TRUE);gsize length;gchar *text=g_key_file_to_data(config,&length,NULL);
   /* Never replace a project file supplied by the cloned repository. */
   if(g_file_test(file,G_FILE_TEST_EXISTS)) ok=TRUE;
   else ok=g_file_set_contents(file,text,length,&j.error);
   g_free(text);g_key_file_unref(config);
   if(ok) {
    /* A cloned project can opt into Git membership without losing its settings. */
    config=g_key_file_new();ok=g_key_file_load_from_file(config,file,G_KEY_FILE_KEEP_COMMENTS,&j.error);if(ok) { g_key_file_set_boolean(config,"project","git_based",TRUE);g_key_file_set_string(config,"project","base_path","./");g_key_file_set_string(config,"project","name",project_name);text=g_key_file_to_data(config,&length,NULL);ok=g_file_set_contents(file,text,length,&j.error);g_free(text); }g_key_file_unref(config);
   }
   if(ok && app->project && !project_close(FALSE)) { g_free(native);g_free(file);break; }
   if(ok) ok=project_load_file_with_session(file);
  }
  if(!ok) show_error(GTK_WINDOW(d),j.error ? j.error->message : "Cannot open the Git project.");
  g_clear_error(&j.error);g_free(native);g_free(file);if(ok) break;
 }
 if(!clone) g_object_unref(remote);
 gtk_widget_destroy(d);
}
static GtkWidget *git_icon(gboolean add)
{
 const gchar *file="<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24'><path d='M4 2h10l5 5v14H4z' fill='#f3f6fa' stroke='#486581' stroke-width='1.7'/><path d='M14 2v6h5' fill='none' stroke='#486581' stroke-width='1.7'/><circle cx='17' cy='17' r='6' fill='#238636'/><path d='M17 13v8m-4-4h8' stroke='white' stroke-width='2'/></svg>";
 const gchar *branch="<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24'><path d='M6 5v14m0-7c9 0 11-1 11-7' fill='none' stroke='#486581' stroke-width='2.4'/><g fill='#f3f6fa' stroke='#486581' stroke-width='1.8'><circle cx='6' cy='4' r='2.5'/><circle cx='6' cy='20' r='2.5'/><circle cx='17' cy='4' r='2.5'/></g><circle cx='17' cy='17' r='6' fill='#238636'/><path d='m13.5 17 2.2 2.2 4.2-4.5' fill='none' stroke='white' stroke-width='2'/></svg>";
 const gchar *svg=add ? file : branch;GdkPixbufLoader *loader=gdk_pixbuf_loader_new_with_type("svg",NULL);GtkWidget *icon=NULL;
 if(loader) { if(gdk_pixbuf_loader_write(loader,(const guchar *)svg,strlen(svg),NULL) && gdk_pixbuf_loader_close(loader,NULL)) icon=gtk_image_new_from_pixbuf(gdk_pixbuf_loader_get_pixbuf(loader));g_object_unref(loader); }
 return icon ? icon : gtk_image_new_from_icon_name(add ? "list-add" : "emblem-default",GTK_ICON_SIZE_SMALL_TOOLBAR);
}
void insight_git_install(GCallback callback)
{
 refresh_project=callback;GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"menu_project1")));
 const gchar *labels[]={"Create Git Project…","Create Project from Git…"};
 for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,i ? "insight-create-from-git" : "insight-create-git-project");gtk_menu_shell_insert(GTK_MENU_SHELL(menu),item,2+i);g_signal_connect(item,"activate",G_CALLBACK(create_project),GUINT_TO_POINTER(i));gtk_widget_show(item); }
 menu=ui_lookup_widget(main_widgets.window,"file1_menu");
 if(!menu) menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"file1")));
 GList *children=gtk_container_get_children(GTK_CONTAINER(menu));gint position=5,index=0;for(GList *p=children;p;p=p->next,index++) if(p->data==ui_lookup_widget(main_widgets.window,"menu_save_as1")) position=index+1;g_list_free(children);
 for(guint i=0;i<2;i++) { GtkWidget *item=gtk_image_menu_item_new_with_label(i ? "Add to Git…" : "Commit changes…");gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),git_icon(i));gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE);gtk_widget_set_name(item,i ? "insight-git-add-menu" : "insight-git-commit-menu");gtk_menu_shell_insert(GTK_MENU_SHELL(menu),item,position+i);g_signal_connect(item,"activate",i ? G_CALLBACK(add_command) : G_CALLBACK(commit_command),NULL);gtk_widget_show_all(item); }
}
void insight_git_toolbar(void)
{
 GtkToolbar *toolbar=GTK_TOOLBAR(main_widgets.toolbar);GtkWidget *save=toolbar_get_widget_by_name("SaveAs");if(!save) save=toolbar_get_widget_by_name("Save");gint position=save && GTK_IS_TOOL_ITEM(save) ? gtk_toolbar_get_item_index(toolbar,GTK_TOOL_ITEM(save))+1 : -1;
 for(guint i=0;i<2;i++) { GtkToolItem *item=gtk_tool_button_new(git_icon(i),i ? "Add to Git" : "Commit changes");toolbar_item_ref(item);gtk_widget_set_name(GTK_WIDGET(item),i ? "insight-git-add-tool" : "insight-git-commit-tool");gtk_widget_set_tooltip_text(GTK_WIDGET(item),i ? "Add project files to Git" : "Review and commit selected project changes");g_signal_connect(item,"clicked",i ? G_CALLBACK(add_command) : G_CALLBACK(commit_command),NULL);gtk_toolbar_insert(toolbar,item,position<0 ? -1 : position+(gint)i);gtk_widget_show_all(GTK_WIDGET(item)); }
}
