/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared paste editing tools, split by responsibility without changing their operations.
 * RU: Общие инструменты правки, разделённые по ответственности без изменения операций. */
typedef struct {
    GtkWidget *mode,*indent,*preview,*status;
    const gchar *source,*eol;
    gint anchor,caret;
}
LineInsertView;
/* EN: Build plan from the captured line insertion view; return validation success. view: review state;
* plan: validated edit plan.
* RU: Создаёт plan по окну вставки строк view; возвращает успех проверки. view: состояние просмотра; plan:
* проверенный план. */
static gboolean line_insert_plan(LineInsertView *view,InsightLineInsert *plan)
{
    GError *error=NULL;
    gboolean ok=insight_line_insert(view->source,view->anchor,view->caret,gtk_combo_box_get_active(GTK_COMBO_BOX(view->mode)),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->indent)),view->eol,plan,&error);
    if(!ok) {
        gtk_label_set_text(GTK_LABEL(view->status),insight_tr(error->message));
        g_clear_error(&error);
    }
    return ok;
}
/* EN: Render the line insertion plan and its resulting selection in view. view: review state; plan:
* validated edit plan.
* RU: Показывает plan вставки строк и итоговое выделение в view. view: состояние просмотра; plan:
* проверенный план. */
static void line_insert_render(LineInsertView *view,const InsightLineInsert *plan)
{
    GString *result=g_string_new_len(view->source,plan->start);
    g_string_append(result,plan->text);
    g_string_append(result,view->source+plan->end);
    gint low=MAX(0,plan->caret-4000),high=MIN((gint)result->len,plan->caret+4000);
    while(low<plan->caret && (result->str[low]&0xc0)==0x80) low++;
    while(high>plan->caret && (result->str[high]&0xc0)==0x80) high--;
    if(low && result->str[low]=='\n' && result->str[low-1]=='\r') low++;
    if(high<(gint)result->len && high && result->str[high-1]=='\r' && result->str[high]=='\n') high--;
    GString *display=g_string_new(NULL);
    for(gint p=low;p<=high;) {
        if(p==plan->caret) g_string_append(display,"│");
        if(p==high) break;
        gchar c=result->str[p];
        if(c=='\r' || c=='\n') {
            g_string_append(display,"¶\n");
            if(c=='\r' && p+1<high && result->str[p+1]=='\n') p++;
            p++;
        }
        else if(c==' ' || c=='\t') {
            g_string_append(display,c==' ' ? "·" : "⇥");
            p++;
        }
        else {
            const gchar *next=g_utf8_next_char(result->str+p);
            g_string_append_len(display,result->str+p,next-(result->str+p));
            p=next-result->str;
        }
    }
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),display->str,display->len);
    gchar *status=insight_i18n_printf("Replace bytes %d–%d; caret %d. Shown result bytes %d–%d.",plan->start,plan->end,plan->caret,low,high);
    gtk_label_set_text(GTK_LABEL(view->status),status);
    g_free(status);
    g_string_free(display,TRUE);
    g_string_free(result,TRUE);
}
/* EN: Recompute the line insertion preview after options change. widget: source widget; data: callback
* context or encoded action.
* RU: Пересчитывает предпросмотр вставки строк после смены параметров. widget: виджет события; data:
* контекст либо код команды. */
static void line_insert_changed(GtkWidget *widget,gpointer data)
{
    (void)widget;
    LineInsertView *view=data;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),"",0);
    gtk_label_set_text(GTK_LABEL(view->status),"Press Preview to inspect the result before applying");
}
/* EN: Apply plan only to the unchanged captured request in one Undo. request: captured destination; plan:
* validated edit plan.
* RU: Применяет plan только к неизменному request одним Undo. request: зафиксированное назначение; plan:
* проверенный план. */
static gboolean line_insert_apply(Request *request,const InsightLineInsert *plan)
{
    if(!file_request_valid(request,TRUE) || !tools_capture(&(Request){
        0
    })) {
        ui_set_statusbar(TRUE,"Line insertion cancelled because the document or selection changed");
        return FALSE;
    }
    insight_completion_cancel(document_get_current()->editor);
    SEND(request->sci,SCI_AUTOCCANCEL,0,0);
    sci_start_undo_action(request->sci);
    SEND(request->sci,SCI_SETTARGETRANGE,plan->start,plan->end);
    SEND(request->sci,SCI_REPLACETARGET,strlen(plan->text),(sptr_t)plan->text);
    SEND(request->sci,SCI_SETSEL,plan->caret,plan->caret);
    sci_end_undo_action(request->sci);
    SEND(request->sci,SCI_SCROLLCARET,0,0);
    gtk_widget_grab_focus(GTK_WIDGET(request->sci));
    return TRUE;
}
/* EN: Open the guarded line insertion review dialog. item: source menu item; data: callback context or
* encoded action.
* RU: Открывает защищённый диалог просмотра вставки строк. item: пункт меню; data: контекст либо код
* команды. */
static void line_insert_command(GtkMenuItem *item,gpointer data)
{
    (void)item;
    Request request={
        0
    };
    if(!tools_capture(&request)) return;
    if(sci_get_length(request.sci)>8*1024*1024) {
        ui_set_statusbar(TRUE,"Line insertion is limited to 8 MiB");
        return;
    }
    GeanyEditor *editor=document_get_current()->editor;
    gboolean indent=editor_get_indent_prefs(editor)->auto_indent_mode!=GEANY_AUTOINDENT_NONE;
    gchar *source=sci_get_contents(request.sci,-1),*eol=g_strdup(editor_get_eol_char(editor));
    gint command=GPOINTER_TO_INT(data);
    InsightLineInsert plan={
        0
    };
    GError *error=NULL;
    if(strlen(source)!=(gsize)sci_get_length(request.sci)) {
        ui_set_statusbar(TRUE,"Line insertion requires text without NUL bytes");
        g_free(source);
        g_free(eol);
        return;
    }
    if(command<3) {
        if(insight_line_insert(source,request.anchor,request.caret,command,indent,eol,&plan,&error)) line_insert_apply(&request,&plan);
        else ui_set_statusbar(TRUE,"%s",insight_tr(error->message));
        g_clear_error(&error);
        insight_line_insert_clear(&plan);
        g_free(source);
        g_free(eol);
        return;
    }
    GtkWidget *dialog=gtk_dialog_new_with_buttons("Line insertion preview",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Preview",GTK_RESPONSE_HELP,"Apply",GTK_RESPONSE_APPLY,NULL);
    g_object_ref_sink(dialog);
    gtk_widget_set_name(dialog,"insight-line-insert-dialog");
    gtk_window_set_default_size(GTK_WINDOW(dialog),780,470);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    LineInsertView view={
        gtk_combo_box_text_new(),gtk_check_button_new_with_label("Copy the current line's indentation (temporary override)"),gtk_text_view_new(),gtk_label_new(""),source,eol,request.anchor,request.caret
    };
    const gchar *labels[]={
        "Insert Line — above","Insert Line Before Next — below","Insert New Line — split and stay"
    };
    for(guint i=0;i<3;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view.mode),labels[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(view.mode),0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(view.indent),indent);
    gtk_widget_set_name(view.mode,"insight-line-insert-mode");
    gtk_widget_set_name(view.indent,"insight-line-insert-indent");
    gtk_widget_set_name(view.preview,"insight-line-insert-preview");
    gtk_widget_set_name(view.status,"insight-line-insert-status");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view.preview),FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view.preview),TRUE);
    g_object_set_data(G_OBJECT(view.preview),"insight-no-i18n",GINT_TO_POINTER(1));
    GtkWidget *scope=gtk_label_new("Above/below use the active caret line. Split replaces the exact selection."),*legend=gtk_label_new("Preview marks: · space, ⇥ tab, ¶ line ending, │ resulting caret"),*scroll=gtk_scrolled_window_new(NULL,NULL);
    gtk_label_set_line_wrap(GTK_LABEL(scope),TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(legend),TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(view.status),TRUE);
    gtk_container_add(GTK_CONTAINER(scroll),view.preview);
    GtkWidget *items[]={
        scope,view.mode,view.indent,legend,view.status
    };
    for(guint i=0;i<G_N_ELEMENTS(items);i++) gtk_box_pack_start(GTK_BOX(box),items[i],FALSE,FALSE,5);
    gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,5);
    insight_panel_help_attach(box,"line-insertion.md");
    g_signal_connect(view.mode,"changed",G_CALLBACK(line_insert_changed),&view);
    g_signal_connect(view.indent,"toggled",G_CALLBACK(line_insert_changed),&view);
    line_insert_changed(NULL,&view);
    gtk_widget_show_all(dialog);
    gint response;
    while((response=gtk_dialog_run(GTK_DIALOG(dialog)))==GTK_RESPONSE_HELP || response==GTK_RESPONSE_APPLY) {
        if(line_insert_plan(&view,&plan)) {
            line_insert_render(&view,&plan);
            if(response==GTK_RESPONSE_APPLY) {
                line_insert_apply(&request,&plan);
                insight_line_insert_clear(&plan);
                break;
            }
            insight_line_insert_clear(&plan);
        }
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    g_free(source);
    g_free(eol);
}
/* EN: Build the line insertion submenu. menu: target menu.
* RU: Создаёт подменю вставки строк. menu: меню назначения. */
static void line_insert_menu(GtkWidget *menu)
{
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
    const gchar *labels[]={
        "Insert Line","Insert Line Before Next","Insert New Line","Line Insertion Preview…"
    },*ids[]={
        "insight-insert-line","insight-insert-line-below","insight-insert-new-line","insight-line-insert-preview-command"
    };
    for(guint i=0;i<4;i++) {
        GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);
        gtk_widget_set_name(entry,ids[i]);
        g_signal_connect(entry,"activate",G_CALLBACK(line_insert_command),GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry);
    }
}
/* EN: Build the paragraph transformation submenu. menu: target menu.
* RU: Создаёт подменю преобразования абзацев. menu: меню назначения. */
static void paragraph_menu(GtkWidget *menu)
{
    GtkWidget *parent=gtk_menu_item_new_with_label("Paragraph and lines"),*sub=gtk_menu_new();
    gtk_widget_set_name(parent,"insight-paragraph-tools");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),sub);
    const gchar *labels[]={
        "Reform Paragraph","Join Lines","Paragraph Preview…"
    },*ids[]={
        "insight-reform-paragraph","insight-join-lines","insight-paragraph-preview-command"
    };
    for(guint i=0;i<3;i++) {
        GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);
        gtk_widget_set_name(entry,ids[i]);
        g_signal_connect(entry,"activate",G_CALLBACK(paragraph_command),GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub),entry);
    }
    line_insert_menu(sub);
    g_signal_connect(sub,"show",G_CALLBACK(paragraph_menu_show),NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),parent);
    gtk_widget_show_all(parent);
}
