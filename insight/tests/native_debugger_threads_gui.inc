/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify real worker/thread/frame context across locals, watches, expression trees, memory and source navigation.
 * RU: Проверяет настоящий контекст потоков/кадров в переменных, наблюдениях, дереве, памяти и переходах к исходнику. */

/* EN: Activate function in named worker's all-thread stack within window and return TRUE when dispatched.
 * The returned thread/frame strings belong to the caller; they identify the requested confirmed context.
 * RU: Активирует function в общем стеке потока worker окна window; TRUE означает отправку запроса.
 * Строки thread/frame принадлежат вызывающему и обозначают запрошенный контекст. */
static gboolean threads_gui_frame(GtkWidget *window, const gchar *worker, const gchar *function,
                                  gchar **thread, gchar **frame)
{
    GtkWidget *tree = find(window, "insight-gdb-all-thread-stacks");
    if (!tree || !gtk_widget_is_sensitive(tree))
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    GtkTreeIter root, child;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &root); valid;
         valid = gtk_tree_model_iter_next(model, &root))
    {
        gchar *label = NULL;
        gtk_tree_model_get(model, &root, 0, &label, -1);
        gboolean matches = label && strstr(label, worker);
        g_free(label);
        if (!matches)
            continue;
        for (gboolean item = gtk_tree_model_iter_children(model, &child, &root); item;
             item = gtk_tree_model_iter_next(model, &child))
        {
            gchar *name = NULL;
            gtk_tree_model_get(model, &child, 1, &name, -1);
            matches = !g_strcmp0(name, function);
            g_free(name);
            if (!matches)
                continue;
            g_free(*thread);
            g_free(*frame);
            gtk_tree_model_get(model, &child, 5, thread, 6, frame, -1);
            GtkTreePath *path = gtk_tree_model_get_path(model, &child);
            gtk_tree_view_row_activated(GTK_TREE_VIEW(tree), path, NULL);
            gtk_tree_path_free(path);
            return TRUE;
        }
    }
    return FALSE;
}

/* EN: Check window's confirmed global status and locals/watches for thread/frame/value/TLS.
 * RU: Проверяет подтверждённые статус и переменные/наблюдения window для thread/frame/value/TLS. */
static gboolean threads_gui_context(GtkWidget *window, const gchar *thread, const gchar *frame,
                                    const gchar *value, const gchar *tls)
{
    GtkWidget *status = find(window, "insight-gdb-stop-reason");
    gchar *expected = g_strdup_printf("Thread #%s / frame #%s", thread, frame);
    gboolean matches = status && strstr(gtk_label_get_text(GTK_LABEL(status)), expected) &&
        has_local(window, "value", value);
    g_free(expected);
    expected = g_strdup_printf("value = %s", value);
    matches = matches && find(window, expected);
    g_free(expected);
    expected = g_strdup_printf("tls_value = %s", tls);
    matches = matches && find(window, expected) && find(window, "shared_value = 7");
    g_free(expected);
    GtkWidget *resume = find(window, "Continue");
    return matches && resume && gtk_widget_is_sensitive(resume);
}

/* EN: Check scalar expression root value in window's actual variable-object model.
 * RU: Проверяет значение скалярного корня в настоящей модели объектов переменных окна window. */
static gboolean threads_gui_value(GtkWidget *window, const gchar *value)
{
    GtkWidget *tree = find(window, "insight-gdb-value-tree");
    if (!tree || !gtk_widget_is_sensitive(tree))
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter_first(model, &row))
        return FALSE;
    gchar *actual = NULL;
    gtk_tree_model_get(model, &row, 2, &actual, -1);
    gboolean matches = !g_strcmp0(actual, value);
    g_free(actual);
    return matches;
}

/* EN: Read the second worker's numeric counter watch from window into value, returning FALSE until available.
 * RU: Читает числовое наблюдение счётчика второго потока окна window в value; FALSE означает отсутствие значения. */
static gboolean threads_gui_counter(GtkWidget *window, guint64 *value)
{
    GtkWidget *view = find(window, "progress[1] = ");
    if (!GTK_IS_TEXT_VIEW(view))
        return FALSE;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    gchar *text = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
    const gchar *prefix = "progress[1] = ";
    gchar *start = strstr(text, prefix);
    gchar *end = NULL;
    guint64 number = start ? g_ascii_strtoull(start + strlen(prefix), &end, 10) : 0;
    gboolean valid = start && end > start + strlen(prefix) && (!*end || *end == '\n');
    if (valid)
        *value = number;
    g_free(text);
    return valid;
}

/* EN: Return TRUE once window's collected tree contains nonempty stacks for both named workers.
 * RU: Возвращает TRUE, когда собранное дерево window содержит непустые стеки обоих именованных потоков. */
