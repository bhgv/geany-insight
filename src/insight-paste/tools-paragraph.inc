/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared paste editing tools, split by responsibility without changing their operations.
 * RU: Общие инструменты правки, разделённые по ответственности без изменения операций. */
typedef struct {
    GtkWidget *mode,*margin,*preview,*status;
    const gchar *text,*eol;
    gint anchor,caret,tab,start,end;
}
ParagraphView;
/* EN: Return an owned bounded paragraph preview for view or NULL. view: review state.
* RU: Возвращает собственный ограниченный предпросмотр абзаца view либо NULL. view: состояние просмотра. */
static gchar *paragraph_preview(ParagraphView *view)
{
    GError *error=NULL;
    gchar *result=insight_paragraph(view->text,view->anchor,view->caret,gtk_combo_box_get_active(GTK_COMBO_BOX(view->mode))==1,gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(view->margin)),view->tab,view->eol,&view->start,&view->end,&error);
    gchar *status=result ? insight_i18n_printf("Scope: bytes %d–%d. Preview shows at most 8000 bytes.",view->start,view->end) : g_strdup(insight_tr(error->message));
    gtk_label_set_text(GTK_LABEL(view->status),status);
    g_free(status);
    g_clear_error(&error);
    gsize length=result ? MIN(strlen(result),8000) : 0;
    while(length && !g_utf8_validate(result,length,NULL)) length--;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),result ? result : "",length);
    return result;
}
/* EN: Refresh paragraph preview and Apply sensitivity. widget: source widget; data: callback context or
* encoded action.
* RU: Обновляет предпросмотр абзаца и доступность применения. widget: виджет события; data: контекст либо
* код команды. */
static void paragraph_changed(GtkWidget *widget,gpointer data)
{
    (void)widget;
    ParagraphView *view=data;
    gtk_widget_set_sensitive(view->margin,gtk_combo_box_get_active(GTK_COMBO_BOX(view->mode))==0);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),"",0);
    gtk_label_set_text(GTK_LABEL(view->status),"Press Preview to inspect the result before applying");
}
/* EN: Replace source in an unchanged request range with result in one Undo. request: captured destination;
* start: first byte; end: exclusive end byte; source: captured source text; result: output/result.
* RU: Заменяет source в неизменном диапазоне request результатом result одним Undo. request:
* зафиксированное назначение; start: первый байт; end: конечный байт; source: снимок текста; result:
* результат. */
static gboolean paragraph_apply(Request *request,gint start,gint end,const gchar *source,const gchar *result)
{
    if(!file_request_valid(request,TRUE) || !tools_capture(&(Request){
        0
    })) {
        ui_set_statusbar(TRUE,"Paragraph operation cancelled because the document or selection changed");
        return FALSE;
    }
    if(strlen(result)==(gsize)(end-start) && !strncmp(source+start,result,end-start)) return TRUE;
    insight_completion_cancel(document_get_current()->editor);
    SEND(request->sci,SCI_AUTOCCANCEL,0,0);
    sci_start_undo_action(request->sci);
    SEND(request->sci,SCI_SETTARGETRANGE,start,end);
    SEND(request->sci,SCI_REPLACETARGET,strlen(result),(sptr_t)result);
    gint finish=start+strlen(result);
    SEND(request->sci,SCI_SETSEL,request->anchor>request->caret ? finish : start,request->anchor>request->caret ? start : finish);
    sci_end_undo_action(request->sci);
    gtk_widget_grab_focus(GTK_WIDGET(request->sci));
    return TRUE;
}
/* EN: Review paragraph transformations before applying to the captured selection. item: source menu item;
* data: callback context or encoded action.
* RU: Показывает преобразования абзаца до применения к зафиксированному выделению. item: пункт меню; data:
* контекст либо код команды. */
static void paragraph_command(GtkMenuItem *item,gpointer data)
{
    (void)item;
    Request request={
        0
    };
    if(!tools_capture(&request)) return;
    if(sci_get_length(request.sci)>8*1024*1024) {
        ui_set_statusbar(TRUE,"Paragraph tools are limited to 8 MiB");
        return;
    }
    GeanyEditor *editor=document_get_current()->editor;
    const InsightFiletypeOptions *options=insight_languages_editing_options(editor);
    const GeanyEditorPrefs *prefs=editor_get_prefs(editor);
    gint margin=options ? options->margin : editor->line_breaking ? prefs->line_break_column : prefs->long_line_column;
    gint tab=SEND(request.sci,SCI_GETTABWIDTH,0,0);
    gchar *text=sci_get_contents(request.sci,-1),*eol=g_strdup(editor_get_eol_char(editor));
    gint command=GPOINTER_TO_INT(data);
    if(command<2) {
        GError *error=NULL;
        gint start=0,end=0;
        gchar *result=insight_paragraph(text,request.anchor,request.caret,command==1,CLAMP(margin,1,1000),CLAMP(tab,1,32),eol,&start,&end,&error);
        if(result) paragraph_apply(&request,start,end,text,result);
        else ui_set_statusbar(TRUE,"%s",insight_tr(error->message));
        g_clear_error(&error);
        g_free(result);
        g_free(text);
        g_free(eol);
        return;
    }
    GtkWidget *dialog=gtk_dialog_new_with_buttons("Paragraph and lines",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Preview",GTK_RESPONSE_HELP,"Apply",GTK_RESPONSE_APPLY,NULL);
    g_object_ref_sink(dialog);
    gtk_widget_set_name(dialog,"insight-paragraph-dialog");
    gtk_window_set_default_size(GTK_WINDOW(dialog),760,490);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    ParagraphView view={
        gtk_combo_box_text_new(),gtk_spin_button_new_with_range(1,1000,1),gtk_text_view_new(),gtk_label_new(""),text,eol,request.anchor,request.caret,CLAMP(tab,1,32),0,0
    };
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view.mode),"Reform Paragraph");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view.mode),"Join Lines");
    gtk_combo_box_set_active(GTK_COMBO_BOX(view.mode),0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(view.margin),CLAMP(margin,1,1000));
    gtk_widget_set_name(view.mode,"insight-paragraph-mode");
    gtk_widget_set_name(view.margin,"insight-paragraph-margin");
    gtk_widget_set_name(view.preview,"insight-paragraph-preview");
    gtk_widget_set_name(view.status,"insight-paragraph-status");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view.preview),FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view.preview),TRUE);
    g_object_set_data(G_OBJECT(view.preview),"insight-no-i18n",GINT_TO_POINTER(1));
    GtkWidget *label=gtk_label_new("Margin in display columns (temporary; file type settings are unchanged)"),*scope=gtk_label_new("Reform: blank-delimited paragraphs. Join: intersected lines, or current + next line."),*scroll=gtk_scrolled_window_new(NULL,NULL);
    gtk_label_set_line_wrap(GTK_LABEL(scope),TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(view.status),TRUE);
    gtk_container_add(GTK_CONTAINER(scroll),view.preview);
    GtkWidget *items[]={
        scope,view.mode,label,view.margin,view.status
    };
    for(guint i=0;i<G_N_ELEMENTS(items);i++) gtk_box_pack_start(GTK_BOX(box),items[i],FALSE,FALSE,5);
    gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,5);
    insight_panel_help_attach(box,"paragraph-tools.md");
    g_signal_connect(view.mode,"changed",G_CALLBACK(paragraph_changed),&view);
    g_signal_connect(view.margin,"value-changed",G_CALLBACK(paragraph_changed),&view);
    paragraph_changed(NULL,&view);
    gtk_widget_show_all(dialog);
    gint response;
    while((response=gtk_dialog_run(GTK_DIALOG(dialog)))==GTK_RESPONSE_HELP || response==GTK_RESPONSE_APPLY) {
        gchar *result=paragraph_preview(&view);
        if(response==GTK_RESPONSE_APPLY && result) {
            paragraph_apply(&request,view.start,view.end,text,result);
            g_free(result);
            break;
        }
        g_free(result);
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    g_free(text);
    g_free(eol);
}
/* EN: Refresh paragraph command availability. menu: target menu; data: callback context or encoded action.
* RU: Обновляет доступность команд абзаца. menu: меню назначения; data: контекст либо код команды. */
static void paragraph_menu_show(GtkWidget *menu,gpointer data)
{
    (void)data;
    gboolean ready=tools_capture(&(Request){
        0
    });
    GList *children=gtk_container_get_children(GTK_CONTAINER(menu));
    for(GList *p=children;p;p=p->next) gtk_widget_set_sensitive(p->data,ready);
    g_list_free(children);
}
