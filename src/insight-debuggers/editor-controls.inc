/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared editor breakpoint commands and toolbar for built-in debugger sessions.
 * RU: Общие команды точек останова в редакторе и панель встроенных отладчиков. */

typedef struct
{
    GtkWidget *window, *ready;
    GtkWidget *commands[INSIGHT_DEBUG_COMMAND_COUNT];
    InsightDebugPoint toggle;
    gpointer data;
} InsightDebugSession;

static GList *debug_sessions;
static GtkToolItem *debug_tools[INSIGHT_DEBUG_COMMAND_COUNT + 1];

/* EN: Return the most recently activated debugger session, or NULL.
 * RU: Возвращает последний активированный сеанс отладки или NULL. */
static InsightDebugSession *active_debug_session(void)
{
    return debug_sessions ? debug_sessions->data : NULL;
}

/* EN: Return whether an editor can send a breakpoint command to an open debugger.
 * RU: Проверяет, открыт ли отладчик, принимающий команды установки точек. */
gboolean insight_debug_active(void)
{
    return active_debug_session() != NULL;
}

/* EN: Toggle a breakpoint at one-based line in filename in the active session.
 * RU: Переключает точку на строке line файла filename в активном сеансе. */
gboolean insight_debug_toggle(const gchar *filename, guint line)
{
    InsightDebugSession *session = active_debug_session();
    if (!session)
        return FALSE;
    if (!filename || !line)
        ui_set_statusbar(FALSE, "%s", insight_tr("Open a saved source file to toggle a breakpoint."));
    else if (!gtk_widget_is_sensitive(session->ready))
        ui_set_statusbar(FALSE, "%s", insight_tr("Pause the debugger before changing breakpoints."));
    else
        session->toggle(filename, line, session->data);
    return TRUE;
}

/* EN: Toggle the active editor's current line, returning whether a debugger handled it.
 * RU: Переключает точку на текущей строке редактора и сообщает, обработал ли команду отладчик. */
gboolean insight_debug_toggle_current(void)
{
    GeanyDocument *doc = document_get_current();
    if (!DOC_VALID(doc))
        return insight_debug_toggle(NULL, 0);
    GeanyEditor *editor = doc->editor;
    return insight_debug_toggle(doc->file_name,
        (guint)sci_get_line_from_position(editor->sci, sci_get_current_position(editor->sci)) + 1);
}

/* EN: Run the active session's toolbar command identified by data.
 * RU: Выполняет команду активного сеанса по индексу data. */
static void debug_tool_clicked(GtkToolButton *button, gpointer data)
{
    (void)button;
    InsightDebugSession *session = active_debug_session();
    guint command = GPOINTER_TO_UINT(data);
    if (!session)
        return;
    if (command == INSIGHT_DEBUG_COMMAND_COUNT)
        insight_debug_toggle_current();
    else if (session->commands[command] && gtk_widget_is_sensitive(session->commands[command]))
        gtk_button_clicked(GTK_BUTTON(session->commands[command]));
}

/* EN: Refresh toolbar availability for the current debugger. Signal arguments are unused.
 * RU: Обновляет доступность кнопок активного отладчика. Аргументы сигнала не используются. */
static void debug_tools_refresh(GtkWidget *widget, GtkStateFlags previous, gpointer data)
{
    (void)widget;
    (void)previous;
    (void)data;
    InsightDebugSession *session = active_debug_session();
    for (guint i = 0; i < G_N_ELEMENTS(debug_tools); i++)
    {
        if (!debug_tools[i])
            continue;
        GtkWidget *source = !session ? NULL :
            i == INSIGHT_DEBUG_COMMAND_COUNT ? session->ready : session->commands[i];
        gtk_widget_set_visible(GTK_WIDGET(debug_tools[i]), source != NULL);
        gtk_widget_set_sensitive(GTK_WIDGET(debug_tools[i]), source && gtk_widget_is_sensitive(source));
    }
}

/* EN: Activate the debugger in data when its window receives focus.
 * RU: Активирует сеанс data при получении фокуса его окном. */
static gboolean debug_session_focus(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
    (void)widget;
    (void)event;
    debug_sessions = g_list_prepend(g_list_remove(debug_sessions, data), data);
    debug_tools_refresh(NULL, 0, NULL);
    return FALSE;
}

/* EN: Unregister and release the debugger session in data when its window closes.
 * RU: Удаляет сеанс data при закрытии его окна. */
static void debug_session_closed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    debug_sessions = g_list_remove(debug_sessions, data);
    g_free(data);
    debug_tools_refresh(NULL, 0, NULL);
}

/* EN: Register window and its breakpoint/command handlers until the window is destroyed.
 * ready controls breakpoint availability. commands contains borrowed GtkButtons; data belongs to the caller.
 * RU: Регистрирует window и команды до закрытия окна. ready задаёт доступность точек,
 * commands содержит кнопки, data остаётся у вызывающей стороны. */
void insight_debug_register(GtkWidget *window, GtkWidget *ready, InsightDebugPoint toggle,
                            gpointer data, GtkWidget *const commands[INSIGHT_DEBUG_COMMAND_COUNT])
{
    InsightDebugSession *session = g_new0(InsightDebugSession, 1);
    session->window = window;
    session->ready = ready;
    session->toggle = toggle;
    session->data = data;
    for (guint i = 0; i < INSIGHT_DEBUG_COMMAND_COUNT; i++)
    {
        session->commands[i] = commands[i];
        if (commands[i])
            g_signal_connect(commands[i], "state-flags-changed", G_CALLBACK(debug_tools_refresh), NULL);
    }
    g_signal_connect(ready, "state-flags-changed", G_CALLBACK(debug_tools_refresh), NULL);
    debug_sessions = g_list_prepend(debug_sessions, session);
    g_signal_connect(window, "focus-in-event", G_CALLBACK(debug_session_focus), session);
    g_signal_connect(window, "destroy", G_CALLBACK(debug_session_closed), session);

    if (GTK_IS_TOOLBAR(main_widgets.toolbar) && !debug_tools[0])
    {
        const gchar *labels[] = {"Pause", "Continue", "Step Into", "Step Over", "Step Out",
                                 "Run", "Stop", "Toggle Breakpoint (F2)"};
        const gchar *icons[] = {"media-playback-pause", "media-playback-start", "go-down",
                                "go-next", "go-up", "system-run", "media-playback-stop", "media-record"};
        for (guint i = 0; i < G_N_ELEMENTS(debug_tools); i++)
        {
            GtkToolItem *item = gtk_tool_button_new(NULL, insight_tr(labels[i]));
            gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(item), icons[i]);
            insight_i18n_key(G_OBJECT(item), "label", labels[i]);
            insight_i18n_key(G_OBJECT(item), "tooltip-text", labels[i]);
            gtk_widget_set_no_show_all(GTK_WIDGET(item), TRUE);
            g_signal_connect(item, "clicked", G_CALLBACK(debug_tool_clicked), GUINT_TO_POINTER(i));
            toolbar_item_ref(item);
            gtk_toolbar_insert(GTK_TOOLBAR(main_widgets.toolbar), item, (gint)i);
            debug_tools[i] = item;
        }
    }
    debug_tools_refresh(NULL, 0, NULL);
}
