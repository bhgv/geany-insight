/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Signal filtering, viewport actions and waveform search.
 * RU: Фильтрация сигналов, навигация и поиск по диаграмме.
 */

/* EN: Rebuild viewer's signal choices using its current filter.
 * RU: Перестраивает список сигналов viewer с учётом текущего фильтра. */
static void refresh_selector(Viewer *viewer)
{
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(viewer->filter));
    gchar *folded = g_utf8_casefold(query, -1);
    GtkComboBoxText *selector = GTK_COMBO_BOX_TEXT(viewer->selector);
    viewer->rebuilding = TRUE;
    gtk_combo_box_text_remove_all(selector);
    for (guint i = 0; i < viewer->capture->signals->len; i++)
    {
        Signal *signal = viewer->capture->signals->pdata[i];
        gchar *name = g_utf8_casefold(signal->name, -1);
        if (strstr(name, folded))
        {
            gchar id[16];
            g_snprintf(id, sizeof id, "%u", i);
            gtk_combo_box_text_append(selector, id, signal->name);
        }
        g_free(name);
    }
    gchar active_id[16];
    g_snprintf(active_id, sizeof active_id, "%d", viewer->active);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(selector), active_id);
    viewer->rebuilding = FALSE;
    g_free(folded);
}

/* EN: Select and reveal hierarchy in waveform window. Return false if it is absent.
 * RU: Выбирает и показывает hierarchy в окне window. Возвращает false, если сигнала нет. */
gboolean insight_waveforms_select_track(GtkWindow *window, const gchar *hierarchy)
{
    if (!window || !gtk_widget_get_visible(GTK_WIDGET(window)))
        return FALSE;
    Viewer *viewer = g_object_get_data(G_OBJECT(window), "insight-wave-viewer");
    if (!viewer)
        return FALSE;

    for (guint i = 0; i < viewer->capture->signals->len; i++)
    {
        const Signal *signal = viewer->capture->signals->pdata[i];
        if (g_strcmp0(signal->name, hierarchy))
            continue;

        gtk_entry_set_text(GTK_ENTRY(viewer->filter), "");
        activate_signal(viewer, (gint)i);
        refresh_selector(viewer);
        gtk_window_present(window);
        return TRUE;
    }
    return FALSE;
}

/* EN: Apply the text filter from entry to the signal selector in Viewer data.
 * RU: Применяет фильтр entry к списку сигналов Viewer из data. */
static void filter_changed(GtkSearchEntry *entry, gpointer data)
{
    (void)entry;
    refresh_selector(data);
}

/* EN: Apply the filter in entry and select the first match in Viewer data.
 * RU: Применяет фильтр entry и выбирает первое совпадение в Viewer из data. */
static void filter_activate(GtkEntry *entry, gpointer data)
{
    (void)entry;
    Viewer *viewer = data;
    refresh_selector(viewer);
    gtk_combo_box_set_active(GTK_COMBO_BOX(viewer->selector), 0);
}

/* EN: Apply the stepping, zoom or pan action from button to Viewer data.
 * RU: Выполняет шаг, масштабирование или прокрутку Viewer из data по действию button. */
static void wave_action(GtkButton *button, gpointer data)
{
    Viewer *viewer = data;
    Signal *signal = selected(viewer);
    if (!signal)
        return;
    gint action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
    Trace *trace = signal->trace;
    guint64 time;
    if ((action == 1 || action == -1) && wave_adjacent_event(trace, viewer->cursor, action == 1, &time))
        viewer->cursor = time;
    if (action == 6 || action == -6)
        wave_step_visible(viewer, action == 6);
    guint64 total = MAX(G_GUINT64_CONSTANT(1), viewer->capture->end);
    if (action == 2)
        viewer->span = MAX(G_GUINT64_CONSTANT(1), viewer->span / 2);
    if (action == 3)
        viewer->span = MIN(total, viewer->span * 2);
    guint64 step = MAX(G_GUINT64_CONSTANT(1), viewer->span / 2);
    if (action == 4)
        viewer->start -= MIN(viewer->start, step);
    else if (action == 5)
        viewer->start = MIN(total - viewer->span, viewer->start + step);
    else
        viewer->start = MIN(total - viewer->span,
                            viewer->cursor > viewer->span / 2 ? viewer->cursor - viewer->span / 2 : 0);
    update(viewer);
}

typedef enum
{
    WAVE_MATCH_VALUE,
    WAVE_MATCH_RISING,
    WAVE_MATCH_FALLING
} WaveMatch;

/* EN: Find a match in trace strictly after cursor. mode selects an edge or value predicate, value is used in value mode, and time receives the tick.
 * RU: Ищет совпадение в trace строго после cursor. mode задаёт фронт или значение, value задаёт искомое значение, time получает такт. */
