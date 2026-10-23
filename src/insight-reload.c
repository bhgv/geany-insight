/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-reload.h"
#include "insight-snapshot-compare.h"
#include "insight-i18n.h"
#include "document.h"
#include "documentprivate.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "../insight/desktop/text_transfer.h"
#include <gio/gio.h>
#include <string.h>

#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define FILE_LIMIT (8u*1024u*1024u)
#define SCAN_LIMIT (64u*1024u*1024u)
enum { PICK, INDEX, PATH, STATE, COLUMNS };
enum { REFRESH=1, CLEAN, NONE, OPEN, COMPARE, COPY, RELOAD };
typedef struct {
    guint id;
    gchar *name,*stamp,*hash,*encoding;
    gboolean eligible,dirty,readonly,bom,sci_readonly;
    gint length,undo,total;
} ReloadRow;
typedef struct {
    GtkWidget *dialog,*tree,*status;
    GtkListStore *model;
    GPtrArray *rows;
    gboolean destroyed;
} ReloadView;
static gboolean initialized,busy;

static void row_free(gpointer data)
{
    ReloadRow *row=data;
    g_free(row->name);g_free(row->stamp);g_free(row->hash);g_free(row->encoding);g_free(row);
}
/* Include file identity and sub-second time where available. No contents are read. */
static gchar *disk_stamp(const gchar *name,guint64 *mtime,GError **error)
{
    gchar *path=g_filename_from_utf8(name,-1,NULL,NULL,error);
    if(!path) return NULL;
    GFile *file=g_file_new_for_path(path);g_free(path);
    GFileInfo *info=g_file_query_info(file,"standard::type,standard::size,time::modified,time::modified-usec,etag::value,id::file",G_FILE_QUERY_INFO_NONE,NULL,error);
    g_object_unref(file);
    if(!info) return NULL;
    gchar *stamp=NULL;
    if(g_file_info_get_file_type(info)!=G_FILE_TYPE_REGULAR || g_file_info_get_size(info)>FILE_LIMIT) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"Requires a regular local file of at most 8 MiB");
    } else {
        *mtime=g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_TIME_MODIFIED);
        const gchar *etag=g_file_info_get_etag(info),*identity=g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE);
        stamp=g_strdup_printf("%"G_GUINT64_FORMAT":%u:%"G_GOFFSET_FORMAT":%s:%s",*mtime,
            g_file_info_get_attribute_uint32(info,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC),g_file_info_get_size(info),etag ? etag:"",identity ? identity:"");
    }
    g_object_unref(info);return stamp;
}
static gchar *buffer_hash(GeanyDocument *doc)
{
    gint length=sci_get_length(doc->editor->sci);
    if(length<0 || (guint)length>FILE_LIMIT) return NULL;
    gchar *text=sci_get_contents(doc->editor->sci,-1);
    gchar *hash=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)text,length);
    g_free(text);return hash;
}
static void destroyed(GtkWidget *widget,gpointer data)
{ (void)widget;((ReloadView *)data)->destroyed=TRUE; }
static void status(ReloadView *view,const gchar *message)
{ if(!view->destroyed) gtk_label_set_text(GTK_LABEL(view->status),insight_tr(message)); }
static void refresh(ReloadView *view)
{
    gtk_list_store_clear(view->model);g_ptr_array_set_size(view->rows,0);
    guint scanned=0,modified=0,omitted=0;gsize bytes=0;
    for(guint i=0;documents_array && i<documents_array->len;i++) {
        GeanyDocument *doc=g_ptr_array_index(documents_array,i);
        if(!DOC_VALID(doc) || !doc->file_name) continue;
        if(scanned>=128) { omitted++;continue; }
        scanned++;
        ReloadRow *row=g_new0(ReloadRow,1);row->id=doc->id;row->name=g_strdup(doc->file_name);
        const gchar *state="Unchanged on disk";GError *error=NULL;guint64 mtime=0;
        if(doc->priv->is_remote || !g_path_is_absolute(doc->file_name)) state="Not a local saved file";
        else {
            row->stamp=disk_stamp(row->name,&mtime,&error);
            if(!row->stamp) state=error ? error->message:"Cannot inspect disk file";
            else if(mtime!=(guint64)doc->priv->mtime || doc->priv->file_disk_status==FILE_CHANGED) {
                row->length=sci_get_length(doc->editor->sci);
                if(row->length<0 || (guint)row->length>FILE_LIMIT || bytes+(guint)row->length>SCAN_LIMIT) state="Buffer snapshot limit reached";
                else {
                    bytes+=row->length;row->hash=buffer_hash(doc);row->eligible=row->hash!=NULL;
                    row->sci_readonly=SEND(doc->editor->sci,SCI_GETREADONLY,0,0);row->dirty=doc->changed;row->readonly=doc->readonly;row->bom=doc->has_bom;row->encoding=g_strdup(doc->encoding);
                    row->undo=SEND(doc->editor->sci,SCI_GETUNDOCURRENT,0,0);row->total=SEND(doc->editor->sci,SCI_GETUNDOACTIONS,0,0);
                    state=row->dirty ? "Changed on disk — unsaved buffer edits":"Changed on disk";
                    modified++;
                }
            }
        }
        GtkTreeIter iter;gtk_list_store_append(view->model,&iter);
        gtk_list_store_set(view->model,&iter,PICK,row->eligible && !row->dirty,INDEX,view->rows->len,PATH,row->name,STATE,insight_tr(state),-1);
        g_ptr_array_add(view->rows,row);g_clear_error(&error);
    }
    gchar *message=g_strdup_printf(insight_tr("%u checked • %u changed • %u omitted (128-file limit)"),scanned,modified,omitted);
    status(view,message);g_free(message);
}
static ReloadRow *selected(ReloadView *view)
{
    GtkTreeModel *model;GtkTreeIter iter;guint index;
    if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree)),&model,&iter)) return NULL;
    gtk_tree_model_get(model,&iter,INDEX,&index,-1);
    return index<view->rows->len ? g_ptr_array_index(view->rows,index):NULL;
}
static void toggle(GtkCellRendererToggle *renderer,gchar *path,gpointer data)
{
    (void)renderer;ReloadView *view=data;GtkTreeIter iter;guint index;gboolean pick;
    if(!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(view->model),&iter,path)) return;
    gtk_tree_model_get(GTK_TREE_MODEL(view->model),&iter,INDEX,&index,PICK,&pick,-1);
    ReloadRow *row=g_ptr_array_index(view->rows,index);
    if(row->eligible) gtk_list_store_set(view->model,&iter,PICK,!pick,-1);
}
static void select_clean(ReloadView *view,gboolean clean)
{
    GtkTreeIter iter;
    for(gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(view->model),&iter);valid;valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(view->model),&iter)) {
        guint index;gtk_tree_model_get(GTK_TREE_MODEL(view->model),&iter,INDEX,&index,-1);
        ReloadRow *row=g_ptr_array_index(view->rows,index);
        gtk_list_store_set(view->model,&iter,PICK,clean && row->eligible && !row->dirty,-1);
    }
}
static GeanyDocument *validate(ReloadRow *row)
{
    GeanyDocument *doc=document_find_by_id(row->id);
    if(!row->eligible || !DOC_VALID(doc) || g_strcmp0(doc->file_name,row->name) ||
        SEND(doc->editor->sci,SCI_GETREADONLY,0,0)!=row->sci_readonly || doc->changed!=row->dirty || doc->readonly!=row->readonly || doc->has_bom!=row->bom ||
        g_strcmp0(doc->encoding,row->encoding) || sci_get_length(doc->editor->sci)!=row->length ||
        SEND(doc->editor->sci,SCI_GETUNDOCURRENT,0,0)!=row->undo ||
        SEND(doc->editor->sci,SCI_GETUNDOACTIONS,0,0)!=row->total || SEND(doc->editor->sci,SCI_GETUNDOSEQUENCE,0,0)) return NULL;
    gchar *hash=buffer_hash(doc);gboolean same=!g_strcmp0(hash,row->hash);g_free(hash);
    guint64 mtime=0;gchar *stamp=disk_stamp(row->name,&mtime,NULL);
    same= same && stamp && !g_strcmp0(stamp,row->stamp);g_free(stamp);
    return same ? doc:NULL;
}
static void compare(ReloadView *view)
{
    ReloadRow *row=selected(view);GeanyDocument *doc=row ? validate(row):NULL;
    if(!doc) { status(view,"Selection changed or unavailable; Refresh the list");return; }
    gchar *path=g_filename_from_utf8(row->name,-1,NULL,NULL,NULL);gsize size=0;
    gchar *disk=path ? insight_text_file_read(path,&size,NULL):NULL;g_free(path);
    gchar *buffer=sci_get_contents(doc->editor->sci,-1);
    if(!disk || !validate(row) || !insight_snapshot_compare_show(view->dialog,"Disk snapshot (UTF-8)",disk,size,row->name,buffer,row->length))
        status(view,"Comparison requires UTF-8 snapshots of at most 1 MiB; Refresh if the file changed");
    g_free(disk);g_free(buffer);
}
static void reload_selected(ReloadView *view)
{
    GPtrArray *picked=g_ptr_array_new();GtkTreeIter iter;guint dirty=0;
    for(gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(view->model),&iter);valid;valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(view->model),&iter)) {
        gboolean pick;guint index;gtk_tree_model_get(GTK_TREE_MODEL(view->model),&iter,PICK,&pick,INDEX,&index,-1);
        if(pick) { ReloadRow *row=g_ptr_array_index(view->rows,index);g_ptr_array_add(picked,row);if(row->dirty) dirty++; }
    }
    if(!picked->len) { status(view,"Choose files to reload first");g_ptr_array_unref(picked);return; }
    GtkWidget *question=gtk_message_dialog_new(GTK_WINDOW(view->dialog),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,
        insight_tr("Reload %u selected files from disk?"),picked->len);
    gtk_widget_set_name(question,"insight-reload-confirm");gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(question),
        insight_tr("%u files contain unsaved edits. Reload replaces buffer contents. Undo history follows the native reload preference. No disk files are written."),dirty);
    gtk_dialog_add_buttons(GTK_DIALOG(question),insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Reload"),GTK_RESPONSE_ACCEPT,NULL);
    gboolean gone=FALSE;g_object_ref_sink(question);
    g_signal_connect_swapped(question,"destroy",G_CALLBACK(g_nullify_pointer),&question);
    /* Keep a separate owner reference because the parent can be destroyed in run(). */
    GtkWidget *owner=question;gint answer=gtk_dialog_run(GTK_DIALOG(owner));gone=question==NULL;
    if(!gone) gtk_widget_destroy(owner);g_object_unref(owner);
    if(view->destroyed || answer!=GTK_RESPONSE_ACCEPT) { g_ptr_array_unref(picked);return; }
    guint done=0;
    /* Validate the whole batch before changing its first member. */
    for(guint i=0;i<picked->len;i++) if(!validate(g_ptr_array_index(picked,i))) goto stale;
    for(guint i=0;i<picked->len;i++) {
        ReloadRow *row=g_ptr_array_index(picked,i);GeanyDocument *doc=validate(row);
        if(!doc || !document_reload_force(doc,row->encoding)) goto stale;
        done++;
        if(view->destroyed) { g_ptr_array_unref(picked);return; }
    }
    refresh(view);status(view,"Selected files reloaded");g_ptr_array_unref(picked);return;
stale:
    refresh(view);
    gchar *message=g_strdup_printf(insight_tr("Reload stopped after %u files: a buffer or disk file changed. Review the refreshed list."),done);
    status(view,message);g_free(message);g_ptr_array_unref(picked);
}
void insight_reload_command(void)
{
    if(busy) return;busy=TRUE;
    ReloadView view={0};view.rows=g_ptr_array_new_with_free_func(row_free);
    view.dialog=gtk_dialog_new_with_buttons(insight_tr("Reload Modified Files"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
        insight_tr("Refresh"),REFRESH,insight_tr("Select Clean"),CLEAN,insight_tr("Select None"),NONE,insight_tr("Open File"),OPEN,insight_tr("Compare with Disk"),COMPARE,insight_tr("Copy Paths"),COPY,insight_tr("Reload Selected…"),RELOAD,insight_tr("Close"),GTK_RESPONSE_CLOSE,NULL);
    g_object_ref_sink(view.dialog);gtk_widget_set_name(view.dialog,"insight-reload-modified");gtk_window_set_default_size(GTK_WINDOW(view.dialog),920,440);
    g_signal_connect(view.dialog,"destroy",G_CALLBACK(destroyed),&view);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(view.dialog));gtk_container_set_border_width(GTK_CONTAINER(box),8);
    view.model=gtk_list_store_new(COLUMNS,G_TYPE_BOOLEAN,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING);
    view.tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(view.model));gtk_widget_set_name(view.tree,"insight-reload-files");
    GtkCellRenderer *check=gtk_cell_renderer_toggle_new();g_signal_connect(check,"toggled",G_CALLBACK(toggle),&view);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view.tree),gtk_tree_view_column_new_with_attributes(insight_tr("Reload"),check,"active",PICK,NULL));
    const gchar *labels[]={"File","State"};
    for(guint i=0;i<2;i++) { GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(insight_tr(labels[i]),gtk_cell_renderer_text_new(),"text",PATH+i,NULL);gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_append_column(GTK_TREE_VIEW(view.tree),column); }
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view.tree);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
    view.status=gtk_label_new("");gtk_widget_set_name(view.status,"insight-reload-status");gtk_label_set_selectable(GTK_LABEL(view.status),TRUE);gtk_label_set_line_wrap(GTK_LABEL(view.status),TRUE);gtk_box_pack_start(GTK_BOX(box),view.status,FALSE,FALSE,6);
    refresh(&view);gtk_widget_show_all(view.dialog);
    while(!view.destroyed) {
        gint response=gtk_dialog_run(GTK_DIALOG(view.dialog));if(view.destroyed) break;
        if(response==REFRESH) refresh(&view);
        else if(response==CLEAN || response==NONE) select_clean(&view,response==CLEAN);
        else if(response==RELOAD) reload_selected(&view);
        else if(response==COMPARE) compare(&view);
        else if(response==OPEN) { ReloadRow *row=selected(&view);GeanyDocument *doc=row ? document_find_by_id(row->id):NULL;if(DOC_VALID(doc) && !g_strcmp0(doc->file_name,row->name)) { document_show_tab(doc);break; }else status(&view,"Selection changed or unavailable; Refresh the list"); }
        else if(response==COPY) {
            GString *text=g_string_new("");GtkTreeIter iter;
            for(gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(view.model),&iter);valid;valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(view.model),&iter)) {
                gboolean pick;gchar *path;gtk_tree_model_get(GTK_TREE_MODEL(view.model),&iter,PICK,&pick,PATH,&path,-1);if(pick) { g_string_append(text,path);g_string_append_c(text,'\n'); }g_free(path);
            }
            gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text->str,text->len);g_string_free(text,TRUE);
        } else break;
    }
    if(!view.destroyed) gtk_widget_destroy(view.dialog);
    g_object_unref(view.dialog);g_object_unref(view.model);g_ptr_array_unref(view.rows);busy=FALSE;
}
static void activate(GtkMenuItem *item,gpointer data)
{ (void)item;(void)data;insight_reload_command(); }
void insight_reload_init(void)
{
    if(initialized) return;initialized=TRUE;
    GtkWidget *menu=ui_lookup_widget(main_widgets.window,"file1_menu"),*item=gtk_menu_item_new_with_label("Reload Modified Files…");
    gtk_widget_set_name(item,"insight-reload-modified-command");g_signal_connect(item,"activate",G_CALLBACK(activate),NULL);
    keybindings_attach_menu(GEANY_KEY_GROUP_DOCUMENT,GEANY_KEYS_INSIGHT_RELOADMODIFIED,item);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
}
