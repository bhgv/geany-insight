/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-project-report.h"
#include "app.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include <string.h>
#define STATE "insight-project-report-state"
typedef struct {
    GtkWidget *dialog,*preview,*status,*include,*lines,*sort,*relative,*types;
    InsightReportSnapshot *snapshot;
    GHashTable *kinds;
    GPtrArray *available;
    GCancellable *cancel;
    gchar *text;
    guint64 serial;
    gboolean file_list,busy,closed;
} ReportView;
typedef struct { InsightReportSnapshot *snapshot;InsightReportOptions options;gchar *text;guint64 serial; } ReportJob;
static InsightReportCapture provider;
static gboolean initialized,command_busy;
static guint64 report_serial;
static void job_free(gpointer data)
{
    ReportJob *job=data;insight_report_snapshot_unref(job->snapshot);g_clear_pointer(&job->options.kinds,g_hash_table_unref);g_free(job->text);g_free(job);
}
static void view_free(gpointer data)
{
    ReportView *view=data;insight_report_snapshot_unref(view->snapshot);g_hash_table_unref(view->kinds);g_ptr_array_unref(view->available);g_clear_object(&view->cancel);g_free(view->text);g_free(view);
}
static void destroyed(GtkWidget *widget,gpointer data)
{ (void)widget;ReportView *view=data;view->closed=TRUE;if(view->cancel) g_cancellable_cancel(view->cancel); }
static void controls(ReportView *view)
{
    gboolean ready=view->text && !view->busy;
    const gint outputs[]={GTK_RESPONSE_ACCEPT,202,203};for(guint i=0;i<G_N_ELEMENTS(outputs);i++) gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(view->dialog),outputs[i]),ready);
    gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(view->dialog),201),!view->busy);
    gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(view->dialog),204),view->busy);
    gtk_widget_set_sensitive(view->include,!view->busy && !view->file_list);gtk_widget_set_sensitive(view->lines,!view->busy && !view->file_list);
    gtk_widget_set_sensitive(view->sort,!view->busy && !view->file_list);gtk_widget_set_sensitive(view->types,!view->busy && !view->file_list);gtk_widget_set_sensitive(view->relative,!view->busy);
    g_object_set_data(G_OBJECT(view->dialog),"insight-report-busy",GINT_TO_POINTER(view->busy));
}
static void dirty(GtkWidget *widget,gpointer data)
{
    (void)widget;ReportView *view=data;if(view->closed) return;view->serial++;
    if(view->cancel) g_cancellable_cancel(view->cancel);g_clear_pointer(&view->text,g_free);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),"",0);
    gtk_label_set_text(GTK_LABEL(view->status),insight_tr("Report options changed. Generate a new preview."));controls(view);
}
static void worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel)
{
    (void)source;ReportJob *job=data;GError *error=NULL;
    job->text=insight_project_report_build(job->snapshot,&job->options,cancel,NULL,&error);
    if(job->text) g_task_return_boolean(task,TRUE);else g_task_return_error(task,error);
}
static void completed(GObject *object,GAsyncResult *result,gpointer data)
{
    (void)data;ReportView *view=g_object_get_data(object,STATE);GError *error=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);ReportJob *job=g_task_get_task_data(G_TASK(result));
    if(!view || view->closed) { g_clear_error(&error);return; }
    g_clear_object(&view->cancel);view->busy=FALSE;
    if(view->serial!=job->serial) { controls(view);g_clear_error(&error);return; }
    if(!ok) { gtk_label_set_text(GTK_LABEL(view->status),insight_tr(error->message));g_clear_error(&error);controls(view);return; }
    view->text=job->text;job->text=NULL;gsize size=strlen(view->text),shown=MIN(size,32768u);
    while(shown && shown<size && ((guchar)view->text[shown]&0xc0)==0x80) shown--;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),view->text,shown);
    gtk_label_set_text(GTK_LABEL(view->status),insight_tr(size>shown ? "Preview shows the first 32 KiB. Copy, Open and Save use the complete report." : "Report ready. Copy, Open or Save the captured project snapshot."));controls(view);
}
static GHashTable *copy_kinds(GHashTable *source)
{
    GHashTable *copy=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);GHashTableIter iter;gpointer key;g_hash_table_iter_init(&iter,source);while(g_hash_table_iter_next(&iter,&key,NULL)) g_hash_table_add(copy,g_strdup(key));return copy;
}
static void generate(ReportView *view)
{
    if(view->busy) return;dirty(NULL,view);view->busy=TRUE;view->cancel=g_cancellable_new();controls(view);
    ReportJob *job=g_new0(ReportJob,1);job->snapshot=insight_report_snapshot_ref(view->snapshot);job->serial=view->serial;
    job->options.file_list=view->file_list;job->options.include_symbols=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->include));job->options.line_numbers=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->lines));job->options.sort_names=gtk_combo_box_get_active(GTK_COMBO_BOX(view->sort))==1;job->options.relative_paths=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->relative));job->options.kinds=copy_kinds(view->kinds);
    gtk_label_set_text(GTK_LABEL(view->status),insight_tr("Generating captured project report…"));
    GTask *task=g_task_new(view->dialog,view->cancel,completed,NULL);g_task_set_task_data(task,job,job_free);g_task_run_in_thread(task,worker);g_object_unref(task);
}
static void types_dialog(GtkWidget *widget,gpointer data)
{
    (void)widget;ReportView *view=data;if(view->busy) return;
    GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Report Symbol Types"),GTK_WINDOW(view->dialog),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Apply"),GTK_RESPONSE_APPLY,NULL);g_object_ref_sink(dialog);gtk_widget_set_name(dialog,"insight-report-types-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),360,360);
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),box);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),scroll,TRUE,TRUE,0);
    GPtrArray *checks=g_ptr_array_new();
    for(guint i=0;i<view->available->len;i++) { const gchar *kind=g_ptr_array_index(view->available,i);GtkWidget *check=gtk_check_button_new_with_label(kind);g_object_set_data(G_OBJECT(check),"insight-no-i18n",GINT_TO_POINTER(1));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),g_hash_table_contains(view->kinds,kind));gtk_box_pack_start(GTK_BOX(box),check,FALSE,FALSE,0);g_ptr_array_add(checks,check); }
    gtk_widget_show_all(dialog);
    if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_APPLY) { g_hash_table_remove_all(view->kinds);for(guint i=0;i<checks->len;i++) if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_ptr_array_index(checks,i)))) g_hash_table_add(view->kinds,g_strdup(g_ptr_array_index(view->available,i)));dirty(NULL,view); }
    gtk_widget_destroy(dialog);g_object_unref(dialog);g_ptr_array_unref(checks);
}
static gchar *suggested_name(ReportView *view)
{
    gchar *base=g_path_get_basename(view->snapshot->project);for(gchar *p=base;*p;p++) if(strchr("/\\:\r\n\t",*p)) *p='_';
    gchar *name=g_strconcat(*base ? base:"project",view->file_list ? "-files.txt":".RPT",NULL);g_free(base);return name;
}
static void save_report(ReportView *view)
{
    GtkWidget *dialog=gtk_file_chooser_dialog_new(insight_tr(view->file_list ? "Export Project File List":"Save Project Report"),GTK_WINDOW(view->dialog),GTK_FILE_CHOOSER_ACTION_SAVE,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Save"),GTK_RESPONSE_ACCEPT,NULL);g_object_ref_sink(dialog);gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),TRUE);gtk_widget_set_name(dialog,"insight-report-save-dialog");gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog),TRUE);gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);gchar *name=suggested_name(view);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),name);g_free(name);
    if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
        gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));GError *error=NULL;GPtrArray *open=g_ptr_array_new_with_free_func(g_free);
        for(guint i=0;documents_array && i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(doc->is_valid && doc->file_name && g_path_is_absolute(doc->file_name)) { gchar *native=g_filename_from_utf8(doc->file_name,-1,NULL,NULL,&error);if(!native) break;g_ptr_array_add(open,native); } }
        if(!error && app->project && app->project->file_name) { gchar *native=g_filename_from_utf8(app->project->file_name,-1,NULL,NULL,&error);if(native) g_ptr_array_add(open,native); }
        gboolean ok=!error && path && view->text && insight_project_report_save(view->snapshot,path,view->text,strlen(view->text),open,&error);
        gtk_label_set_text(GTK_LABEL(view->status),insight_tr(ok ? "Project report saved. Source files and project settings are unchanged." : error ? error->message:"Cannot save project report."));
        g_clear_error(&error);g_ptr_array_unref(open);g_free(path);
    }
    gtk_widget_destroy(dialog);g_object_unref(dialog);
}
static gint string_order(gconstpointer a,gconstpointer b) { return strcmp(*(gchar * const *)a,*(gchar * const *)b); }
InsightReportSnapshot *insight_project_report_capture(GError **error)
{
    if(provider) return provider(error);
    g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_INITIALIZED,"Project membership is not ready.");return NULL;
}
void insight_project_report_command(guint action)
{
    if(command_busy || !provider) return;GError *error=NULL;InsightReportSnapshot *snapshot=provider(&error);
    if(!snapshot) { ui_set_statusbar(TRUE,"%s",insight_tr(error ? error->message:"Project report snapshot unavailable."));g_clear_error(&error);return; }
    command_busy=TRUE;ReportView *view=g_new0(ReportView,1);view->snapshot=snapshot;view->file_list=action!=0;view->available=g_ptr_array_new_with_free_func(g_free);view->kinds=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
    GHashTable *paths=g_hash_table_new(g_str_hash,g_str_equal);for(guint i=0;i<snapshot->paths->len;i++) g_hash_table_add(paths,g_ptr_array_index(snapshot->paths,i));
    for(gsize i=0;!view->file_list && i<insight_index_count(snapshot->index);i++) { const InsightSymbol *symbol=insight_index_at(snapshot->index,i);if(g_hash_table_contains(paths,symbol->path) && !g_hash_table_contains(view->kinds,symbol->kind)) { if(view->available->len>=128) { ui_set_statusbar(TRUE,"%s",insight_tr("Project report exceeds 128 symbol types."));g_hash_table_unref(paths);view_free(view);command_busy=FALSE;return; }g_hash_table_add(view->kinds,g_strdup(symbol->kind));g_ptr_array_add(view->available,g_strdup(symbol->kind)); } }
    g_hash_table_unref(paths);g_ptr_array_sort(view->available,string_order);
    view->dialog=gtk_dialog_new_with_buttons(insight_tr(view->file_list ? "Export Project File List":"Project Report"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Close"),GTK_RESPONSE_CLOSE,insight_tr("Generate"),201,insight_tr("Cancel generation"),204,insight_tr("Copy Report"),202,insight_tr("Open Report"),203,insight_tr("Save…"),GTK_RESPONSE_ACCEPT,NULL);
    gtk_widget_set_name(view->dialog,"insight-project-report-dialog");gtk_window_set_default_size(GTK_WINDOW(view->dialog),900,580);gtk_dialog_set_default_response(GTK_DIALOG(view->dialog),GTK_RESPONSE_CLOSE);
    g_object_ref_sink(view->dialog);g_object_set_data_full(G_OBJECT(view->dialog),STATE,view,view_free);g_signal_connect(view->dialog,"destroy",G_CALLBACK(destroyed),view);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(view->dialog));insight_panel_help_attach(box,"project-report.md");GtkWidget *label=gtk_label_new(snapshot->project);g_object_set_data(G_OBJECT(label),"insight-no-i18n",GINT_TO_POINTER(1));gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_MIDDLE);gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,6);
    GtkWidget *options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_pack_start(GTK_BOX(box),options,FALSE,FALSE,4);
    view->include=gtk_check_button_new_with_label(insight_tr("Include Symbols"));view->lines=gtk_check_button_new_with_label(insight_tr("Line Numbers"));view->relative=gtk_check_button_new_with_label(insight_tr("Relative file paths"));view->sort=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view->sort),insight_tr("Occurrence"));gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view->sort),insight_tr("Name"));gtk_combo_box_set_active(GTK_COMBO_BOX(view->sort),0);
    GtkWidget *checks[]={view->include,view->lines,view->relative};const gchar *names[]={"insight-report-symbols","insight-report-lines","insight-report-relative"};for(guint i=0;i<3;i++) { gtk_widget_set_name(checks[i],names[i]);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checks[i]),TRUE);gtk_box_pack_start(GTK_BOX(options),checks[i],FALSE,FALSE,0);g_signal_connect(checks[i],"toggled",G_CALLBACK(dirty),view); }
    gtk_widget_set_name(view->sort,"insight-report-sort");gtk_box_pack_start(GTK_BOX(options),gtk_label_new(insight_tr("Sort by")),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(options),view->sort,FALSE,FALSE,0);g_signal_connect(view->sort,"changed",G_CALLBACK(dirty),view);
    view->types=gtk_button_new_with_label(insight_tr("Symbol Types…"));gtk_widget_set_name(view->types,"insight-report-types");gtk_box_pack_start(GTK_BOX(options),view->types,FALSE,FALSE,0);g_signal_connect(view->types,"clicked",G_CALLBACK(types_dialog),view);
    view->preview=gtk_text_view_new();gtk_widget_set_name(view->preview,"insight-report-preview");gtk_text_view_set_editable(GTK_TEXT_VIEW(view->preview),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view->preview),TRUE);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view->preview);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
    view->status=gtk_label_new("");gtk_widget_set_name(view->status,"insight-report-status");gtk_label_set_line_wrap(GTK_LABEL(view->status),TRUE);gtk_box_pack_start(GTK_BOX(box),view->status,FALSE,FALSE,8);gtk_widget_show_all(view->dialog);generate(view);
    for(;;) {
        gint response=gtk_dialog_run(GTK_DIALOG(view->dialog));
        if(response==201) { generate(view);continue; }
        if(response==204) { if(view->cancel) g_cancellable_cancel(view->cancel);continue; }
        if((response==202 || response==203 || response==GTK_RESPONSE_ACCEPT) && (!view->text || view->busy)) continue;
        if(response==202) { gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),view->text,-1);continue; }
        if(response==GTK_RESPONSE_ACCEPT) { save_report(view);continue; }
        if(response==203) {
            gchar *name=g_strdup_printf("%s — %s (%" G_GUINT64_FORMAT ").%s",insight_tr("Project Report"),snapshot->project,++report_serial,view->file_list ? "txt":"RPT");
            /* Keep this a detached unsaved tab even when a project name includes separators. */
            for(gchar *p=name;*p;p++) if(strchr("/\\:\r\n",*p)) *p='_';document_new_file(name,NULL,view->text);g_free(name);break;
        }
        break;
    }
    GtkWidget *dialog=view->dialog;gtk_widget_destroy(dialog);g_object_unref(dialog);command_busy=FALSE;
}
static void activate(GtkMenuItem *item,gpointer data) { (void)item;insight_project_report_command(GPOINTER_TO_UINT(data)); }
void insight_project_report_init(GtkWidget *menu,InsightReportCapture capture)
{
    provider=capture;if(initialized) return;initialized=TRUE;
    const gchar *labels[]={"Project Report…","Export Project File List…"};const gchar *names[]={"insight-project-report","insight-export-project-files"};const guint keys[]={GEANY_KEYS_INSIGHT_PROJECTREPORT,GEANY_KEYS_INSIGHT_EXPORTPROJECTFILES};
    for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,names[i]);g_signal_connect(item,"activate",G_CALLBACK(activate),GUINT_TO_POINTER(i));keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,keys[i],item);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item); }
}