static gboolean wave_find_match(const Trace *trace, guint64 cursor, WaveMatch mode, const gchar *value,
                                guint64 *time)
{
    guint start = upper_bound(trace, cursor);
    for (guint i = start; i < trace->edges->len; i++)
    {
        const Edge *edge = &g_array_index(trace->edges, Edge, i);
        gboolean matches = FALSE;
        if (mode == WAVE_MATCH_VALUE)
            matches = !strcmp(edge->value, value);
        else if (i && trace->width == 1)
        {
            const Edge *previous = &g_array_index(trace->edges, Edge, i - 1);
            gchar from = mode == WAVE_MATCH_RISING ? '0' : '1';
            gchar to = mode == WAVE_MATCH_RISING ? '1' : '0';
            matches = previous->value[0] == from && edge->value[0] == to;
        }
        if (matches)
        {
            *time = edge->time;
            return TRUE;
        }
    }
    return FALSE;
}

/* Move the waveform cursor to the next matching value or logical edge. */
/* Перемещает курсор диаграммы к следующему значению или логическому фронту. */
/* EN: Open the tool from button for the current view or simulation in data.
 * RU: Открывает инструмент по button для текущего просмотра или симуляции из data. */
static void wave_search_condition(GtkButton *button, gpointer data)
{
    (void)button;
    Viewer *viewer = data;
    Signal *signal = selected(viewer);
    if (!signal)
        return;

    GtkWidget *dialog =
        gtk_dialog_new_with_buttons("Find Signal Condition", GTK_WINDOW(viewer->window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT, "_Close",
                                    GTK_RESPONSE_CLOSE, "_Find Next", GTK_RESPONSE_ACCEPT, NULL);
    simulation_dialog_tools(
        dialog,
        "Choose a signal and a bit pattern, then search from the waveform cursor. The condition "
        "uses values recorded in the VCD.",
        "Выберите сигнал и битовый шаблон, затем начните поиск от курсора. Условие проверяется "
        "по значениям в VCD.");
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *heading = gtk_label_new(signal->name);
    gtk_label_set_ellipsize(GTK_LABEL(heading), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 4);

    GtkWidget *mode = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode), "Exact binary value (0, 1, x, z)");
    if (signal->trace->width == 1)
    {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode), "Rising edge (0 to 1)");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode), "Falling edge (1 to 0)");
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
    gtk_box_pack_start(GTK_BOX(box), mode, FALSE, FALSE, 4);
    GtkWidget *value = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(value), (gint)signal->trace->width);
    gtk_entry_set_activates_default(GTK_ENTRY(value), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_box_pack_start(GTK_BOX(box), value, FALSE, FALSE, 4);
    GtkWidget *status = gtk_label_new("Enter every bit of the selected signal for an exact match.");
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 4);
    gtk_widget_show_all(dialog);

    while (insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        WaveMatch match = (WaveMatch)gtk_combo_box_get_active(GTK_COMBO_BOX(mode));
        gchar *bits = g_ascii_strdown(gtk_entry_get_text(GTK_ENTRY(value)), -1);
        guint64 time;
        if (match == WAVE_MATCH_VALUE &&
            (strlen(bits) != signal->trace->width || strspn(bits, "01xz") != strlen(bits)))
            gtk_label_set_text(GTK_LABEL(status), "Enter exactly the signal width in bits (0, 1, x, z).");
        else if (!wave_find_match(signal->trace, viewer->cursor, match, bits, &time))
            gtk_label_set_text(GTK_LABEL(status), "No later matching event in the capture.");
        else
        {
            viewer->cursor = time;
            guint64 total = MAX(G_GUINT64_CONSTANT(1), viewer->capture->end);
            guint64 half = viewer->span / 2;
            viewer->start = MIN(total - viewer->span, time > half ? time - half : 0);
            update(viewer);
            gchar *message = g_strdup_printf("Match at tick %" G_GUINT64_FORMAT, time);
            gtk_label_set_text(GTK_LABEL(status), message);
            g_free(message);
        }
        g_free(bits);
    }
    gtk_widget_destroy(dialog);
}

/* EN: Toggle the selected track in Viewer data. button is unused.
 * RU: Закрепляет или открепляет выбранную дорожку Viewer из data. button не используется. */
static void pin_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    Viewer *viewer = data;
    gint active = viewer->active;
    if (active < 0)
        return;
    gint index = pinned_index(viewer, active);
    if (index >= 0)
        g_array_remove_index(viewer->tracks, (guint)index);
    else if (viewer->tracks->len < WAVE_TRACKS)
        g_array_append_val(viewer->tracks, active);
    update(viewer);
}

/* EN: Set the value radix in Viewer data from box.
 * RU: Устанавливает формат чисел Viewer из data по выбору в box. */
static void radix_changed(GtkComboBox *box, gpointer data)
{
    Viewer *viewer = data;
    viewer->hexadecimal = gtk_combo_box_get_active(box) == 1;
    update(viewer);
}

/* EN: Set or clear the marker designated by button in Viewer data.
 * RU: Ставит или снимает отметку Viewer из data, заданную кнопкой button. */
static void marker_clicked(GtkButton *button, gpointer data)
{
    Viewer *viewer = data;
    gint marker = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "marker"));
    if (marker < 0)
        viewer->marker_set[0] = viewer->marker_set[1] = FALSE;
    else
    {
        viewer->marker[marker] = viewer->cursor;
        viewer->marker_set[marker] = TRUE;
    }
    update(viewer);
}
