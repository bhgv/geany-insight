/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-backups.h"
#include "insight-snapshot-compare.h"
#include "app.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include "../insight/desktop/backup_store.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
typedef struct { GtkWidget *dialog,*tree,*preview,*status;GtkListStore *model;GPtrArray *entries;gchar *directory,*source;GeanyFiletype *type;guint source_id; } Browser;
static gboolean initialized,busy;
static guint64 view_serial;
static void status_error(const GError *error)
{ ui_set_statusbar(TRUE,"%s",insight_tr(error ? error->message : "Backup operation failed.")); }
static gchar *directory_for(GeanyDocument *doc,GError **error)
{
    if(!doc || !doc->is_valid || !doc->file_name || !g_path_is_absolute(doc->file_name)) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Save the file with a name before using backup versions.");return NULL;
    }
    gchar *source=g_filename_from_utf8(doc->file_name,-1,NULL,NULL,error);if(!source) return NULL;
    gchar *directory=insight_backup_directory(app->configdir,source,error);g_free(source);return directory;
}
static void save_backup(void)
{
    GeanyDocument *doc=document_get_current();GError *error=NULL;gchar *directory=directory_for(doc,&error);
    if(!directory) { status_error(error);g_clear_error(&error);return; }
    ScintillaObject *sci=doc->editor->sci;gint length=sci_get_length(sci);gchar *text=NULL,*saved=NULL;
    if(length<0 || (guint)length>INSIGHT_BACKUP_TEXT_LIMIT) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Backup snapshots are limited to 8 MiB.");
    else {
        text=sci_get_contents(sci,-1);
        insight_backup_save(directory,text,length,&saved,&error);
    }
    if(error) status_error(error);
    else ui_set_statusbar(TRUE,"%s",insight_tr("New backup saved. The source buffer, disk file and Undo history are unchanged."));
    g_clear_error(&error);g_free(saved);g_free(text);g_free(directory);
}
static InsightBackupEntry *selected(Browser *browser)
{
    GtkTreeIter iter;GtkTreeModel *model=NULL;guint index;
    if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)),&model,&iter)) return NULL;
    gtk_tree_model_get(model,&iter,0,&index,-1);
    return browser->entries && index<browser->entries->len ? g_ptr_array_index(browser->entries,index):NULL;
}
static gchar *read_selected(Browser *browser,gsize *length)
{
    InsightBackupEntry *entry=selected(browser);if(!entry) return NULL;
    GError *error=NULL;gchar *text=insight_backup_read(browser->directory,entry,length,&error);
    if(error) { gtk_label_set_text(GTK_LABEL(browser->status),insight_tr(error->message));g_clear_error(&error); }
    return text;
}
static void selection_changed(GtkTreeSelection *selection,gpointer data)
{
    (void)selection;Browser *browser=data;gsize length=0;gchar *text=read_selected(browser,&length);
    GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(browser->preview));gtk_text_buffer_set_text(buffer,"",0);
    gboolean valid=text!=NULL;
    gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(browser->dialog),GTK_RESPONSE_ACCEPT),valid);
    gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(browser->dialog),202),valid);
    gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(browser->dialog),204),valid);
    if(text) {
        gsize shown=MIN(length,16384u);while(shown<length && shown && ((guchar)text[shown]&0xc0)==0x80) shown--;
        /* GTK preview normalizes EOL; the retained file and opened tab stay exact. */
        GString *preview=g_string_sized_new(shown);
        for(gsize i=0;i<shown;i++) { if(text[i]=='\r') { g_string_append_c(preview,'\n');if(i+1<shown && text[i+1]=='\n') i++; }else g_string_append_c(preview,text[i]); }
        gtk_text_buffer_set_text(buffer,preview->str,preview->len);g_string_free(preview,TRUE);
        gtk_label_set_text(GTK_LABEL(browser->status),insight_tr(length>shown ? "Preview shows the first 16 KiB. Open or copy uses the complete snapshot." : "Read-only UTF-8 snapshot. Opening does not replace the source file."));g_free(text);
    }
}
static void refresh(Browser *browser)
{
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)));gtk_list_store_clear(browser->model);
    selection_changed(NULL,browser);
    if(browser->entries) g_ptr_array_free(browser->entries,TRUE);browser->entries=NULL;
    GError *error=NULL;browser->entries=insight_backup_list(browser->directory,&error);
    if(error) { gtk_label_set_text(GTK_LABEL(browser->status),insight_tr(error->message));g_clear_error(&error);return; }
    for(guint i=0;i<browser->entries->len;i++) {
        InsightBackupEntry *entry=g_ptr_array_index(browser->entries,i);GtkTreeIter iter;
        gchar *date=g_strdup_printf("%.4s-%.2s-%.2s %.2s:%.2s:%.2s UTC",entry->name,entry->name+4,entry->name+6,entry->name+9,entry->name+11,entry->name+13);
        gtk_list_store_append(browser->model,&iter);gtk_list_store_set(browser->model,&iter,0,i,1,date,2,(guint64)entry->size,3,entry->name,-1);g_free(date);
    }
    if(browser->entries->len) { GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(browser->tree),path,NULL,FALSE);gtk_tree_path_free(path); }
    else gtk_label_set_text(GTK_LABEL(browser->status),insight_tr("No backup versions for this file. Use Save New Backup File first."));
}
static void show_folder(const gchar *directory)
{
    GError *error=NULL;
    GFile *file=g_file_new_for_path(directory);gchar *uri=g_file_get_uri(file);
    if(!g_file_query_exists(file,NULL)) ui_set_statusbar(TRUE,"%s",insight_tr("No backup folder yet. Save a backup first."));
    else if(!g_app_info_launch_default_for_uri(uri,NULL,&error)) status_error(error);
    g_clear_error(&error);g_free(uri);g_object_unref(file);
}
static void open_view(Browser *browser,const gchar *text)
{
    gchar *base=g_path_get_basename(browser->source),*name=g_strdup_printf("%s — %s (%" G_GUINT64_FORMAT ").txt",insight_tr("Backup Contents"),base,++view_serial);g_free(base);
    GeanyDocument *view=document_new_file(name,browser->type,NULL);g_free(name);if(!view) return;
    ScintillaObject *sci=view->editor->sci;SEND(sci,SCI_SETUNDOCOLLECTION,FALSE,0);sci_set_text(sci,text);SEND(sci,SCI_SETUNDOCOLLECTION,TRUE,0);
    SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);view->readonly=TRUE;SEND(sci,SCI_SETREADONLY,TRUE,0);document_set_text_changed(view,FALSE);ui_document_show_hide(view);
    /* This view is a detached snapshot, never a source for another backup family. */
    g_object_set_data(G_OBJECT(sci),"insight-backup-view",GINT_TO_POINTER(1));
}
static void browse(gboolean compare)
{
    GeanyDocument *source=document_get_current();GError *error=NULL;gchar *directory=directory_for(source,&error);
    if(!directory) { status_error(error);g_clear_error(&error);return; }
    Browser browser={0};browser.directory=directory;browser.source=g_strdup(source->file_name);browser.type=source->file_type;browser.source_id=source->id;
    browser.dialog=gtk_dialog_new_with_buttons(insight_tr(compare ? "Compare with Backup File":"Open Backup File"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
        insight_tr("Close"),GTK_RESPONSE_CLOSE,insight_tr("Refresh"),201,insight_tr("Show Folder"),203,insight_tr("Copy Snapshot"),202,insight_tr("Compare with Current Buffer"),204,insight_tr(compare ? "Compare":"Open Backup"),GTK_RESPONSE_ACCEPT,NULL);
    gtk_widget_set_name(browser.dialog,"insight-backup-dialog");gtk_window_set_default_size(GTK_WINDOW(browser.dialog),900,540);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(browser.dialog));GtkWidget *label=gtk_label_new(browser.source);g_object_set_data(G_OBJECT(label),"insight-no-i18n",GINT_TO_POINTER(1));gtk_label_set_selectable(GTK_LABEL(label),TRUE);gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_MIDDLE);gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,6);
    browser.model=gtk_list_store_new(4,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_UINT64,G_TYPE_STRING);browser.tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(browser.model));gtk_widget_set_name(browser.tree,"insight-backup-versions");
    const gchar *labels[]={"Created (UTC)","Bytes","Version"};
    for(guint i=0;i<3;i++) gtk_tree_view_append_column(GTK_TREE_VIEW(browser.tree),gtk_tree_view_column_new_with_attributes(insight_tr(labels[i]),gtk_cell_renderer_text_new(),"text",i+1,NULL));
    browser.preview=gtk_text_view_new();gtk_widget_set_name(browser.preview,"insight-backup-preview");gtk_text_view_set_editable(GTK_TEXT_VIEW(browser.preview),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(browser.preview),TRUE);
    GtkWidget *top=gtk_scrolled_window_new(NULL,NULL),*bottom=gtk_scrolled_window_new(NULL,NULL),*paned=gtk_paned_new(GTK_ORIENTATION_VERTICAL);gtk_container_add(GTK_CONTAINER(top),browser.tree);gtk_container_add(GTK_CONTAINER(bottom),browser.preview);gtk_paned_pack1(GTK_PANED(paned),top,TRUE,FALSE);gtk_paned_pack2(GTK_PANED(paned),bottom,TRUE,FALSE);gtk_paned_set_position(GTK_PANED(paned),220);gtk_box_pack_start(GTK_BOX(box),paned,TRUE,TRUE,0);
    browser.status=gtk_label_new("");gtk_widget_set_name(browser.status,"insight-backup-status");gtk_label_set_line_wrap(GTK_LABEL(browser.status),TRUE);gtk_box_pack_start(GTK_BOX(box),browser.status,FALSE,FALSE,8);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser.tree)),"changed",G_CALLBACK(selection_changed),&browser);
    gtk_dialog_set_default_response(GTK_DIALOG(browser.dialog),GTK_RESPONSE_CLOSE);gtk_widget_show_all(browser.dialog);refresh(&browser);
    for(;;) {
        gint response=gtk_dialog_run(GTK_DIALOG(browser.dialog));
        if(response==201) { refresh(&browser);continue; }
        if(response==203) { show_folder(browser.directory);continue; }
        if(response!=202 && response!=204 && response!=GTK_RESPONSE_ACCEPT) break;
        gsize length=0;gchar *text=read_selected(&browser,&length);if(!text) { selection_changed(NULL,&browser);continue; }
        if(response==202) { gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text,length);g_free(text);continue; }
        if(response==204 || compare) {
            GeanyDocument *doc=document_find_by_id(browser.source_id);gchar *current=NULL;gint size=-1;
            if(doc && doc->is_valid && !g_strcmp0(doc->file_name,browser.source)) {
                size=sci_get_length(doc->editor->sci);
                if(size>=0 && (guint)size<=INSIGHT_SNAPSHOT_COMPARE_LIMIT) current=sci_get_contents(doc->editor->sci,-1);
            }
            InsightBackupEntry *entry=selected(&browser);
            gchar *label=g_strdup_printf("%s — %s",insight_tr("Backup snapshot"),entry ? entry->name:"");
            gboolean opened=current && insight_snapshot_compare_show(main_widgets.window,label,text,length,insight_tr("Current buffer snapshot"),current,size);
            g_free(label);g_free(current);g_free(text);
            if(opened) break;
            gtk_label_set_text(GTK_LABEL(browser.status),insight_tr("Comparison requires the original source document and valid UTF-8 snapshots up to 1 MiB per side."));continue;
        }
        open_view(&browser,text);g_free(text);break;
    }
    gtk_widget_destroy(browser.dialog);g_object_unref(browser.model);if(browser.entries) g_ptr_array_free(browser.entries,TRUE);g_free(browser.source);g_free(directory);
}
void insight_backups_command(guint action)
{
    if(busy) return;busy=TRUE;
    if(action==0) save_backup();else if(action==1 || action==3) browse(action==3);else if(action==2) {
        GError *error=NULL;gchar *directory=directory_for(document_get_current(),&error);
        if(directory) show_folder(directory);else status_error(error);g_free(directory);g_clear_error(&error);
    }
    busy=FALSE;
}
static void activate(GtkMenuItem *item,gpointer data) { (void)item;insight_backups_command(GPOINTER_TO_UINT(data)); }
void insight_backups_init(void)
{
    if(initialized) return;initialized=TRUE;
    GtkWidget *menu=ui_lookup_widget(main_widgets.window,"file1_menu");
    const gchar *labels[]={"Save New Backup File","Open Backup File…","Show Backup Folder","Compare with Backup File…"};const gchar *names[]={"insight-save-backup","insight-open-backup","insight-backup-folder","insight-compare-backup"};
    const guint keys[]={GEANY_KEYS_INSIGHT_SAVEBACKUP,GEANY_KEYS_INSIGHT_OPENBACKUP,GEANY_KEYS_INSIGHT_BACKUPFOLDER,GEANY_KEYS_INSIGHT_COMPAREBACKUP};
    for(guint i=0;i<4;i++) {
        GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,names[i]);keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,keys[i],item);g_signal_connect(item,"activate",G_CALLBACK(activate),GUINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
    }
    GtkWidget *help=gtk_menu_item_new_with_label("Backup Versions Help");g_signal_connect(help,"activate",G_CALLBACK(insight_help_show),"backup-versions.md");gtk_menu_shell_append(GTK_MENU_SHELL(menu),help);gtk_widget_show(help);
}
