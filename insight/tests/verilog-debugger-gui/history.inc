/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Failure reporting, register history and signal-stop fixtures.
 * RU: Диагностика, история регистров и настройки остановок по сигналам. */

/* Fail with the current phase so the CI log identifies the blocked action. */
/* Завершает проверку с номером этапа, чтобы в CI было видно, где она остановилась. */
static gboolean fail_timeout(gpointer data)
{
    (void)data;
    g_printerr("Verilog debugger GTK test timed out at phase %u\n", (guint)phase);
    GList *windows = gtk_window_list_toplevels();
    for (GList *entry = windows; entry; entry = entry->next)
    {
        GtkWidget *window = entry->data;
        if (!gtk_widget_get_visible(window))
            continue;
        const gchar *title = gtk_window_get_title(GTK_WINDOW(window));
        g_printerr("Visible window: %s\n", title ? title : "(untitled)");
        if (GTK_IS_MESSAGE_DIALOG(window))
        {
            gchar *message = NULL, *details = NULL;
            g_object_get(window, "text", &message, "secondary-text", &details, NULL);
            g_printerr("%s\n%s\n", message ? message : "", details ? details : "");
            g_free(message);
            g_free(details);
        }
        GtkWidget *status = find_control(window, "statusbar");
        if (status)
        {
            GtkWidget *area = gtk_statusbar_get_message_area(GTK_STATUSBAR(status));
            GList *children = gtk_container_get_children(GTK_CONTAINER(area));
            for (GList *child = children; child; child = child->next)
                if (GTK_IS_LABEL(child->data))
                    g_printerr("Status: %s\n", gtk_label_get_text(GTK_LABEL(child->data)));
            g_list_free(children);
        }
        GtkWidget *console = find_control(window, "console");
        if (console)
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console));
            GtkTextIter first, last;
            gtk_text_buffer_get_bounds(buffer, &first, &last);
            gchar *text = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
            g_printerr("Console:\n%s\n", text);
            g_free(text);
        }
    }
    g_list_free(windows);
    exit(1);
}

static void log_graph_status(GtkStatusbar *bar, guint context, const gchar *message,
                             gpointer data)
{
    (void)bar;
    (void)context;
    (void)data;
    if (phase == INSPECT_CONNECTIONS && message && *message)
        g_print("Graph status: %s\n", message);
}

/* EN: Return the path for signal in tree, optionally requiring raw bits.
 * RU: Возвращает путь сигнала signal в tree с необязательной проверкой bits. */
static GtkTreePath *register_row(GtkTreeView *tree, const gchar *signal, const gchar *bits)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter row;
    for (gboolean more = gtk_tree_model_get_iter_first(model, &row); more;
         more = gtk_tree_model_iter_next(model, &row))
    {
        gchar *name, *raw;
        gtk_tree_model_get(model, &row, 0, &name, 2, &raw, -1);
        gboolean match = !strcmp(name, signal) && (!bits || !strcmp(raw, bits));
        g_free(name);
        g_free(raw);
        if (match)
            return gtk_tree_model_get_path(model, &row);
    }
    return NULL;
}

/* EN: Edit the register at path in tree to bits through its real entry.
 * RU: Изменяет регистр по path в tree на bits через редактор ячейки. */
static gboolean edit_register(GtkTreeView *tree, GtkTreePath *path, const gchar *bits)
{
    gtk_tree_view_set_cursor(tree, path, gtk_tree_view_get_column(tree, 1), TRUE);
    GtkWidget *entry = find_control(GTK_WIDGET(tree), "insight-verilog-register-edit");
    if (!GTK_IS_ENTRY(entry))
        return FALSE;
    gtk_entry_set_text(GTK_ENTRY(entry), bits);
    g_signal_emit_by_name(entry, "activate");
    return TRUE;
}

/* EN: Validate history and activate the first recorded tick. data is unused.
 * RU: Проверяет историю и открывает первый записанный такт. data не используется. */
static gboolean open_history_tick(gpointer data)
{
    (void)data;
    GtkWidget *dialog = find_window("Signal changes");
    if (!dialog)
        return G_SOURCE_CONTINUE;
    GtkWidget *widget = find_control(dialog, "insight-verilog-signal-history");
    if (!GTK_IS_TREE_VIEW(widget))
        return fail_timeout(NULL);

    GtkTreeView *tree = GTK_TREE_VIEW(widget);
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter row;
    gboolean saw_stop = FALSE;
    GtkTreePath *first_tick = NULL;
    for (gboolean more = gtk_tree_model_get_iter_first(model, &row); more;
         more = gtk_tree_model_iter_next(model, &row))
    {
        gchar *tick;
        gtk_tree_model_get(model, &row, 6, &tick, -1);
        saw_stop |= !strcmp(tick, "15");
        if (!strcmp(tick, "0") && !first_tick)
            first_tick = gtk_tree_model_get_path(model, &row);
        g_free(tick);
    }
    if (!saw_stop || !first_tick)
        return fail_timeout(NULL);
    gtk_tree_view_set_cursor(tree, first_tick, gtk_tree_view_get_column(tree, 0), FALSE);
    gtk_tree_view_row_activated(tree, first_tick, gtk_tree_view_get_column(tree, 0));
    gtk_tree_path_free(first_tick);
    return G_SOURCE_REMOVE;
}