static gboolean threads_gui_stacks_ready(GtkWidget *window)
{
    GtkWidget *tree = find(window, "insight-gdb-all-thread-stacks");
    if (!tree || !gtk_widget_is_sensitive(tree))
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    GtkTreeIter root;
    guint found = 0;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &root); valid;
         valid = gtk_tree_model_iter_next(model, &root))
    {
        gchar *label = NULL;
        gtk_tree_model_get(model, &root, 0, &label, -1);
        if (label && (strstr(label, "worker-one") || strstr(label, "worker-two")) &&
            gtk_tree_model_iter_n_children(model, &root) > 0)
            found++;
        g_free(label);
    }
    return found == 2;
}

/* EN: Drive window/tabs through two workers and three nested frame contexts; source and memory must agree.
 * Capture the real views only after raising the debugger. Finish without resuming worker execution.
 * RU: Проверяет window/tabs на двух потоках и трёх вложенных кадрах; исходник и память должны совпадать.
 * Снимает настоящие представления после подъёма окна. Завершает без возобновления потоков. */
static gboolean native_threads_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    static gchar *thread, *frame;
    static guint watch;
    static guint64 other_counter;
    static const gchar *expressions[] = {"value", "tls_value", "shared_value", "progress[1]"};
    GtkWidget *policy = find(window, "insight-gdb-thread-policy");
    if (phase == 1)
    {
        GtkWidget *location = find(window, "Function or file:line");
        if (location && gtk_widget_is_sensitive(location))
        {
            gtk_entry_set_text(GTK_ENTRY(location), "capture_threads");
            if (click(window, "Add Breakpoint"))
                phase = 2;
        }
    }
    else if (phase == 2)
    {
        GtkWidget *entry = find(window, "GDB watch expression");
        if (entry && gtk_widget_is_sensitive(entry))
        {
            if (watch < G_N_ELEMENTS(expressions))
            {
                gtk_entry_set_text(GTK_ENTRY(entry), expressions[watch++]);
                g_assert_true(click(window, "Add Watch"));
            }
            else if (click(window, "Run"))
                phase = 3;
        }
    }
    else if (phase == 3 && gtk_widget_is_sensitive(find(window, "Continue")))
    {
        GtkWidget *page = find(window, "insight-gdb-all-thread-stacks");
        while (page && gtk_widget_get_parent(page) != tabs)
            page = gtk_widget_get_parent(page);
        if (page)
        {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), gtk_notebook_page_num(GTK_NOTEBOOK(tabs), page));
            phase = 4;
        }
    }
    else if (phase == 4 && threads_gui_frame(window, "worker-one", "worker_leaf", &thread, &frame))
        phase = 5;
    else if (phase == 5 && threads_gui_context(window, thread, frame, "111", "1007"))
    {
        gtk_entry_set_text(GTK_ENTRY(find(window, "insight-gdb-inspect-expression")), "value");
        if (click(window, "Inspect Expression"))
            phase = 6;
    }
    else if (phase == 6 && threads_gui_value(window, "111"))
    {
        gtk_entry_set_text(GTK_ENTRY(find(window, "insight-gdb-memory-address")), "&value");
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(find(window, "insight-gdb-memory-size")), 4);
        if (click(window, "Read Memory"))
            phase = 7;
    }
    else if (phase == 7 && find(window, G_BYTE_ORDER == G_LITTLE_ENDIAN ? "6f 00 00 00" : "00 00 00 6f") &&
             threads_gui_frame(window, "worker-two", "worker_leaf", &thread, &frame))
        phase = 8;
    else if (phase == 8 && threads_gui_context(window, thread, frame, "211", "2007"))
    {
        g_assert_false(threads_gui_value(window, "111"));
        gtk_entry_set_text(GTK_ENTRY(find(window, "insight-gdb-inspect-expression")), "value");
        if (click(window, "Inspect Expression"))
            phase = 9;
    }
    else if (phase == 9 && threads_gui_value(window, "211"))
    {
        gtk_entry_set_text(GTK_ENTRY(find(window, "insight-gdb-memory-address")), "&value");
        if (click(window, "Read Memory"))
            phase = 10;
    }
    else if (phase == 10 && find(window, G_BYTE_ORDER == G_LITTLE_ENDIAN ? "d3 00 00 00" : "00 00 00 d3") &&
             threads_gui_frame(window, "worker-one", "thread_checkpoint", &thread, &frame))
        phase = 11;
    else if (phase == 11 && threads_gui_context(window, thread, frame, "122", "1007") &&
             gtk_widget_is_sensitive(policy))
    {
        g_assert_true(gtk_combo_box_set_active_id(GTK_COMBO_BOX(policy), "step"));
        phase = 16;
    }
    else if (phase == 16 && gtk_widget_is_sensitive(policy) &&
             !g_strcmp0(gtk_combo_box_get_active_id(GTK_COMBO_BOX(policy)), "step") &&
             threads_gui_counter(window, &other_counter) && click(window, "Step Over"))
        phase = 17;
    else if (phase == 17 && threads_gui_context(window, thread, frame, "122", "1007") &&
             strstr(gtk_label_get_text(GTK_LABEL(find(window, "insight-gdb-stop-reason"))), "Step complete"))
    {
        guint64 now = 0;
        g_assert_true(threads_gui_counter(window, &now));
        g_assert_cmpuint(now, ==, other_counter);
        g_assert_true(gtk_combo_box_set_active_id(GTK_COMBO_BOX(policy), "off"));
        phase = 18;
    }
    else if (phase == 18 && gtk_widget_is_sensitive(policy) &&
             !g_strcmp0(gtk_combo_box_get_active_id(GTK_COMBO_BOX(policy)), "off"))
    {
        GtkWidget *page = find(window, "insight-gdb-all-thread-stacks");
        while (page && gtk_widget_get_parent(page) != tabs)
            page = gtk_widget_get_parent(page);
        g_assert_nonnull(page);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), gtk_notebook_page_num(GTK_NOTEBOOK(tabs), page));
        if (threads_gui_frame(window, "worker-one", "worker_entry", &thread, &frame))
            phase = 12;
    }
    else if (phase == 12 && threads_gui_context(window, thread, frame, "101", "1007") &&
             threads_gui_frame(window, "worker-one", "worker_leaf", &thread, &frame))
        phase = 13;
    else if (phase == 13 && threads_gui_context(window, thread, frame, "111", "1007"))
    {
        GtkWidget *tree = find(window, "insight-gdb-all-thread-stacks");
        GtkWidget *page = gtk_widget_get_parent(gtk_widget_get_parent(tree));
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), gtk_notebook_page_num(GTK_NOTEBOOK(tabs), page));
        if (click(window, "insight-gdb-refresh-stacks"))
            phase = 20;
    }
    else if (phase == 20 && threads_gui_context(window, thread, frame, "111", "1007") &&
             threads_gui_stacks_ready(window))
    {
        gtk_tree_view_expand_all(GTK_TREE_VIEW(find(window, "insight-gdb-all-thread-stacks")));
        gtk_window_present(GTK_WINDOW(window));
        gdk_window_raise(gtk_widget_get_window(window));
        gtk_widget_queue_draw(window);
        phase = 14;
    }
    else if (phase == 14)
    {
        native_exit_gui_capture(window, "INSIGHT_GDB_THREADS_CAPTURE");
        GtkWidget *view = find(window, "insight-gdb-frame-variables");
        GtkWidget *page = gtk_widget_get_parent(gtk_widget_get_parent(view));
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), gtk_notebook_page_num(GTK_NOTEBOOK(tabs), page));
        gtk_window_present(GTK_WINDOW(window));
        gdk_window_raise(gtk_widget_get_window(window));
        gtk_widget_queue_draw(window);
        phase = 15;
    }
    else if (phase == 15)
    {
        native_exit_gui_capture(window, "INSIGHT_GDB_THREAD_VALUES_CAPTURE");
        GtkWidget *page = policy;
        while (page && gtk_widget_get_parent(page) != tabs)
            page = gtk_widget_get_parent(page);
        g_assert_nonnull(page);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), gtk_notebook_page_num(GTK_NOTEBOOK(tabs), page));
        g_assert_true(gtk_combo_box_set_active_id(GTK_COMBO_BOX(policy), "step"));
        phase = 19;
    }
    else if (phase == 19 && gtk_widget_is_sensitive(policy) &&
             !g_strcmp0(gtk_combo_box_get_active_id(GTK_COMBO_BOX(policy)), "step"))
    {
        gtk_window_present(GTK_WINDOW(window));
        gdk_window_raise(gtk_widget_get_window(window));
        gtk_widget_queue_draw(window);
        phase = 21;
    }
    else if (phase == 21)
    {
        native_exit_gui_capture(window, "INSIGHT_GDB_THREAD_POLICY_CAPTURE");
        g_print("C/C++ GTK: confirmed scheduler policy, step isolation and non-executing stack refresh passed.\n");
        g_print("C/C++ GTK: worker/frame locals, TLS, shared globals, watches, value trees, memory and confirmed backtrace navigation passed.\n");
        g_free(thread);
        g_free(frame);
        passed = TRUE;
        gtk_widget_destroy(window);
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
