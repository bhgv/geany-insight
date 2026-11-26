/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared paste editing tools, split by responsibility without changing their operations.
 * RU: Общие инструменты правки, разделённые по ответственности без изменения операций. */
/* EN: Revalidate the captured request against document identity, edits and selection. request: captured
* destination; selection: selection requirement.
* RU: Повторно проверяет request по документу, правкам и выделению. request: зафиксированное назначение;
* selection: требование выделения. */
static gboolean file_request_valid(Request *request,gboolean selection)
{
    GeanyDocument *doc=document_find_by_id(request->id);
    return doc && doc==document_get_current() && doc->editor->sci==request->sci && state(request->sci)->generation==request->generation &&
    (!selection || (SEND(request->sci,SCI_GETSELECTIONS,0,0)==1 && sci_get_selection_mode(request->sci)==SC_SEL_STREAM &&
    !SEND(request->sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) && !SEND(request->sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0) &&
    SEND(request->sci,SCI_GETANCHOR,0,0)==request->anchor && sci_get_current_position(request->sci)==request->caret));
}
/* EN: Return whether path is already open in an editor buffer. path: filename.
* RU: Возвращает наличие уже открытого буфера path. path: имя файла. */
static gboolean destination_open(const gchar *path)
{
    gchar *utf8=g_filename_to_utf8(path,-1,NULL,NULL,NULL);
    if(!utf8) return TRUE;
    gboolean opened=document_find_by_filename(utf8)!=NULL;
    g_free(utf8);
    if(opened) return TRUE;
    GFile *destination=g_file_new_for_path(path);
    GFileInfo *target=g_file_query_info(destination,G_FILE_ATTRIBUTE_ID_FILE,G_FILE_QUERY_INFO_NONE,NULL,NULL);
    g_object_unref(destination);
    const gchar *id=target ? g_file_info_get_attribute_string(target,G_FILE_ATTRIBUTE_ID_FILE) : NULL;
    for(guint i=0;id && documents_array && i<documents_array->len && !opened;i++) {
        GeanyDocument *doc=g_ptr_array_index(documents_array,i);
        if(!doc->is_valid || !doc->file_name) continue;
        gchar *native=g_filename_from_utf8(doc->file_name,-1,NULL,NULL,NULL);
        if(!native) continue;
        GFile *file=g_file_new_for_path(native);
        GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_ID_FILE,G_FILE_QUERY_INFO_NONE,NULL,NULL);
        g_free(native);
        g_object_unref(file);
        if(info) {
            opened=!g_strcmp0(id,g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE));
            g_object_unref(info);
        }
    }
    g_clear_object(&target);
    return opened;
}
/* EN: Choose and insert bounded UTF-8 file text into an unchanged captured destination. item: source menu
* item; data: callback context or encoded action.
* RU: Выбирает и вставляет ограниченный текст UTF-8 в неизменное назначение. item: пункт меню; data:
* контекст либо код команды. */
static void insert_file(GtkMenuItem *item,gpointer data)
{
    (void)item;
    (void)data;
    Request request={
        0
    };
    if(!tools_capture(&request)) return;
    GtkWidget *dialog=gtk_file_chooser_dialog_new(insight_tr("Insert File"),GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_OPEN,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Insert"),GTK_RESPONSE_ACCEPT,NULL);
    g_object_ref_sink(dialog);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),TRUE);
    gtk_widget_set_name(dialog,"insight-insert-file-dialog");
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog),TRUE);
    gchar *path=NULL;
    if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    if(path) {
        GError *error=NULL;
        gsize length=0;
        gchar *text=insight_text_file_read(path,&length,&error);
        if(!text) ui_set_statusbar(TRUE,"%s",error ? insight_tr(error->message) : insight_tr("Choose a local filename."));
        else if(!tools_insert(&request,text)) ui_set_statusbar(TRUE,"Insertion cancelled because the document or selection changed");
        g_clear_error(&error);
        g_free(text);
        g_free(path);
    }
}
/* EN: Select explicit overwrite confirmation for file export. chooser: file chooser; data: callback
* context or encoded action.
* RU: Выбирает явное подтверждение перезаписи экспорта. chooser: выбор файла; data: контекст либо код
* команды. */
static GtkFileChooserConfirmation export_confirmation(GtkFileChooser *chooser,gpointer data) {
    (void)data;
    gchar *path=gtk_file_chooser_get_filename(chooser);
    gboolean open=path&&destination_open(path);
    g_free(path);
    return open?GTK_FILE_CHOOSER_CONFIRMATION_ACCEPT_FILENAME:GTK_FILE_CHOOSER_CONFIRMATION_CONFIRM;
}
/* EN: Open the guarded transfer workflow for text buffers. item: source menu item; data: callback context
* or encoded action.
* RU: Открывает защищённую передачу текста между буферами. item: пункт меню; data: контекст либо код
* команды. */
static void transfer_text(GtkMenuItem *item,gpointer data) {
    (void)item;
    (void)data;
    insight_buffer_transfer_command();
}
/* EN: Export the captured text or selection without overwriting open buffers. item: source menu item;
* data: callback context or encoded action.
* RU: Экспортирует текст либо выделение без перезаписи открытых буферов. item: пункт меню; data: контекст
* либо код команды. */
static void export_text(GtkMenuItem *item,gpointer data)
{
    (void)item;
    gboolean selection=GPOINTER_TO_INT(data);
    GeanyDocument *doc=document_get_current();
    if(!doc) return;
    ScintillaObject *sci=doc->editor->sci;
    gint begin=selection ? sci_get_selection_start(sci) : 0,end=selection ? sci_get_selection_end(sci) : sci_get_length(sci);
    if(selection && (begin==end || SEND(sci,SCI_GETSELECTIONS,0,0)!=1 || sci_get_selection_mode(sci)!=SC_SEL_STREAM || SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) || SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0))) return;
    if(end-begin>INSIGHT_TEXT_TRANSFER_LIMIT) {
        ui_set_statusbar(TRUE,"Text transfer is limited to 8 MiB.");
        return;
    }
    gchar *text=sci_get_contents_range(sci,begin,end);
    gsize length=end-begin;
    if(strlen(text)!=length || !g_utf8_validate(text,length,NULL)) {
        ui_set_statusbar(TRUE,"Text transfer requires UTF-8 without NUL bytes.");
        g_free(text);
        return;
    }
    Request request={
        0
    };
    request.id=doc->id;
    request.sci=sci;
    request.generation=state(sci)->generation;
    request.anchor=SEND(sci,SCI_GETANCHOR,0,0);
    request.caret=sci_get_current_position(sci);
    GtkWidget *dialog=gtk_file_chooser_dialog_new(insight_tr(selection ? "Save Selection" : "Save a Copy"),GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_SAVE,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Save"),GTK_RESPONSE_ACCEPT,NULL);
    g_object_ref_sink(dialog);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),TRUE);
    gtk_widget_set_name(dialog,"insight-export-text-dialog");
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog),TRUE);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),selection ? "selection.txt" : "copy.txt");
    if(selection) g_signal_connect(dialog,"confirm-overwrite",G_CALLBACK(export_confirmation),NULL);
    GtkWidget *note=gtk_label_new(insight_tr(selection ? "Export UTF-8 text. Open destinations use buffer review without saving to disk." : "Export UTF-8 text without changing the original document. Open destinations are protected."));
    gtk_label_set_line_wrap(GTK_LABEL(note),TRUE);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog),note);
    gtk_widget_show(note);
    gchar *path=NULL;
    if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    if(path) {
        GError *error=NULL;
        gboolean saved=FALSE;
        if(!file_request_valid(&request,selection)) ui_set_statusbar(TRUE,"Export cancelled because the source document or selection changed");
        else if(destination_open(path)) {
            if(selection) insight_buffer_transfer_to_path(path);
            else ui_set_statusbar(TRUE,"Close the destination document before exporting to this path");
        }
        else if(!(saved=insight_text_file_write(path,text,length,&error))) ui_set_statusbar(TRUE,"%s",error ? insight_tr(error->message) : insight_tr("Choose a local filename."));
        if(saved) document_open_file(path,FALSE,NULL,"UTF-8");
        g_clear_error(&error);
        g_free(path);
    }
    g_free(text);
}
