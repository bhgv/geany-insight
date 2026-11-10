/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Memory value comparison and calculator controls.
 * RU: Сравнение значений памяти и управление калькулятором.
 */

/* EN: Show left and right in page's dump base, aligned and marked where digits differ.
 * RU: Показывает left и right в системе дампа page, выравнивая и выделяя разные разряды. */
static void memory_calculator_compare(MemoryPage *page, uint64_t left, uint64_t right)
{
    const guint bases[] = {16, 10, 2};
    const guint groups[] = {4, 3, 8};
    gint index = CLAMP(gtk_combo_box_get_active(page->base), 0, 2);
    guint group = groups[index];
    gchar digits[2][MEMORY_VALUE_TEXT_SIZE];
    memory_word_format(digits[0], left, bases[index], 0);
    memory_word_format(digits[1], right, bases[index], 0);
    guint lengths[] = {strlen(digits[0]), strlen(digits[1])};
    guint width = MAX(lengths[0], lengths[1]);
    guint padding[] = {width - lengths[0], width - lengths[1]};

    GString *markup = g_string_new("<tt>");
    for (guint row = 0; row < 2; ++row)
    {
        if (row)
            g_string_append_c(markup, '\n');
        for (guint col = 0; col < width; ++col)
        {
            if (col && (width - col) % group == 0)
                g_string_append_c(markup, ' ');
            gchar first = col < padding[0] ? '0' : digits[0][col - padding[0]];
            gchar second = col < padding[1] ? '0' : digits[1][col - padding[1]];
            gchar digit = row ? second : first;
            if (first != second)
                g_string_append_printf(markup, "<b><u>%c</u></b>", digit);
            else
                g_string_append_c(markup, digit);
        }
    }
    g_string_append(markup, "</tt>");
    gtk_label_set_markup(GTK_LABEL(page->result), markup->str);
    g_string_free(markup, TRUE);
}

/* EN: Calculate the selected operation for page when control changes.
 * RU: Вычисляет выбранную операцию для page после изменения control. */
static void memory_calculator_update(GtkWidget *control, MemoryPage *page)
{
    (void)control;
    gint operation = gtk_combo_box_get_active(page->operation);
    guint64 left, right = 0;
    const gchar *left_text = gtk_entry_get_text(page->left);
    const gchar *right_text = gtk_entry_get_text(page->right);
    gboolean unary = operation == MEMORY_NOT;
    gtk_widget_set_sensitive(GTK_WIDGET(page->right), !unary);

    if (!g_ascii_string_to_unsigned(left_text, 10, 0, G_MAXUINT64, &left, NULL) ||
        (!unary && !g_ascii_string_to_unsigned(right_text, 10, 0, G_MAXUINT64, &right, NULL)))
    {
        gtk_label_set_text(GTK_LABEL(page->result), insight_tr("Enter unsigned decimal operands."));
        return;
    }
    if (operation == MEMORY_COMPARE)
    {
        memory_calculator_compare(page, left, right);
        return;
    }
    uint64_t result;
    if (!memory_calculate(operation, left, right, &result))
    {
        gtk_label_set_text(GTK_LABEL(page->result), insight_tr("Division by zero."));
        return;
    }
    gchar decimal[MEMORY_VALUE_TEXT_SIZE], hex[MEMORY_VALUE_TEXT_SIZE];
    memory_word_format(decimal, result, 10, 64);
    memory_word_format(hex, result, 16, 64);
    gchar *text = g_strdup_printf("%s  (0x%s)", decimal, hex);
    gtk_label_set_text(GTK_LABEL(page->result), text);
    g_free(text);
}

/* EN: Remember the operand entry focused by event for subsequent imports into page.
 * RU: Запоминает поле entry, получившее фокус event, для переноса чисел в page. */
static gboolean memory_calculator_focus(GtkEntry *entry, GdkEventFocus *event, MemoryPage *page)
{
    (void)event;
    page->target = entry;
    return FALSE;
}

/* EN: Read the address or value under event in view into page's open calculator.
 * Return true if the double-click was handled.
 * RU: Передаёт адрес или значение под event в view в открытый калькулятор page.
 * Возвращает true, если двойной щелчок обработан. */
