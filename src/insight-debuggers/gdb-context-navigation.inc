/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Route thread/frame selections and source navigation through confirmed GDB context changes.
 * RU: Выполняет выбор потока/кадра и переходы к исходникам через подтверждённую смену контекста GDB. */

/* EN: Select the GDB thread chosen in combo for the dialog in data.
 * RU: Выбирает поток GDB из combo для диалога из data. */
static void native_select_thread(GtkComboBox *combo, gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    const gchar *id = gtk_combo_box_get_active_id(combo);
    if (session->closed || !session->ready || session->filling_context || !session->stopped ||
        session->pending || session->writing || !native_context_id(id))
        return;
    gchar *command = g_strdup_printf("-thread-select %s", id);
    session->context_pending = NATIVE_CONTEXT_SELECT_THREAD;
    native_debug_send(dialog, command);
    g_free(command);
}

/* EN: Select the GDB frame in selection for the dialog in data.
 * RU: Выбирает кадр GDB из selection для диалога из data. */
static void native_select_frame(GtkTreeSelection *selection, gpointer data)
{
    (void)selection;
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (session->closed || !session->ready || session->filling_context || !session->stopped ||
        session->pending || session->writing)
        return;
    gchar *level = native_selected_frame(session, NULL, NULL);
    if (native_context_id(level))
    {
        gchar *command = g_strdup_printf("-stack-select-frame %s", level);
        g_free(session->requested_frame);
        session->requested_frame = g_strdup(level);
        session->context_pending = NATIVE_CONTEXT_SELECT_FRAME;
        native_debug_send(dialog, command);
        g_free(command);
    }
    g_free(level);
}

/* EN: Select path in view for dialog data; open source only after GDB confirms the frame.
 * Reopening the confirmed frame needs no command. Invalid, busy and inactive activations are ignored.
 * RU: Выбирает path в view для окна data; открывает исходник только после подтверждения кадра GDB.
 * Повторный переход к подтверждённому кадру не требует команды. Неверные и занятые переходы игнорируются. */
static void native_open_frame(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column,
                              gpointer data)
{
    (void)column;
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (session->closed || !session->ready || !session->stopped || session->pending || session->writing)
        return;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter(model, &row, path))
        return;
    gchar *level = NULL;
    gtk_tree_model_get(model, &row, 0, &level, -1);
    if (native_context_id(level))
    {
        if (!g_strcmp0(level, session->selected_frame))
            native_source_stopped(session);
        else
        {
            session->filling_context = TRUE;
            GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
            gtk_tree_selection_select_iter(selection, &row);
            session->filling_context = FALSE;
            native_select_frame(selection, dialog);
        }
    }
    g_free(level);
}