/* EN: Open the history menu on register path in tree.
 * RU: Открывает историю регистра по path в tree через меню строки. */
static gboolean open_register_history(GtkTreeView *tree, GtkTreePath *path)
{
    GdkRectangle area;
    gtk_tree_view_get_cell_area(tree, path, gtk_tree_view_get_column(tree, 0), &area);
    GdkEvent *event = gdk_event_new(GDK_BUTTON_PRESS);
    event->button.window = g_object_ref(gtk_tree_view_get_bin_window(tree));
    event->button.button = 3;
    event->button.x = area.x + 4;
    event->button.y = area.y + area.height / 2;
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(tree));
    GdkSeat *seat = gdk_display_get_default_seat(display);
    if (seat)
        gdk_event_set_device(event, gdk_seat_get_pointer(seat));
    gboolean handled = FALSE;
    g_signal_emit_by_name(tree, "button-press-event", event, &handled);
    gdk_event_free(event);
    if (!handled)
        return FALSE;
    GList *menus = gtk_menu_get_for_attach_widget(GTK_WIDGET(tree));
    for (GList *menu = menus; menu; menu = menu->next)
    {
        GtkWidget *changes = find_control(menu->data, "Changes…");
        if (!GTK_IS_MENU_ITEM(changes))
            continue;
        g_timeout_add(100, open_history_tick, NULL);
        gtk_menu_item_activate(GTK_MENU_ITEM(changes));
        return TRUE;
    }
    return FALSE;
}

/* EN: Verify restored rules in simulation and select the clock rule.
 * enabled is the expected clock rule state.
 * RU: Проверяет правила в simulation и выбирает правило такта.
 * enabled — ожидаемое состояние правила. */
static gboolean restored_signal_rules(GtkWidget *simulation, gboolean enabled)
{
    GtkWidget *tree = find_control(simulation, "insight-verilog-signal-stops");
    if (!GTK_IS_TREE_VIEW(tree))
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    GtkTreeIter row;
    gboolean armed = FALSE, missing = FALSE, disabled = FALSE;
    for (gboolean more = gtk_tree_model_get_iter_first(model, &row); more;
         more = gtk_tree_model_iter_next(model, &row))
    {
        gchar *path, *state;
        gtk_tree_model_get(model, &row, 0, &path, 4, &state, -1);
        if (!strcmp(path, "counter_tb.dut.clk"))
        {
            armed = !strcmp(state, enabled ? "Armed signal stop" : "Disabled signal stop");
            gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &row);
        }
        missing |= !strcmp(path, "counter_tb.dut.missing") && !strcmp(state, "Signal not found");
        disabled |= !strcmp(path, "counter_tb.dut.odd") && !strcmp(state, "Disabled signal stop");
        g_free(path);
        g_free(state);
    }
    return armed && missing && disabled;
}

/* EN: Seed signal rules in profile for the current fixture, returning success.
 * RU: Подготавливает правила текущего теста в profile и возвращает результат. */
static gboolean seed_signal_rules(const gchar *profile)
{
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    g_checksum_update(checksum, (const guchar *)"\0", 1);
    g_checksum_update(checksum, (const guchar *)fixture_source, strlen(fixture_source));
    gchar *name = g_strconcat(g_checksum_get_string(checksum), ".conf.signal-stops", NULL);
    gchar *directory = g_build_filename(profile, "insight", "verilog-debug", NULL);
    gchar *path = g_build_filename(directory, name, NULL);
    const gchar *rules[] = {"counter_tb.dut.clk\tmask\tz\t1",
        "counter_tb.dut.missing\tchange\t\t1", "counter_tb.dut.odd\tchange\t\t0"};
    GKeyFile *config = g_key_file_new();
    g_key_file_set_string_list(config, "signals", "stops", rules, G_N_ELEMENTS(rules));
    gboolean saved = !g_mkdir_with_parents(directory, 0700) && g_key_file_save_to_file(config, path, NULL);
    g_key_file_unref(config);
    g_checksum_free(checksum);
    g_free(name);
    g_free(directory);
    g_free(path);
    return saved;
}

