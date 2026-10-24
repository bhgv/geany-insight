/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-compare-ui.h"
#include "geany.h"
#include "insight-snapshot-compare.h"
#include "insight-revisions.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "keybindings.h"
#include "ui_utils.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include "../insight/core/diff.h"
#include "../insight/desktop/text_transfer.h"
#include <string.h>
#define STATE "insight-snapshot-comparison"
typedef struct {
    GtkWidget *window,*views[2],*tree,*status,*run,*cancel_button,*copy,*previous,*next,*space,*eol;
    GtkListStore *model;
    gchar *text[2],*names[2];
    InsightDiff diff;
    GCancellable *cancel;
    guint64 serial;
    gboolean busy,closed;
} CompareView;
typedef struct { gchar *text[2];InsightDiff diff;guint options;guint64 serial; } CompareJob;
static gboolean initialized;
static void job_free(gpointer data)
{
    CompareJob *job=data;g_free(job->text[0]);g_free(job->text[1]);insight_diff_clear(&job->diff);g_free(job);
}
static void view_free(gpointer data)
{
    CompareView *view=data;g_clear_object(&view->cancel);g_object_unref(view->model);
    for(guint i=0;i<2;i++) { g_free(view->text[i]);g_free(view->names[i]); }
    insight_diff_clear(&view->diff);g_free(view);
}
static void closing(GtkWidget *widget,gpointer data)
{
    (void)widget;CompareView *view=data;view->closed=TRUE;
    if(view->cancel) g_cancellable_cancel(view->cancel);
}
static gboolean selected(CompareView *view,guint *index)
{
    GtkTreeIter iter;GtkTreeModel *model=NULL;
    if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree)),&model,&iter)) return FALSE;
    gtk_tree_model_get(model,&iter,0,index,-1);return *index<view->diff.count;
}
static void select_row(GtkTreeSelection *selection,gpointer data)
{
    (void)selection;CompareView *view=data;if(view->closed) return;guint index;
    if(!selected(view,&index)) return;
    InsightDiffHunk *h=&view->diff.hunks[index];
    for(guint side=0;side<2;side++) {
        gsize begin=side ? h->b_begin:h->a_begin,end=side ? h->b_end:h->a_end;
        GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->views[side]));GtkTextIter a,z;
        gtk_text_buffer_get_iter_at_offset(buffer,&a,g_utf8_pointer_to_offset(view->text[side],view->text[side]+begin));
        gtk_text_buffer_get_iter_at_offset(buffer,&z,g_utf8_pointer_to_offset(view->text[side],view->text[side]+end));
        gtk_text_buffer_select_range(buffer,&z,&a);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(view->views[side]),&a,0.15,TRUE,0,0.3);
    }
}
static void controls(CompareView *view)
{
    gboolean rows=!view->busy && view->diff.count>0;
    gtk_widget_set_sensitive(view->run,!view->busy);gtk_widget_set_sensitive(view->cancel_button,view->busy);
    gtk_widget_set_sensitive(view->space,!view->busy);gtk_widget_set_sensitive(view->eol,!view->busy);
    gtk_widget_set_sensitive(view->copy,rows);gtk_widget_set_sensitive(view->previous,rows);gtk_widget_set_sensitive(view->next,rows);
    g_object_set_data(G_OBJECT(view->window),"insight-snapshot-busy",GINT_TO_POINTER(view->busy));
}
static void clear_rows(CompareView *view)
{
    gtk_list_store_clear(view->model);insight_diff_clear(&view->diff);
    for(guint i=0;i<2;i++) {
        GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->views[i]));GtkTextIter a,z;
        gtk_text_buffer_get_bounds(buffer,&a,&z);gtk_text_buffer_remove_tag_by_name(buffer,"difference",&a,&z);
        gtk_text_buffer_place_cursor(buffer,&a);
    }
    controls(view);
}
static guint line_at(const gchar *text,gsize offset)
{
    guint line=1;
    for(gsize i=0;i<offset;i++) {
        if(text[i]=='\r') { line++;if(i+1<offset && text[i+1]=='\n') i++; }
        else if(text[i]=='\n') line++;
    }
    return line;
}
static gchar *line_range(const gchar *text,gsize begin,gsize end)
{
    if(begin==end) return g_strdup_printf("%u (%s)",line_at(text,begin),insight_tr("anchor"));
    gsize last=end;
    if(last>begin && text[last-1]=='\n') last--;
    if(last>begin && text[last-1]=='\r') last--;
    return g_strdup_printf("%u–%u",line_at(text,begin),line_at(text,last>begin ? last-1:begin));
}
static int cancelled(void *data) { return g_cancellable_is_cancelled(data); }
static void worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel)
{
    (void)source;CompareJob *job=data;
    int rc=insight_diff_compute_cancellable(job->text[0],job->text[1],job->options,cancelled,cancel,&job->diff);
    if(rc) g_task_return_new_error(task,G_IO_ERROR,rc==-3 ? G_IO_ERROR_CANCELLED:G_IO_ERROR_FAILED,"%s",
        rc==-3 ? "Snapshot comparison cancelled." : rc==-2 ? "Snapshot comparison exceeds the 4 million LCS cell limit." : "Cannot allocate snapshot comparison.");
    else g_task_return_boolean(task,TRUE);
}
static void completed(GObject *object,GAsyncResult *result,gpointer data)
{
    (void)data;CompareView *view=g_object_get_data(object,STATE);GError *error=NULL;
    gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);CompareJob *job=g_task_get_task_data(G_TASK(result));
    if(!view || view->closed) { g_clear_error(&error);return; }
    g_clear_object(&view->cancel);view->busy=FALSE;
    if(job->serial!=view->serial) { controls(view);g_clear_error(&error);return; }
    if(!ok) { controls(view);gtk_label_set_text(GTK_LABEL(view->status),insight_tr(error->message));g_clear_error(&error);return; }
    view->diff=job->diff;job->diff=(InsightDiff){0};
    for(guint i=0;i<view->diff.count;i++) {
        InsightDiffHunk *h=&view->diff.hunks[i];GtkTreeIter iter;
        const gchar *kind=h->a_begin==h->a_end ? "Inserted" : h->b_begin==h->b_end ? "Deleted":"Changed";
        gchar *left=line_range(view->text[0],h->a_begin,h->a_end),*right=line_range(view->text[1],h->b_begin,h->b_end);
        gtk_list_store_append(view->model,&iter);gtk_list_store_set(view->model,&iter,0,i,1,insight_tr(kind),2,left,3,right,-1);g_free(left);g_free(right);
        for(guint side=0;side<2;side++) {
            gsize begin=side ? h->b_begin:h->a_begin,end=side ? h->b_end:h->a_end;GtkTextIter a,z;
            GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->views[side]));
            gtk_text_buffer_get_iter_at_offset(buffer,&a,g_utf8_pointer_to_offset(view->text[side],view->text[side]+begin));
            gtk_text_buffer_get_iter_at_offset(buffer,&z,g_utf8_pointer_to_offset(view->text[side],view->text[side]+end));gtk_text_buffer_apply_tag_by_name(buffer,"difference",&a,&z);
        }
    }
    controls(view);
    gtk_label_set_text(GTK_LABEL(view->status),insight_tr(view->diff.count ? "Snapshot differences ready. Source buffers and disk files are unchanged." : "No differences under the selected comparison options."));
    if(view->diff.count) { GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(view->tree),path,NULL,FALSE);gtk_tree_path_free(path); }
}
static void run(GtkWidget *widget,gpointer data)
{
    (void)widget;CompareView *view=data;if(view->busy || view->closed) return;
    view->busy=TRUE;clear_rows(view);view->serial++;view->cancel=g_cancellable_new();
    CompareJob *job=g_new0(CompareJob,1);job->serial=view->serial;
    for(guint i=0;i<2;i++) job->text[i]=g_strdup(view->text[i]);
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->space))) job->options|=INSIGHT_DIFF_IGNORE_SPACE;
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->eol))) job->options|=INSIGHT_DIFF_IGNORE_EOL;
    gtk_label_set_text(GTK_LABEL(view->status),insight_tr("Comparing captured snapshots…"));
    GTask *task=g_task_new(view->window,view->cancel,completed,NULL);g_task_set_task_data(task,job,job_free);g_task_run_in_thread(task,worker);g_object_unref(task);
}
static void option_changed(GtkWidget *widget,gpointer data)
{
    (void)widget;CompareView *view=data;if(view->closed) return;view->serial++;
    if(view->cancel) g_cancellable_cancel(view->cancel);
    clear_rows(view);gtk_label_set_text(GTK_LABEL(view->status),insight_tr("Options changed. Compare again to rebuild the difference list."));
}
static void cancel_compare(GtkWidget *widget,gpointer data)
{ (void)widget;CompareView *view=data;if(view->cancel) g_cancellable_cancel(view->cancel); }
static void close_view(GtkWidget *widget,gpointer data)
{ (void)widget;gtk_widget_destroy(((CompareView *)data)->window); }
static void step(GtkWidget *widget,gpointer data)
{
    CompareView *view=data;if(view->busy || !view->diff.count) return;guint index=0;
    gboolean has=selected(view,&index);if(!has) index=0;
    else if(widget==view->next && index+1<view->diff.count) index++;
    else if(widget==view->previous && index>0) index--;
    GtkTreePath *path=gtk_tree_path_new_from_indices(index,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(view->tree),path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(view->tree),path,NULL,FALSE,0,0);gtk_tree_path_free(path);
}
static void copy_report(GtkWidget *widget,gpointer data)
{
    (void)widget;CompareView *view=data;if(view->busy || !view->diff.count) return;
    GString *report=g_string_new("Kind\tLeft lines\tRight lines\n");GtkTreeIter iter;
    gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(view->model),&iter);
    while(valid) {
        gchar *kind=NULL,*left=NULL,*right=NULL;gtk_tree_model_get(GTK_TREE_MODEL(view->model),&iter,1,&kind,2,&left,3,&right,-1);
        g_string_append_printf(report,"%s\t%s\t%s\n",kind,left,right);g_free(kind);g_free(left);g_free(right);
        if(report->len>1024u*1024u) { gtk_label_set_text(GTK_LABEL(view->status),insight_tr("Difference report exceeds 1 MiB. Clipboard unchanged."));g_string_free(report,TRUE);return; }
        valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(view->model),&iter);
    }
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),report->str,report->len);g_string_free(report,TRUE);
}
static GtkWidget *button(GtkWidget *box,const gchar *text,const gchar *name,GCallback callback,CompareView *view)
{
    GtkWidget *widget=gtk_button_new_with_label(insight_tr(text));gtk_widget_set_name(widget,name);g_signal_connect(widget,"clicked",callback,view);gtk_box_pack_start(GTK_BOX(box),widget,FALSE,FALSE,0);return widget;
}
gboolean insight_snapshot_compare_show(GtkWidget *parent,const gchar *left_name,const gchar *left,gsize left_size,const gchar *right_name,const gchar *right,gsize right_size)
{
    const gchar *text[]={left,right};gsize sizes[]={left_size,right_size};
    for(guint i=0;i<2;i++) if(!text[i] || sizes[i]>INSIGHT_SNAPSHOT_COMPARE_LIMIT || memchr(text[i],0,sizes[i]) || !g_utf8_validate(text[i],sizes[i],NULL)) return FALSE;
    CompareView *view=g_new0(CompareView,1);view->model=gtk_list_store_new(4,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);
    view->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(view->window,"insight-snapshot-compare-window");gtk_window_set_title(GTK_WINDOW(view->window),insight_tr("Snapshot comparison"));gtk_window_set_default_size(GTK_WINDOW(view->window),INSIGHT_COMPARE_WIDTH,INSIGHT_COMPARE_HEIGHT);
    gtk_window_set_transient_for(GTK_WINDOW(view->window),GTK_WINDOW(parent));gtk_window_set_destroy_with_parent(GTK_WINDOW(view->window),TRUE);
    g_object_set_data_full(G_OBJECT(view->window),STATE,view,view_free);g_signal_connect(view->window,"destroy",G_CALLBACK(closing),view);
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(view->window),box);
    GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
    view->run=button(bar,"Compare","insight-snapshot-run",G_CALLBACK(run),view);view->cancel_button=button(bar,"Cancel comparison","insight-snapshot-cancel",G_CALLBACK(cancel_compare),view);
    view->previous=button(bar,"Previous difference","insight-snapshot-previous",G_CALLBACK(step),view);view->next=button(bar,"Next difference","insight-snapshot-next",G_CALLBACK(step),view);
    view->copy=button(bar,"Copy difference report","insight-snapshot-copy",G_CALLBACK(copy_report),view);button(bar,"Close","insight-snapshot-close",G_CALLBACK(close_view),view);
    GtkWidget *options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_pack_start(GTK_BOX(box),options,FALSE,FALSE,0);
    view->space=gtk_check_button_new_with_label(insight_tr("Ignore spaces and tabs"));view->eol=gtk_check_button_new_with_label(insight_tr("Ignore line ending style"));gtk_widget_set_name(view->space,"insight-snapshot-ignore-space");gtk_widget_set_name(view->eol,"insight-snapshot-ignore-eol");
    gtk_box_pack_start(GTK_BOX(options),view->space,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(options),view->eol,FALSE,FALSE,0);
    g_signal_connect(view->space,"toggled",G_CALLBACK(option_changed),view);g_signal_connect(view->eol,"toggled",G_CALLBACK(option_changed),view);
    GtkWidget *tabs=insight_compare_ui_tabs(box,"insight-snapshot-toolbar-tabs"),*files=insight_compare_ui_page(tabs,"Files and comparison"),*view_page=insight_compare_ui_page(tabs,"View"),*navigation=insight_compare_ui_page(tabs,"Navigation");
    insight_compare_ui_move(bar,files);insight_compare_ui_move(options,files);GtkWidget *navigation_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*report_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_box_pack_start(GTK_BOX(navigation),navigation_row,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(view_page),report_row,FALSE,FALSE,0);
    insight_compare_ui_move(view->previous,navigation_row);insight_compare_ui_move(view->next,navigation_row);insight_compare_ui_move(view->copy,report_row);
    GtkWidget *panes=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);gtk_box_pack_start(GTK_BOX(box),panes,TRUE,TRUE,0);
    const gchar *names[]={left_name,right_name};
    for(guint i=0;i<2;i++) {
        view->text[i]=g_strndup(text[i],sizes[i]);view->names[i]=g_strdup(names[i] ? names[i]:"");
        GtkWidget *column=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*label=gtk_label_new(view->names[i]);gtk_widget_set_name(label,i ? "insight-snapshot-right-name":"insight-snapshot-left-name");g_object_set_data(G_OBJECT(label),"insight-no-i18n",GINT_TO_POINTER(1));gtk_widget_set_tooltip_text(label,view->names[i]);gtk_label_set_selectable(GTK_LABEL(label),TRUE);gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_MIDDLE);gtk_widget_set_margin_end(label,24);gtk_box_pack_start(GTK_BOX(column),insight_panel_help_overlay(label,"snapshot-comparison.md"),FALSE,FALSE,0);
        view->views[i]=gtk_text_view_new();gtk_style_context_add_class(gtk_widget_get_style_context(view->views[i]),"insight-compare-code");gtk_widget_set_name(view->views[i],i ? "insight-snapshot-right":"insight-snapshot-left");gtk_text_view_set_editable(GTK_TEXT_VIEW(view->views[i]),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view->views[i]),TRUE);
        GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->views[i]));gtk_text_buffer_set_text(buffer,view->text[i],sizes[i]);gtk_text_buffer_create_tag(buffer,"difference","background",i ? "#d5f5d5":"#ffe0d0","foreground","#202020",NULL);
        GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view->views[i]);gtk_box_pack_start(GTK_BOX(column),scroll,TRUE,TRUE,0);if(i) gtk_paned_pack2(GTK_PANED(panes),column,TRUE,FALSE);else gtk_paned_pack1(GTK_PANED(panes),column,TRUE,FALSE);
    }
    gtk_paned_set_position(GTK_PANED(panes),460);
    view->tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(view->model));gtk_widget_set_name(view->tree,"insight-snapshot-differences");
    const gchar *columns[]={"Kind","Left lines","Right lines"};for(guint i=0;i<3;i++) { GtkCellRenderer *renderer=gtk_cell_renderer_text_new();if(!i) insight_i18n_cell(renderer);gtk_tree_view_append_column(GTK_TREE_VIEW(view->tree),gtk_tree_view_column_new_with_attributes(i ? view->names[i-1]:insight_tr(columns[i]),renderer,"text",i+1,NULL)); }
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree)),"changed",G_CALLBACK(select_row),view);
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_size_request(scroll,-1,135);gtk_container_add(GTK_CONTAINER(scroll),view->tree);gtk_box_pack_start(GTK_BOX(box),scroll,FALSE,TRUE,0);
    view->status=gtk_label_new("");gtk_widget_set_name(view->status,"insight-snapshot-status");gtk_label_set_line_wrap(GTK_LABEL(view->status),TRUE);gtk_box_pack_start(GTK_BOX(box),view->status,FALSE,FALSE,0);
    gtk_widget_show_all(view->window);run(NULL,view);return TRUE;
}
void insight_snapshot_compare_command(guint action)
{
    GeanyDocument *doc=document_get_current();if(!doc || !doc->is_valid) return;
    gint length=sci_get_length(doc->editor->sci);gchar *left=NULL,*right=NULL,*name=NULL,*right_name=NULL;gsize size=0;GError *error=NULL;
    if(length<0 || (guint)length>INSIGHT_SNAPSHOT_COMPARE_LIMIT) goto invalid;
    right=sci_get_contents(doc->editor->sci,-1);
    const gchar *source_name=doc->file_name ? doc->file_name:insight_tr("Untitled");
    right_name=g_strdup_printf("%s — %s",insight_tr("Current buffer snapshot"),source_name);
    if(action==0) {
        if(!doc->file_name || !g_path_is_absolute(doc->file_name)) goto invalid;
        gchar *path=g_filename_from_utf8(doc->file_name,-1,NULL,NULL,&error);
        if(path) left=insight_text_file_read(path,&size,&error);
        g_free(path);name=g_strdup_printf("%s — %s",insight_tr("Disk snapshot (UTF-8)"),source_name);
    } else {
        left=insight_revisions_original_text(doc->editor->sci);if(left) size=strlen(left);
        name=g_strdup_printf("%s — %s",insight_tr("Original Contents snapshot"),source_name);
    }
    if(!left || !insight_snapshot_compare_show(main_widgets.window,name,left,size,right_name,right,length)) goto invalid;
    goto done;
invalid:
    ui_set_statusbar(TRUE,"%s",insight_tr(error ? error->message : "Comparison requires available UTF-8 snapshots without NUL, at most 1 MiB per side."));
done:
    g_clear_error(&error);g_free(left);g_free(right);g_free(name);g_free(right_name);
}
static void activate(GtkMenuItem *item,gpointer data) { (void)item;insight_snapshot_compare_command(GPOINTER_TO_UINT(data)); }
void insight_snapshot_compare_init(void)
{
    if(initialized) return;initialized=TRUE;GtkWidget *menu=ui_lookup_widget(main_widgets.window,"file1_menu");
    const gchar *labels[]={"Compare Current Buffer with Disk","Compare Current Buffer with Original"};const gchar *names[]={"insight-compare-disk","insight-compare-original"};const guint keys[]={GEANY_KEYS_INSIGHT_COMPAREDISK,GEANY_KEYS_INSIGHT_COMPAREORIGINAL};
    for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,names[i]);g_signal_connect(item,"activate",G_CALLBACK(activate),GUINT_TO_POINTER(i));keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,keys[i],item);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item); }
}
