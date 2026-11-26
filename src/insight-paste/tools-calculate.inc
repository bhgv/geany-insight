/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared paste editing tools, split by responsibility without changing their operations.
 * RU: Общие инструменты правки, разделённые по ответственности без изменения операций. */
/* EN: Return an owned bounded UTF-8 expression from the active selection or NULL.
* RU: Возвращает собственное ограниченное выражение UTF-8 из выделения либо NULL. */
static gchar *calculate_selection(void)
{
    GeanyDocument *doc=document_get_current();
    if(!doc) return NULL;
    ScintillaObject *sci=doc->editor->sci;
    if(SEND(sci,SCI_GETSELECTIONS,0,0)!=1 || sci_get_selection_mode(sci)!=SC_SEL_STREAM || SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) || SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0)) return NULL;
    gint start=sci_get_selection_start(sci),end=sci_get_selection_end(sci);
    if(end<=start || end-start>4096) return NULL;
    gchar *text=sci_get_contents_range(sci,start,end);
    if(strlen(text)!=(gsize)(end-start) || !g_utf8_validate(text,-1,NULL)) g_clear_pointer(&text,g_free);
    return text;
}
/* EN: Replace the unchanged captured expression with result; return whether applied. request: captured
* destination; result: output/result.
* RU: Заменяет неизменное выражение request результатом result; возвращает успех. request: зафиксированное
* назначение; result: результат. */
static gboolean calculate_insert(Request *request,const gchar *result)
{
    if(!file_request_valid(request,TRUE) || !tools_capture(&(Request){
        0
    })) return FALSE;
    gchar *selected=calculate_selection();
    gboolean same=selected && !strcmp(selected,result);
    g_free(selected);
    return same || tools_insert(request,result);
}
/* EN: Calculate and replace the captured expression after validation. item: source menu item; data:
* callback context or encoded action.
* RU: Вычисляет и заменяет зафиксированное выражение после проверки. item: пункт меню; data: контекст либо
* код команды. */
static void calculate_direct(GtkMenuItem *item,gpointer data)
{
    (void)item;
    (void)data;
    Request request={
        0
    };
    if(!tools_capture(&request)) return;
    gchar *expression=calculate_selection();
    if(!expression) {
        ui_set_statusbar(TRUE,"Select a UTF-8 expression of at most 4096 bytes without NUL");
        return;
    }
    GError *error=NULL;
    guint offset=0;
    gchar *result=insight_calculate(expression,&offset,&error);
    if(result) {
        if(!calculate_insert(&request,result)) ui_set_statusbar(TRUE,"Calculation insertion cancelled because the document or selection changed");
    }
    else {
        gchar *status=insight_i18n_printf("Calculation error at byte %u: %s",offset,insight_tr(error->message));
        ui_set_statusbar(TRUE,"%s",status);
        g_free(status);
    }
    g_clear_error(&error);
    g_free(result);
    g_free(expression);
}
typedef struct {
    GtkWidget *expression,*result,*status;
}
CalculateView;
/* EN: Refresh the calculator result and validation status. buffer: expression buffer; data: callback
* context or encoded action.
* RU: Обновляет результат вычисления и состояние проверки. buffer: буфер выражения; data: контекст либо
* код команды. */
static void calculate_changed(GtkTextBuffer *buffer,gpointer data)
{
    (void)buffer;
    CalculateView *view=data;
    gtk_label_set_text(GTK_LABEL(view->result),"");
    gtk_label_set_text(GTK_LABEL(view->status),"Press Calculate to update the result");
}
/* EN: Return an owned calculator result for view or NULL on invalid input. view: review state.
* RU: Возвращает собственный результат view либо NULL при неверном вводе. view: состояние просмотра. */
static gchar *calculate_view(CalculateView *view)
{
    GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->expression));
    if(gtk_text_buffer_get_char_count(buffer)>4096) {
        gtk_label_set_text(GTK_LABEL(view->status),"Calculation requires UTF-8 up to 4096 bytes");
        gtk_label_set_text(GTK_LABEL(view->result),"");
        return NULL;
    }
    GtkTextIter start,end;
    gtk_text_buffer_get_bounds(buffer,&start,&end);
    gchar *expression=gtk_text_buffer_get_text(buffer,&start,&end,FALSE);
    GError *error=NULL;
    guint offset=0;
    gchar *result=insight_calculate(expression,&offset,&error);
    g_free(expression);
    if(result) {
        gtk_label_set_text(GTK_LABEL(view->result),result);
        gtk_label_set_text(GTK_LABEL(view->status),"Approximate double-precision result; large integers may be rounded");
    }
    else {
        gchar *status=insight_i18n_printf("Calculation error at byte %u: %s",offset,insight_tr(error->message));
        gtk_label_set_text(GTK_LABEL(view->status),status);
        g_free(status);
        gtk_label_set_text(GTK_LABEL(view->result),"");
    }
    g_clear_error(&error);
    return result;
}
/* EN: Review calculator input and apply only to the unchanged captured source. item: source menu item;
* data: callback context or encoded action.
* RU: Показывает вычисление и применяет лишь к неизменному исходнику. item: пункт меню; data: контекст
* либо код команды. */
static void calculate_dialog(GtkMenuItem *item,gpointer data)
{
    (void)item;
    (void)data;
    Request request={
        0
    };
    gboolean writable=tools_capture(&request);
    gchar *expression=calculate_selection();
    GtkWidget *dialog=gtk_dialog_new_with_buttons("Calculator",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Close",GTK_RESPONSE_CLOSE,"Calculate",GTK_RESPONSE_HELP,"Copy Result",GTK_RESPONSE_ACCEPT,"Insert Result",GTK_RESPONSE_APPLY,NULL);
    g_object_ref_sink(dialog);
    gtk_widget_set_name(dialog,"insight-calculator-dialog");
    gtk_window_set_default_size(GTK_WINDOW(dialog),780,470);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),GTK_RESPONSE_APPLY,writable);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    CalculateView view={
        gtk_text_view_new(),gtk_label_new(""),gtk_label_new("")
    };
    gtk_widget_set_name(view.expression,"insight-calculator-expression");
    gtk_widget_set_name(view.result,"insight-calculator-result");
    gtk_widget_set_name(view.status,"insight-calculator-status");
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view.expression),TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view.expression),GTK_WRAP_WORD_CHAR);
    gtk_label_set_selectable(GTK_LABEL(view.result),TRUE);
    g_object_set_data(G_OBJECT(view.expression),"insight-no-i18n",GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(view.result),"insight-no-i18n",GINT_TO_POINTER(1));
    GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view.expression));
    gtk_text_buffer_set_text(buffer,expression ? expression : "",-1);
    g_free(expression);
    GtkWidget *scope=gtk_label_new(writable ? "Insert Result uses the selection or cursor where this window was opened." : "Read-only calculator: results can be calculated and copied."),*syntax=gtk_label_new("Decimal/scientific numbers; + - * / % ^; parentheses; pi, e.\nabs sqrt floor ceil round sin cos tan log exp min max pow"),*label=gtk_label_new("Expression"),*result_label=gtk_label_new("Result"),*scroll=gtk_scrolled_window_new(NULL,NULL);
    gtk_label_set_line_wrap(GTK_LABEL(scope),TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(syntax),TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(view.status),TRUE);
    gtk_container_add(GTK_CONTAINER(scroll),view.expression);
    gtk_box_pack_start(GTK_BOX(box),scope,FALSE,FALSE,5);
    gtk_box_pack_start(GTK_BOX(box),syntax,FALSE,FALSE,5);
    gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,4);
    gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,5);
    gtk_box_pack_start(GTK_BOX(box),result_label,FALSE,FALSE,4);
    gtk_box_pack_start(GTK_BOX(box),view.result,FALSE,FALSE,5);
    gtk_box_pack_start(GTK_BOX(box),view.status,FALSE,FALSE,5);
    insight_panel_help_attach(box,"calculate.md");
    g_signal_connect(buffer,"changed",G_CALLBACK(calculate_changed),&view);
    calculate_changed(buffer,&view);
    gtk_widget_show_all(dialog);
    gint response;
    while((response=gtk_dialog_run(GTK_DIALOG(dialog)))==GTK_RESPONSE_HELP || response==GTK_RESPONSE_ACCEPT || response==GTK_RESPONSE_APPLY) {
        gchar *result=calculate_view(&view);
        if(!result) continue;
        if(response==GTK_RESPONSE_ACCEPT) {
            gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),result,-1);
            gtk_label_set_text(GTK_LABEL(view.status),"Result copied to clipboard");
        }
        if(response==GTK_RESPONSE_APPLY) {
            if(!writable || !calculate_insert(&request,result)) ui_set_statusbar(TRUE,"Calculation insertion cancelled because the document or selection changed");
            g_free(result);
            break;
        }
        g_free(result);
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
}
/* EN: Refresh calculator menu availability for the active expression. menu: target menu; data: callback
* context or encoded action.
* RU: Обновляет доступность вычислений для текущего выражения. menu: меню назначения; data: контекст либо
* код команды. */
static void calculate_menu_show(GtkWidget *menu,gpointer data)
{
    (void)data;
    Request request={
        0
    };
    gchar *expression=calculate_selection();
    gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"calculate"),tools_capture(&request) && expression!=NULL);
    g_free(expression);
}
/* EN: Create the calculator commands submenu. menu: target menu.
* RU: Создаёт подменю команд вычисления. menu: меню назначения. */
static void calculate_menu(GtkWidget *menu)
{
    GtkWidget *parent=gtk_menu_item_new_with_label("Calculation"),*sub=gtk_menu_new();
    gtk_widget_set_name(parent,"insight-calculation-tools");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),sub);
    const gchar *labels[]={
        "Calculate Selection","Calculator…"
    },*ids[]={
        "insight-calculate-selection","insight-calculator"
    };
    for(guint i=0;i<2;i++) {
        GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);
        gtk_widget_set_name(entry,ids[i]);
        if(!i) g_object_set_data(G_OBJECT(sub),"calculate",entry);
        g_signal_connect(entry,"activate",i ? G_CALLBACK(calculate_dialog) : G_CALLBACK(calculate_direct),NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(sub),entry);
    }
    g_signal_connect(sub,"show",G_CALLBACK(calculate_menu_show),NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),parent);
    gtk_widget_show_all(parent);
}
