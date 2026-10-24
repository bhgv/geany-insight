/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-compare-ui.h"
#include "geany.h"
#include "insight-git-history.h"
#include "highlighting.h"
#include "filetypes.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include "../insight/desktop/git_history.h"
#include "../insight/core/diff.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define STATE "insight-git-history-state"
typedef enum { REFS,LOG,REVISION,FILES,TEXT } Kind;
typedef struct {
    Kind kind;guint64 serial;guint offset;gboolean more;gchar *root,*oid,*parent,*blob[2];guint modes[2];
    GPtrArray *refs,*commits,*files;InsightGitRevision *revision;gchar *text[2];InsightDiff diff;
} Job;
typedef struct {
    GtkWidget *window,*branch,*commits,*files,*parents,*message,*status,*labels[2],*root_label,*hunks,*page_previous,*page_next;
    GtkListStore *commit_model,*file_model,*hunk_model;ScintillaObject *code[2];
    GPtrArray *refs,*revisions,*deltas;InsightGitRevision *revision;InsightDiff diff;
    gchar *root,*ref_name,*tip;GCancellable *cancel;guint64 serial;guint offset;gboolean more,changing,closed,busy;
} History;
static void launch(History *,Job *);
static void load_log(History *);
static void choose_file(GtkTreeSelection *,gpointer);
static void job_free(gpointer data)
{
    Job *job=data;g_free(job->root);g_free(job->oid);g_free(job->parent);g_clear_pointer(&job->refs,g_ptr_array_unref);g_clear_pointer(&job->commits,g_ptr_array_unref);g_clear_pointer(&job->files,g_ptr_array_unref);insight_git_revision_free(job->revision);
    for(guint i=0;i<2;i++) { g_free(job->blob[i]);g_free(job->text[i]); }insight_diff_clear(&job->diff);g_free(job);
}
static void history_free(gpointer data)
{
    History *view=data;g_clear_object(&view->cancel);g_clear_pointer(&view->refs,g_ptr_array_unref);g_clear_pointer(&view->revisions,g_ptr_array_unref);g_clear_pointer(&view->deltas,g_ptr_array_unref);insight_git_revision_free(view->revision);insight_diff_clear(&view->diff);
    g_object_unref(view->commit_model);g_object_unref(view->file_model);g_object_unref(view->hunk_model);g_free(view->root);g_free(view->ref_name);g_free(view->tip);g_free(view);
}
static void destroyed(GtkWidget *widget,gpointer data)
{ (void)widget;History *view=data;view->closed=TRUE;if(view->cancel) g_cancellable_cancel(view->cancel); }
static void busy(History *view,gboolean value)
{
    view->busy=value;g_object_set_data(G_OBJECT(view->window),"insight-git-history-busy",GINT_TO_POINTER(value));
    gtk_widget_set_sensitive(view->page_previous,!value && view->offset>0);gtk_widget_set_sensitive(view->page_next,!value && view->more && view->offset<100000);
}
static void set_code(History *view,guint side,const gchar *text,const gchar *path)
{
    ScintillaObject *sci=view->code[side];GeanyFiletype *type=filetypes_detect_from_extension(path ? path:"");highlighting_set_styles(sci,type ? type:filetypes[GEANY_FILETYPES_NONE]);
    /* Match the comparison window's code baseline, independently of the
     * editor's source-font preferences. Retain lexer colors and emphasis. */
    SEND(sci,SCI_SETZOOM,0,0);for(guint style=0;style<=STYLE_MAX;style++) { SEND(sci,SCI_STYLESETSIZEFRACTIONAL,style,100*INSIGHT_COMPARE_CODE_POINTS);SEND(sci,SCI_STYLESETFONT,style,(sptr_t)"Monospace"); }
    SEND(sci,SCI_SETREADONLY,FALSE,0);SEND(sci,SCI_SETUNDOCOLLECTION,FALSE,0);sci_set_text(sci,text ? text:"");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);SEND(sci,SCI_SETREADONLY,TRUE,0);
    sci_set_line_numbers(sci,TRUE);SEND(sci,SCI_SETMARGINWIDTHN,0,0);SEND(sci,SCI_SETMARGINWIDTHN,1,MAX(40,SEND(sci,SCI_GETMARGINWIDTHN,1,0)));SEND(sci,SCI_MARKERDEFINE,10,SC_MARK_BACKGROUND);SEND(sci,SCI_MARKERSETBACK,10,side ? 0xDDF3D5:0xD5DFFF);SEND(sci,SCI_MARKERDELETEALL,10,0);SEND(sci,SCI_COLOURISE,0,-1);
}
static void clear_text(History *view)
{
    gtk_list_store_clear(view->hunk_model);insight_diff_clear(&view->diff);
    for(guint i=0;i<2;i++) { set_code(view,i,"",NULL);gtk_label_set_text(GTK_LABEL(view->labels[i]),insight_tr(i ? "Selected commit":"Parent version")); }
}
static void clear_files(History *view)
{
    view->changing=TRUE;gtk_list_store_clear(view->file_model);g_clear_pointer(&view->deltas,g_ptr_array_unref);clear_text(view);view->changing=FALSE;
}
static void clear_revision(History *view)
{
    clear_files(view);view->changing=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(view->parents));insight_git_revision_free(view->revision);view->revision=NULL;gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->message)),"",0);view->changing=FALSE;
}
static gboolean selected(GtkWidget *tree,guint *index)
{
    GtkTreeIter iter;GtkTreeModel *model=NULL;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),&model,&iter)) return FALSE;gtk_tree_model_get(model,&iter,0,index,-1);return TRUE;
}
static void first(GtkWidget *tree)
{ GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path); }
static void select_hunk(GtkTreeSelection *selection,gpointer data)
{
    (void)selection;History *view=data;guint index;if(view->closed || view->changing || !selected(view->hunks,&index) || index>=view->diff.count) return;
    InsightDiffHunk *h=&view->diff.hunks[index];
    for(guint side=0;side<2;side++) {
        gsize begin=side ? h->b_begin:h->a_begin,end=side ? h->b_end:h->a_end;SEND(view->code[side],SCI_SETSEL,begin,end);SEND(view->code[side],SCI_SCROLLCARET,0,0);
    }
}
static void fill_files(History *view,GPtrArray *files)
{
    clear_files(view);view->deltas=files;view->changing=TRUE;
    for(guint i=0;i<files->len;i++) { InsightGitDelta *delta=g_ptr_array_index(files,i);GtkTreeIter iter;gchar status[2]={delta->status,0};gchar *path=g_utf8_make_valid(delta->path,-1);gtk_list_store_append(view->file_model,&iter);gtk_list_store_set(view->file_model,&iter,0,i,1,status,2,path,-1);g_free(path); }
    view->changing=FALSE;gtk_label_set_text(GTK_LABEL(view->status),insight_tr(files->len ? "Select a changed file to inspect its historical versions.":"No changed files against the selected parent."));if(files->len) first(view->files);
}
static int cancelled(void *data) { return g_cancellable_is_cancelled(data); }
static void worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel)
{
    (void)source;Job *job=data;GError *error=NULL;gboolean ok=FALSE;
    if(job->kind==REFS) {
        gchar *root=insight_git_history_root(job->root,cancel,&error);
        if(root) { g_free(job->root);job->root=root;job->refs=insight_git_history_refs(root,cancel,&error);ok=job->refs!=NULL; }
    } else if(job->kind==LOG) { job->commits=insight_git_history_log(job->root,job->oid,job->offset,&job->more,cancel,&error);ok=job->commits!=NULL; }
    else if(job->kind==REVISION) {
        job->revision=insight_git_history_revision(job->root,job->oid,cancel,&error);
        if(job->revision) { const gchar *parent=job->revision->parents->len ? g_ptr_array_index(job->revision->parents,0):NULL;job->files=insight_git_history_delta(job->root,job->oid,parent,cancel,&error);ok=job->files!=NULL; }
    } else if(job->kind==FILES) { job->files=insight_git_history_delta(job->root,job->oid,job->parent,cancel,&error);ok=job->files!=NULL; }
    else {
        for(guint i=0;i<2;i++) { job->text[i]=insight_git_history_text(job->root,job->blob[i],job->modes[i],NULL,cancel,&error);if(!job->text[i]) break; }
        if(job->text[0] && job->text[1]) {
            int rc=insight_diff_compute_cancellable(job->text[0],job->text[1],0,cancelled,cancel,&job->diff);
            if(!rc) ok=TRUE;else g_set_error_literal(&error,G_IO_ERROR,rc==-3 ? G_IO_ERROR_CANCELLED:G_IO_ERROR_FAILED,rc==-3 ? "Git diff cancelled.":rc==-2 ? "Git diff exceeds the 4 million LCS cell limit.":"Cannot allocate Git diff.");
        }
    }
    if(ok) g_task_return_boolean(task,TRUE);else { if(!error) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot load Git history.");g_task_return_error(task,error); }
}
static void completed(GObject *object,GAsyncResult *result,gpointer data)
{
    (void)data;History *view=g_object_get_data(object,STATE);Job *job=g_task_get_task_data(G_TASK(result));GError *error=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);
    if(view->closed || job->serial!=view->serial) { g_clear_error(&error);return; }
    g_clear_object(&view->cancel);busy(view,FALSE);
    if(!ok) { gtk_label_set_text(GTK_LABEL(view->status),insight_tr(error->message));g_clear_error(&error);return; }
    if(job->kind==REFS) {
        g_free(view->root);view->root=g_strdup(job->root);gchar *root=g_filename_display_name(view->root);gtk_label_set_text(GTK_LABEL(view->root_label),root);g_free(root);
        g_clear_pointer(&view->refs,g_ptr_array_unref);view->refs=job->refs;job->refs=NULL;view->changing=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(view->branch));gint active=0;
        for(guint i=0;i<view->refs->len;i++) { InsightGitRef *ref=g_ptr_array_index(view->refs,i);gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view->branch),ref->name);if(!g_strcmp0(ref->name,view->ref_name)) active=i; }
        gtk_combo_box_set_active(GTK_COMBO_BOX(view->branch),view->refs->len ? active:-1);view->changing=FALSE;
        if(view->refs->len) { InsightGitRef *ref=g_ptr_array_index(view->refs,active);g_free(view->tip);view->tip=g_strdup(ref->oid);g_free(view->ref_name);view->ref_name=g_strdup(ref->name);view->offset=0;load_log(view); }
        else gtk_label_set_text(GTK_LABEL(view->status),insight_tr("Repository has no commits or branch tips yet."));
    } else if(job->kind==LOG) {
        g_clear_pointer(&view->revisions,g_ptr_array_unref);view->revisions=job->commits;job->commits=NULL;view->more=job->more;view->changing=TRUE;
        for(guint i=0;i<view->revisions->len;i++) { InsightGitRevision *row=g_ptr_array_index(view->revisions,i);GtkTreeIter iter;gchar *short_id=g_strndup(row->oid,12);gtk_list_store_append(view->commit_model,&iter);gtk_list_store_set(view->commit_model,&iter,0,i,1,short_id,2,row->subject,3,row->author,4,row->date,-1);g_free(short_id); }
        view->changing=FALSE;busy(view,FALSE);if(view->revisions->len) first(view->commits);else gtk_label_set_text(GTK_LABEL(view->status),insight_tr("No commits on this history page."));
    } else if(job->kind==REVISION) {
        view->revision=job->revision;job->revision=NULL;InsightGitRevision *revision=view->revision;
        gchar *message=g_strdup_printf("commit %s\nAuthor: %s\nDate: %s\n\n%s",revision->oid,revision->author,revision->date,revision->message);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->message)),message,-1);g_free(message);
        view->changing=TRUE;
        if(!revision->parents->len) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view->parents),insight_tr("Root commit — empty parent"));
        for(guint i=0;i<revision->parents->len;i++) { gchar *label=insight_i18n_printf("Parent %u: %.12s",i+1,(gchar *)g_ptr_array_index(revision->parents,i));gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view->parents),label);g_free(label); }
        gtk_combo_box_set_active(GTK_COMBO_BOX(view->parents),0);view->changing=FALSE;GPtrArray *files=job->files;job->files=NULL;fill_files(view,files);
    } else if(job->kind==FILES) { GPtrArray *files=job->files;job->files=NULL;fill_files(view,files); }
    else {
        guint index;if(!selected(view->files,&index) || !view->deltas || index>=view->deltas->len) return;InsightGitDelta *delta=g_ptr_array_index(view->deltas,index);gchar *path=g_utf8_make_valid(delta->path,-1);
        for(guint side=0;side<2;side++) { set_code(view,side,job->text[side],path);gchar *label=g_strdup_printf("%s — %s",insight_tr(side ? "Selected commit":"Parent version"),path);insight_i18n_text(G_OBJECT(view->labels[side]),"label",side ? "Selected commit — {value}":"Parent version — {value}",path);insight_i18n_text(G_OBJECT(view->labels[side]),"tooltip-text",side ? "Selected commit — {value}":"Parent version — {value}",path);GtkTreeViewColumn *column=gtk_tree_view_get_column(GTK_TREE_VIEW(view->hunks),side);if(column) gtk_tree_view_column_set_title(column,label);g_free(label); }g_free(path);
        view->diff=job->diff;job->diff=(InsightDiff){0};view->changing=TRUE;
        for(guint i=0;i<view->diff.count;i++) {
            InsightDiffHunk *h=&view->diff.hunks[i];gint lines[2];
            for(guint side=0;side<2;side++) { gsize begin=side ? h->b_begin:h->a_begin,end=side ? h->b_end:h->a_end;gint first_line=SEND(view->code[side],SCI_LINEFROMPOSITION,begin,0),last_line=SEND(view->code[side],SCI_LINEFROMPOSITION,end>begin ? end-1:begin,0);lines[side]=first_line+1;for(gint line=first_line;line<=last_line;line++) SEND(view->code[side],SCI_MARKERADD,line,10); }
            GtkTreeIter iter;gtk_list_store_append(view->hunk_model,&iter);gtk_list_store_set(view->hunk_model,&iter,0,i,1,lines[0],2,lines[1],-1);
        }
        view->changing=FALSE;gtk_label_set_text(GTK_LABEL(view->status),insight_tr(view->diff.count ? "Read-only Git diff. Branch selection does not check out files.":"No text differences; file mode or metadata may have changed."));if(view->diff.count) first(view->hunks);
    }
}
static void launch(History *view,Job *job)
{
    if(view->cancel) g_cancellable_cancel(view->cancel);g_clear_object(&view->cancel);view->serial++;job->serial=view->serial;if(!job->root) job->root=g_strdup(view->root);view->cancel=g_cancellable_new();busy(view,TRUE);gtk_label_set_text(GTK_LABEL(view->status),insight_tr("Loading Git history…"));
    GTask *task=g_task_new(view->window,view->cancel,completed,NULL);g_task_set_task_data(task,job,job_free);g_task_run_in_thread(task,worker);g_object_unref(task);
}
static void load_log(History *view)
{
    view->changing=TRUE;gtk_list_store_clear(view->commit_model);clear_revision(view);view->changing=FALSE;Job *job=g_new0(Job,1);job->kind=LOG;job->oid=g_strdup(view->tip);job->offset=view->offset;launch(view,job);
}
static void branch_changed(GtkComboBox *combo,gpointer data)
{
    History *view=data;if(view->changing || view->closed) return;gint index=gtk_combo_box_get_active(combo);if(index<0 || !view->refs || (guint)index>=view->refs->len) return;
    InsightGitRef *ref=g_ptr_array_index(view->refs,index);g_free(view->tip);view->tip=g_strdup(ref->oid);g_free(view->ref_name);view->ref_name=g_strdup(ref->name);view->offset=0;load_log(view);
}
static void choose_commit(GtkTreeSelection *selection,gpointer data)
{
    (void)selection;History *view=data;guint index;if(view->changing || view->closed || !selected(view->commits,&index) || !view->revisions || index>=view->revisions->len) return;
    InsightGitRevision *row=g_ptr_array_index(view->revisions,index);gchar *oid=g_strdup(row->oid);clear_revision(view);Job *job=g_new0(Job,1);job->kind=REVISION;job->oid=oid;launch(view,job);
}
static void parent_changed(GtkComboBox *combo,gpointer data)
{
    History *view=data;if(view->changing || view->closed || !view->revision) return;gint index=gtk_combo_box_get_active(combo);if(index<0) return;Job *job=g_new0(Job,1);job->kind=FILES;job->oid=g_strdup(view->revision->oid);if((guint)index<view->revision->parents->len) job->parent=g_strdup(g_ptr_array_index(view->revision->parents,index));clear_files(view);launch(view,job);
}
static void choose_file(GtkTreeSelection *selection,gpointer data)
{
    (void)selection;History *view=data;guint index;if(view->changing || view->closed || !selected(view->files,&index) || !view->deltas || index>=view->deltas->len) return;
    InsightGitDelta *delta=g_ptr_array_index(view->deltas,index);clear_text(view);Job *job=g_new0(Job,1);job->kind=TEXT;job->blob[0]=g_strdup(delta->old_oid);job->blob[1]=g_strdup(delta->new_oid);job->modes[0]=delta->old_mode;job->modes[1]=delta->new_mode;launch(view,job);
}
static void refresh(GtkWidget *widget,gpointer data)
{
    (void)widget;History *view=data;view->changing=TRUE;gtk_list_store_clear(view->commit_model);clear_revision(view);view->changing=FALSE;Job *job=g_new0(Job,1);job->kind=REFS;launch(view,job);
}
static void page(GtkWidget *widget,gpointer data)
{
    History *view=data;if(view->busy || !view->tip) return;
    if(widget==view->page_next) { if(!view->more || view->offset>=100000) return;view->offset+=INSIGHT_GIT_HISTORY_PAGE; }
    else { if(!view->offset) return;view->offset-=INSIGHT_GIT_HISTORY_PAGE; }load_log(view);
}
static void move_row(GtkWidget *tree,gint direction)
{
    guint index=0;if(!selected(tree,&index)) return;GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));gint next=(gint)index+direction;if(next<0 || next>=gtk_tree_model_iter_n_children(model,NULL)) return;GtkTreePath *path=gtk_tree_path_new_from_indices(next,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree),path,NULL,FALSE,0,0);gtk_tree_path_free(path);
}
static void older(GtkWidget *widget,gpointer data) { (void)widget;move_row(((History *)data)->commits,1); }
static void newer(GtkWidget *widget,gpointer data) { (void)widget;move_row(((History *)data)->commits,-1); }
static void next_hunk(GtkWidget *widget,gpointer data) { (void)widget;move_row(((History *)data)->hunks,1); }
static void prev_hunk(GtkWidget *widget,gpointer data) { (void)widget;move_row(((History *)data)->hunks,-1); }
static void cancel(GtkWidget *widget,gpointer data) { (void)widget;History *view=data;if(view->cancel) g_cancellable_cancel(view->cancel); }
static void close_window(GtkWidget *widget,gpointer data) { (void)widget;gtk_widget_destroy(((History *)data)->window); }
static GtkWidget *button(GtkWidget *box,const gchar *label,const gchar *name,GCallback callback,History *view)
{ GtkWidget *widget=gtk_button_new_with_label(insight_tr(label));gtk_widget_set_name(widget,name);gtk_box_pack_start(GTK_BOX(box),widget,FALSE,FALSE,0);g_signal_connect(widget,"clicked",callback,view);return widget; }
static GtkWidget *scrolled(GtkWidget *child)
{ GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),child);return scroll; }
static GtkWidget *tree(GtkListStore *model,const gchar *name,const gchar *const *titles,guint columns)
{
    GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));gtk_widget_set_name(view,name);
    for(guint i=0;i<columns;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(insight_tr(titles[i]),gtk_cell_renderer_text_new(),"text",i+1,NULL);gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_append_column(GTK_TREE_VIEW(view),column); }return view;
}
void insight_git_history_show(GtkWidget *parent,const gchar *location)
{
    History *view=g_new0(History,1);view->root=g_strdup(location);view->commit_model=gtk_list_store_new(5,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);view->file_model=gtk_list_store_new(3,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING);view->hunk_model=gtk_list_store_new(3,G_TYPE_UINT,G_TYPE_INT,G_TYPE_INT);
    view->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(view->window,"insight-git-history-window");gtk_window_set_title(GTK_WINDOW(view->window),insight_tr("Git History and Diff"));gtk_window_set_default_size(GTK_WINDOW(view->window),INSIGHT_COMPARE_WIDTH,INSIGHT_COMPARE_HEIGHT);gtk_window_set_transient_for(GTK_WINDOW(view->window),GTK_WINDOW(parent));gtk_window_set_destroy_with_parent(GTK_WINDOW(view->window),TRUE);g_object_set_data_full(G_OBJECT(view->window),STATE,view,history_free);g_signal_connect(view->window,"destroy",G_CALLBACK(destroyed),view);
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_container_set_border_width(GTK_CONTAINER(box),6);gtk_container_add(GTK_CONTAINER(view->window),box);
    view->root_label=gtk_label_new("");g_object_set_data(G_OBJECT(view->root_label),"insight-no-i18n",GINT_TO_POINTER(1));gtk_label_set_ellipsize(GTK_LABEL(view->root_label),PANGO_ELLIPSIZE_MIDDLE);gtk_box_pack_start(GTK_BOX(box),view->root_label,FALSE,FALSE,0);
    GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);view->branch=gtk_combo_box_text_new();gtk_widget_set_name(view->branch,"insight-git-history-branch");gtk_widget_set_size_request(view->branch,260,-1);gtk_box_pack_start(GTK_BOX(bar),view->branch,TRUE,TRUE,0);g_signal_connect(view->branch,"changed",G_CALLBACK(branch_changed),view);
    button(bar,"Refresh","insight-git-history-refresh",G_CALLBACK(refresh),view);button(bar,"Newer commit","insight-git-history-newer",G_CALLBACK(newer),view);button(bar,"Older commit","insight-git-history-older",G_CALLBACK(older),view);view->page_previous=button(bar,"Previous page","insight-git-history-page-previous",G_CALLBACK(page),view);view->page_next=button(bar,"Next page","insight-git-history-page-next",G_CALLBACK(page),view);button(bar,"Cancel loading","insight-git-history-cancel",G_CALLBACK(cancel),view);button(bar,"Close","insight-git-history-close",G_CALLBACK(close_window),view);
    GtkWidget *body=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);gtk_box_pack_start(GTK_BOX(box),body,TRUE,TRUE,0);GtkWidget *lists=gtk_paned_new(GTK_ORIENTATION_VERTICAL);gtk_paned_pack1(GTK_PANED(body),lists,TRUE,FALSE);gtk_paned_set_position(GTK_PANED(body),430);
    const gchar *commit_titles[]={"Commit","Message","Author","Date"},*file_titles[]={"Status","Changed file"},*hunk_titles[]={"Parent line","Commit line"};view->commits=tree(view->commit_model,"insight-git-history-commits",commit_titles,4);view->files=tree(view->file_model,"insight-git-history-files",file_titles,2);gtk_paned_pack1(GTK_PANED(lists),scrolled(view->commits),TRUE,FALSE);gtk_paned_pack2(GTK_PANED(lists),scrolled(view->files),TRUE,FALSE);gtk_paned_set_position(GTK_PANED(lists),320);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->commits)),"changed",G_CALLBACK(choose_commit),view);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->files)),"changed",G_CALLBACK(choose_file),view);
    GtkWidget *right=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_paned_pack2(GTK_PANED(body),right,TRUE,FALSE);
    view->message=gtk_text_view_new();gtk_widget_set_name(view->message,"insight-git-history-message");gtk_text_view_set_editable(GTK_TEXT_VIEW(view->message),FALSE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view->message),GTK_WRAP_WORD_CHAR);GtkWidget *message_scroll=scrolled(view->message);gtk_widget_set_size_request(message_scroll,-1,145);gtk_box_pack_start(GTK_BOX(right),message_scroll,FALSE,TRUE,0);
    view->parents=gtk_combo_box_text_new();gtk_widget_set_name(view->parents,"insight-git-history-parent");gtk_box_pack_start(GTK_BOX(right),view->parents,FALSE,FALSE,0);g_signal_connect(view->parents,"changed",G_CALLBACK(parent_changed),view);
    GtkWidget *code_panes=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);gtk_box_pack_start(GTK_BOX(right),code_panes,TRUE,TRUE,0);gtk_paned_set_position(GTK_PANED(code_panes),390);
    for(guint i=0;i<2;i++) {
        GtkWidget *column=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);view->labels[i]=gtk_label_new("");gtk_label_set_selectable(GTK_LABEL(view->labels[i]),TRUE);gtk_label_set_ellipsize(GTK_LABEL(view->labels[i]),PANGO_ELLIPSIZE_MIDDLE);gtk_widget_set_margin_end(view->labels[i],24);gtk_box_pack_start(GTK_BOX(column),insight_panel_help_overlay(view->labels[i],"git-history.md"),FALSE,FALSE,0);view->code[i]=SCINTILLA(scintilla_new());gtk_widget_set_name(GTK_WIDGET(view->code[i]),i ? "insight-git-history-right":"insight-git-history-left");SEND(view->code[i],SCI_SETCODEPAGE,SC_CP_UTF8,0);sci_set_symbol_margin(view->code[i],FALSE);SEND(view->code[i],SCI_SETMARGINWIDTHN,2,0);gtk_box_pack_start(GTK_BOX(column),GTK_WIDGET(view->code[i]),TRUE,TRUE,0);if(i) gtk_paned_pack2(GTK_PANED(code_panes),column,TRUE,FALSE);else gtk_paned_pack1(GTK_PANED(code_panes),column,TRUE,FALSE);set_code(view,i,"",NULL);
    }
    GtkWidget *navigation=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);gtk_box_pack_start(GTK_BOX(right),navigation,FALSE,FALSE,0);button(navigation,"Previous difference","insight-git-history-diff-previous",G_CALLBACK(prev_hunk),view);button(navigation,"Next difference","insight-git-history-diff-next",G_CALLBACK(next_hunk),view);
    view->hunks=tree(view->hunk_model,"insight-git-history-hunks",hunk_titles,2);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->hunks)),"changed",G_CALLBACK(select_hunk),view);GtkWidget *hunk_scroll=scrolled(view->hunks);gtk_widget_set_size_request(hunk_scroll,-1,85);gtk_box_pack_start(GTK_BOX(right),hunk_scroll,FALSE,TRUE,0);
    GtkWidget *tabs=insight_compare_ui_tabs(box,"insight-git-history-toolbar-tabs"),*history_page=insight_compare_ui_page(tabs,"Merge and history"),*navigation_page=insight_compare_ui_page(tabs,"Navigation");
    insight_compare_ui_move(view->root_label,history_page);insight_compare_ui_move(bar,history_page);insight_compare_ui_move(view->parents,history_page);insight_compare_ui_move(navigation,navigation_page);
    view->status=gtk_label_new("");gtk_widget_set_name(view->status,"insight-git-history-status");gtk_label_set_line_wrap(GTK_LABEL(view->status),TRUE);gtk_box_pack_start(GTK_BOX(box),view->status,FALSE,FALSE,0);gtk_widget_show_all(view->window);refresh(NULL,view);
}
