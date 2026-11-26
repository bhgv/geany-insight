/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared paste editing tools, split by responsibility without changing their operations.
 * RU: Общие инструменты правки, разделённые по ответственности без изменения операций. */
/* Edit tools share the paste generation counter, but never retain a destroyed
* Scintilla widget: resolve the document id before checking the target. */
/* EN: Capture a writable current destination and its edit generation in request; return whether valid.
* request: captured destination.
* RU: Фиксирует доступное назначение и поколение правок в request; возвращает признак корректности.
* request: зафиксированное назначение. */
static gboolean tools_capture(Request *request)
{
    GeanyDocument *doc=document_get_current();
    if(!doc || doc->readonly) return FALSE;
    ScintillaObject *sci=doc->editor->sci;
    if(SEND(sci,SCI_GETREADONLY,0,0) || SEND(sci,SCI_GETSELECTIONS,0,0)!=1 || sci_get_selection_mode(sci)!=SC_SEL_STREAM ||
    SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) || SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0)) return FALSE;
    request->sci=sci;
    request->id=doc->id;
    request->generation=state(sci)->generation;
    request->anchor=SEND(sci,SCI_GETANCHOR,0,0);
    request->caret=sci_get_current_position(sci);
    return TRUE;
}
/* EN: Insert text into the unchanged captured request in one Undo; return whether applied. request:
* captured destination; text: input text.
* RU: Вставляет text в неизменный request одним Undo; возвращает признак применения. request:
* зафиксированное назначение; text: входной текст. */
static gboolean tools_insert(Request *request,const gchar *text)
{
    GeanyDocument *doc=document_find_by_id(request->id);
    if(!doc || doc!=document_get_current() || doc->editor->sci!=request->sci) return FALSE;
    Request current={
        0
    };
    if(!tools_capture(&current) || current.generation!=request->generation || current.anchor!=request->anchor || current.caret!=request->caret) return FALSE;
    insight_completion_cancel(doc->editor);
    SEND(request->sci,SCI_AUTOCCANCEL,0,0);
    sci_start_undo_action(request->sci);
    sci_replace_sel(request->sci,text);
    sci_end_undo_action(request->sci);
    gtk_widget_grab_focus(GTK_WIDGET(request->sci));
    return TRUE;
}
/* Word edits use the editor's character classes, never temporary selections. */
/* EN: Execute bounded word editing action for the current editor. action: command ID.
* RU: Выполняет ограниченную правку слов action в текущем редакторе. action: код команды. */
void insight_paste_word_edit(guint action)
{
    Request request = {
        0
    };
    if (!tools_capture(&request)) return;
    ScintillaObject *sci = request.sci;
    if (action > 3) return;
    gint length = sci_get_length(sci);
    gint begin = request.caret, end = begin;
    if (action == 0)
    end = SEND(sci, SCI_WORDENDPOSITION, begin, FALSE);
    else if (action == 1 && begin > 0)
    begin = SEND(sci, SCI_WORDSTARTPOSITION, SEND(sci, SCI_POSITIONBEFORE, begin, 0), FALSE);
    else if (action == 2)
    {
        gint line = sci_get_line_from_position(sci, begin);
        begin = sci_get_position_from_line(sci, line);
        end = line + 1 < sci_get_line_count(sci) ? sci_get_position_from_line(sci, line + 1) : length;
    }
    else if (action == 3)
    {
        begin = MIN(request.anchor, request.caret);
        end = MAX(request.anchor, request.caret);
        if (begin == end) end = SEND(sci, SCI_POSITIONAFTER, end, 0);
    }
    if (begin < 0 || end > length || end <= begin) return;
    if (end - begin > 1024 * 1024)
    {
        ui_set_statusbar(TRUE, "%s", insight_tr("Text removal is limited to 1 MiB per command"));
        return;
    }
    /* Do not split CRLF when a plugin supplied a raw byte position. */
    if ((begin > 0 && SEND(sci, SCI_GETCHARAT, begin - 1, 0) == '\r' && SEND(sci, SCI_GETCHARAT, begin, 0) == '\n') ||
    (end < length && SEND(sci, SCI_GETCHARAT, end - 1, 0) == '\r' && SEND(sci, SCI_GETCHARAT, end, 0) == '\n')) return;
    gchar *text = sci_get_contents_range(sci, begin, end);
    if (!text || strlen(text) != (gsize)(end - begin) || !g_utf8_validate(text, end - begin, NULL))
    {
        g_free(text);
        ui_set_statusbar(TRUE, "%s", insight_tr("Text removal requires complete UTF-8 text without NUL bytes"));
        return;
    }
    if (action < 2) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, end - begin);
    g_free(text);
    GeanyDocument *doc = document_find_by_id(request.id);
    Request current = {
        0
    };
    if (!doc || doc != document_get_current() || doc->editor->sci != sci ||
    !tools_capture(&current) || current.generation != request.generation ||
    current.anchor != request.anchor || current.caret != request.caret) return;
    insight_completion_cancel(doc->editor);
    SEND(sci, SCI_AUTOCCANCEL, 0, 0);
    sci_start_undo_action(sci);
    SEND(sci, SCI_SETSEL, begin, end);
    sci_replace_sel(sci, "");
    sci_end_undo_action(sci);
    SEND(sci, SCI_CHOOSECARETX, 0, 0);
    SEND(sci, SCI_SCROLLCARET, 0, 0);
    gtk_widget_grab_focus(GTK_WIDGET(sci));
}
/* EN: Dispatch the word-edit action encoded in data. item: source menu item; data: callback context or
* encoded action.
* RU: Выполняет команду правки слов, закодированную в data. item: пункт меню; data: контекст либо код
* команды. */
static void word_edit_command(GtkMenuItem *item, gpointer data)
{
    (void)item;
    insight_paste_word_edit(GPOINTER_TO_UINT(data));
}
/* EN: Refresh word-edit menu availability for the active selection. menu: target menu; data: callback
* context or encoded action.
* RU: Обновляет доступность правки слов для текущего выделения. menu: меню назначения; data: контекст либо
* код команды. */
static void word_edit_show(GtkWidget *menu, gpointer data)
{
    (void)data;
    Request request = {
        0
    };
    gboolean ready = tools_capture(&request);
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *p = children; p; p = p->next)
    if (g_object_get_data(G_OBJECT(p->data), "insight-word-edit")) gtk_widget_set_sensitive(p->data, ready);
    g_list_free(children);
}
/* EN: Build the word-editing submenu with stable command IDs. menu: target menu.
* RU: Создаёт подменю правки слов с устойчивыми именами команд. menu: меню назначения. */
static void word_edit_menu(GtkWidget *menu)
{
    GtkWidget *parent = gtk_menu_item_new_with_label("Word and character operations"), *sub = gtk_menu_new();
    gtk_widget_set_name(parent, "insight-word-editing");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), sub);
    const gchar *labels[] = {
        "Cut Word", "Cut Word Left", "Delete Line", "Delete Character"
    };
    const gchar *ids[] = {
        "insight-cut-word", "insight-cut-word-left", "insight-delete-line", "insight-delete-character"
    };
    const guint keys[] = {
        GEANY_KEYS_INSIGHT_CUTWORD, GEANY_KEYS_INSIGHT_CUTWORDLEFT, GEANY_KEYS_INSIGHT_DELETELINE, GEANY_KEYS_INSIGHT_DELETECHARACTER
    };
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *entry = gtk_menu_item_new_with_label(labels[i]);
        gtk_widget_set_name(entry, ids[i]);
        keybindings_attach_menu(i < 2 ? GEANY_KEY_GROUP_CLIPBOARD : GEANY_KEY_GROUP_EDITOR, keys[i], entry);
        g_object_set_data(G_OBJECT(entry), "insight-word-edit", GINT_TO_POINTER(1));
        g_signal_connect(entry, "activate", G_CALLBACK(word_edit_command), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub), entry);
    }
    GtkWidget *help = gtk_menu_item_new_with_label("Word Editing Help");
    g_signal_connect(help, "activate", G_CALLBACK(insight_help_show), "word-editing.md");
    gtk_menu_shell_append(GTK_MENU_SHELL(sub), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(sub), help);
    g_signal_connect(sub, "show", G_CALLBACK(word_edit_show), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent);
    gtk_widget_show_all(parent);
}