static gboolean memory_calculator_pick(GtkTextView *view, GdkEventButton *event, MemoryPage *page)
{
    if (event->type != GDK_2BUTTON_PRESS || event->button != 1 ||
        !gtk_widget_get_visible(page->calculator) ||
        event->window != gtk_text_view_get_window(view, GTK_TEXT_WINDOW_TEXT))
        return FALSE;

    gint x, y;
    GtkTextIter point;
    gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_TEXT, event->x, event->y, &x, &y);
    if (!gtk_text_view_get_iter_at_location(view, &point, x, y) ||
        gtk_text_iter_get_line(&point) == 0)
        return FALSE;

    GtkTextIter start = point;
    gtk_text_iter_set_line_offset(&start, 0);
    GtkTextIter end = start;
    if (!gtk_text_iter_ends_line(&end))
        gtk_text_iter_forward_to_line_end(&end);
    gchar *line = gtk_text_buffer_get_text(page->buffer, &start, &end, FALSE);
    gchar *address_end = strchr(line, ' ');
    gboolean handled = FALSE;
    if (address_end)
    {
        gchar *value = address_end;
        while (*value == ' ')
            ++value;
        gchar *value_end = value;
        while (g_ascii_isxdigit(*value_end))
            ++value_end;

        gint offset = gtk_text_iter_get_line_offset(&point);
        gchar *digits = NULL;
        if (offset < address_end - line)
        {
            *address_end = '\0';
            digits = line;
        }
        else if (offset >= value - line && offset < value_end - line)
        {
            *value_end = '\0';
            digits = value;
        }

        const guint bases[] = {16, 10, 2};
        gint selection = gtk_combo_box_get_active(page->base);
        guint64 number;
        if (digits && g_ascii_string_to_unsigned(digits, bases[CLAMP(selection, 0, 2)],
                                                 0, G_MAXUINT64, &number, NULL))
        {
            gchar text[MEMORY_VALUE_TEXT_SIZE];
            memory_word_format(text, number, 10, 64);
            gtk_entry_set_text(gtk_widget_get_sensitive(GTK_WIDGET(page->target))
                               ? page->target : page->left, text);
            handled = TRUE;
        }
    }
    g_free(line);
    return handled;
}

/* EN: Show or hide page's calculator according to toggle.
 * RU: Показывает или скрывает калькулятор page по состоянию toggle. */
static void memory_calculator_toggle(GtkToggleButton *toggle, MemoryPage *page)
{
    gtk_widget_set_visible(page->calculator, gtk_toggle_button_get_active(toggle));
}

/* EN: Add calculator controls to box and bind them to page.
 * RU: Добавляет элементы калькулятора в box и привязывает их к page. */
static void memory_calculator_add(GtkBox *box, MemoryPage *page)
{
    page->calculator = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(box, page->calculator, FALSE, FALSE, 0);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(page->calculator), row, FALSE, FALSE, 0);
    page->left = GTK_ENTRY(gtk_entry_new());
    page->right = GTK_ENTRY(gtk_entry_new());
    page->target = page->left;
    page->operation = GTK_COMBO_BOX(gtk_combo_box_text_new());
    const gchar *operations[] = {
        "+", "−", "×", "÷", "AND", "OR", "XOR", "NOT", insight_tr("Compare digits")
    };
    for (guint i = 0; i < G_N_ELEMENTS(operations); ++i)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(page->operation), operations[i]);

    gtk_entry_set_placeholder_text(page->left, insight_tr("First operand"));
    gtk_entry_set_placeholder_text(page->right, insight_tr("Second operand"));
    gtk_entry_set_max_length(page->left, 20);
    gtk_entry_set_max_length(page->right, 20);
    gtk_combo_box_set_active(page->operation, MEMORY_ADD);
    gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(page->left), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(page->operation), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(page->right), TRUE, TRUE, 0);
    page->result = gtk_label_new(NULL);
    gtk_label_set_selectable(GTK_LABEL(page->result), TRUE);
    GtkWidget *result_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(result_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(result_scroll), TRUE);
    gtk_container_add(GTK_CONTAINER(result_scroll), page->result);
    gtk_box_pack_start(GTK_BOX(page->calculator), result_scroll, FALSE, FALSE, 0);
    g_signal_connect(page->left, "focus-in-event", G_CALLBACK(memory_calculator_focus), page);
    g_signal_connect(page->right, "focus-in-event", G_CALLBACK(memory_calculator_focus), page);
    g_signal_connect(page->left, "changed", G_CALLBACK(memory_calculator_update), page);
    g_signal_connect(page->right, "changed", G_CALLBACK(memory_calculator_update), page);
    g_signal_connect(page->operation, "changed", G_CALLBACK(memory_calculator_update), page);
    memory_calculator_update(NULL, page);

    gtk_widget_show_all(page->calculator);
    gtk_widget_set_no_show_all(page->calculator, TRUE);
    gtk_widget_hide(page->calculator);
}
