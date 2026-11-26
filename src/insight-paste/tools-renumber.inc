/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared paste editing tools, split by responsibility without changing their operations.
 * RU: Общие инструменты правки, разделённые по ответственности без изменения операций. */
typedef struct {
    GtkWidget *mode,*radix,*value,*preview,*status;
    const gchar *text;
}
RenumberView;
/* EN: Return an owned renumbered preview for view or NULL. view: review state.
* RU: Возвращает собственный перенумерованный предпросмотр view либо NULL. view: состояние просмотра. */
static gchar *renumber_preview(RenumberView *view)
{
    GError *error=NULL;
    guint count=0;
    gchar *result=insight_renumber(view->text,gtk_combo_box_get_active(GTK_COMBO_BOX(view->mode)),gtk_combo_box_get_active(GTK_COMBO_BOX(view->radix)),gtk_entry_get_text(GTK_ENTRY(view->value)),&count,&error);
    gchar *status=result ? insight_i18n_printf("%u integer tokens. Preview shows at most 8000 bytes.",count) : g_strdup(insight_tr(error->message));
    gtk_label_set_text(GTK_LABEL(view->status),status);
    g_free(status);
    g_clear_error(&error);
    GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview));
    gsize length=result ? MIN(strlen(result),8000) : 0;
    while(length && !g_utf8_validate(result,length,NULL)) length--;
    gtk_text_buffer_set_text(buffer,result ? result : "",length);
    return result;
}
/* EN: Refresh renumbering preview and validation status. widget: source widget; data: callback context or
* encoded action.
* RU: Обновляет предпросмотр перенумерации и проверку. widget: виджет события; data: контекст либо код
* команды. */
static void renumber_changed(GtkWidget *widget,gpointer data)
{
    (void)widget;
    RenumberView *view=data;
    gtk_widget_set_sensitive(view->value,gtk_combo_box_get_active(GTK_COMBO_BOX(view->mode))!=0);
    /* Options invalidate the displayed preview; large documents are processed only
    * on explicit Preview or Apply, never on every keystroke. */
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),"",0);
    gtk_label_set_text(GTK_LABEL(view->status),"Press Preview to inspect the result before applying");
}
/* EN: Review bounded integer renumbering and revalidate before applying. item: source menu item; data:
* callback context or encoded action.
* RU: Показывает ограниченную перенумерацию чисел и повторно проверяет перед применением. item: пункт
* меню; data: контекст либо код команды. */
static void renumber_text(GtkMenuItem *item,gpointer data)
{
    if(insight_renumber_ranges_available()) {
        insight_renumber_ranges_command();
        return;
    }
    (void)item;
    (void)data;
    Request request={
        0
    };
    if(!tools_capture(&request)) return;
    gint start=MIN(request.anchor,request.caret),end=MAX(request.anchor,request.caret);
    if(start==end) {
        start=0;
        end=sci_get_length(request.sci);
    }
    if(end-start>8*1024*1024) {
        ui_set_statusbar(TRUE,"Renumber is limited to 8 MiB");
        return;
    }
    gchar *text=sci_get_contents_range(request.sci,start,end);
    GtkWidget *dialog=gtk_dialog_new_with_buttons("Renumber",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Preview",GTK_RESPONSE_HELP,"Apply",GTK_RESPONSE_APPLY,NULL);
    g_object_ref_sink(dialog);
    gtk_widget_set_name(dialog,"insight-renumber-dialog");
    gtk_window_set_default_size(GTK_WINDOW(dialog),720,480);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*scope=gtk_label_new(request.anchor==request.caret ? "Scope: whole document" : "Scope: exact selection");
    RenumberView view={
        gtk_combo_box_text_new(),gtk_combo_box_text_new(),gtk_entry_new(),gtk_text_view_new(),gtk_label_new(""),text
    };
    const gchar *modes[]={
        "Auto: ascend from first number","Start at value","Offset existing numbers"
    },*radices[]={
        "Auto: preserve each number's radix","Decimal","Hexadecimal","Octal"
    };
    for(guint i=0;i<G_N_ELEMENTS(modes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view.mode),modes[i]);
    for(guint i=0;i<G_N_ELEMENTS(radices);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view.radix),radices[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(view.mode),0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(view.radix),0);
    gtk_entry_set_text(GTK_ENTRY(view.value),"1");
    gtk_widget_set_name(view.mode,"insight-renumber-mode");
    gtk_widget_set_name(view.radix,"insight-renumber-radix");
    gtk_widget_set_name(view.value,"insight-renumber-value");
    gtk_widget_set_name(view.preview,"insight-renumber-preview");
    gtk_widget_set_name(view.status,"insight-renumber-status");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view.preview),FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view.preview),TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view.preview),GTK_WRAP_NONE);
    g_object_set_data(G_OBJECT(view.preview),"insight-no-i18n",GINT_TO_POINTER(1));
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
    gtk_container_add(GTK_CONTAINER(scroll),view.preview);
    gtk_label_set_line_wrap(GTK_LABEL(view.status),TRUE);
    GtkWidget *items[]={
        scope,view.mode,view.radix,view.value,view.status
    };
    for(guint i=0;i<G_N_ELEMENTS(items);i++) gtk_box_pack_start(GTK_BOX(box),items[i],FALSE,FALSE,5);
    gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,5);
    g_signal_connect(view.mode,"changed",G_CALLBACK(renumber_changed),&view);
    g_signal_connect(view.radix,"changed",G_CALLBACK(renumber_changed),&view);
    g_signal_connect(view.value,"changed",G_CALLBACK(renumber_changed),&view);
    insight_panel_help_attach(box,"renumber.md");
    renumber_changed(NULL,&view);
    gtk_widget_show_all(dialog);
    gint response;
    while((response=gtk_dialog_run(GTK_DIALOG(dialog)))==GTK_RESPONSE_HELP || response==GTK_RESPONSE_APPLY) {
        gchar *result=renumber_preview(&view);
        if(response==GTK_RESPONSE_APPLY && result) {
            if(!file_request_valid(&request,TRUE) || !tools_capture(&(Request){
                0
            })) ui_set_statusbar(TRUE,"Renumber cancelled because the document or selection changed");
            else if(strcmp(text,result)) {
                insight_completion_cancel(document_get_current()->editor);
                SEND(request.sci,SCI_AUTOCCANCEL,0,0);
                sci_start_undo_action(request.sci);
                SEND(request.sci,SCI_SETTARGETRANGE,start,end);
                SEND(request.sci,SCI_REPLACETARGET,strlen(result),(sptr_t)result);
                /* Keep the transformed range selected, including reverse selections. */
                gint finish=start+strlen(result);
                SEND(request.sci,SCI_SETSEL,request.anchor>request.caret ? finish : start,request.anchor>request.caret ? start : finish);
                sci_end_undo_action(request.sci);
            }
            g_free(result);
            break;
        }
        g_free(result);
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    g_free(text);
}
/* EN: Open the current editor renumbering review.
* RU: Открывает просмотр перенумерации текущего редактора. */
void insight_paste_renumber(void) {
    renumber_text(NULL,NULL);
}
