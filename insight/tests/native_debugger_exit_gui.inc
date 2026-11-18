/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Drive real GDB exit/relaunch states and capture the production debugger's human status.
 * RU: Проверяет настоящие выходы/повторные запуски GDB и снимает понятный статус окна отладчика. */

/* EN: Capture window to the path in variable when requested. Reject mostly black, occluded captures
 * in the light-theme CI fixture; save only a real, readable production-window JPEG.
 * RU: Снимает window в путь из variable по запросу. Отклоняет преимущественно чёрные перекрытые снимки
 * в CI со светлой темой; сохраняет только читаемый JPEG настоящего окна. */
static void native_exit_gui_capture(GtkWidget *window, const gchar *variable)
{
    const gchar *path = g_getenv(variable);
    if (!path || !*path)
        return;
    GdkWindow *surface = gtk_widget_get_window(window);
    GdkPixbuf *pixels = gdk_pixbuf_get_from_window(surface, 0, 0, gdk_window_get_width(surface),
                                                   gdk_window_get_height(surface));
    g_assert_nonnull(pixels);
    guint dark = 0, samples = 0;
    guchar *bytes = gdk_pixbuf_get_pixels(pixels);
    gint stride = gdk_pixbuf_get_rowstride(pixels);
    gint channels = gdk_pixbuf_get_n_channels(pixels);
    for (gint y = 0; y < gdk_pixbuf_get_height(pixels); y += 8)
        for (gint x = 0; x < gdk_pixbuf_get_width(pixels); x += 8)
        {
            const guchar *sample = bytes + y * stride + x * channels;
            dark += sample[0] < 8 && sample[1] < 8 && sample[2] < 8;
            samples++;
        }
    g_assert_cmpuint(dark * 2, <, samples);
    GError *error = NULL;
    g_assert_true(gdk_pixbuf_save(pixels, path, "jpeg", &error, "quality", "88", NULL));
    g_assert_no_error(error);
    g_object_unref(pixels);
}

/* EN: Drive window/tabs through entry stop, profile save/restore and Restart using phases 8..12.
 * Capture the real launch form after its allocation; return to the existing exit tests after Continue.
 * RU: Проверяет в window/tabs вход, сохранение/восстановление профиля и Restart через фазы 8..12.
 * Снимает настоящую форму после размещения; после Continue возвращается к проверкам завершения. */
static gboolean native_entry_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    GtkWidget *option = find(window, "insight-gdb-stop-at-main");
    GtkWidget *resume = find(window, "Continue");
    GtkWidget *run = find(window, "Run");
    if (phase == 13 && find(window, "Lua observer loaded.") && gtk_widget_is_sensitive(run))
    {
        gtk_entry_set_text(GTK_ENTRY(find(window, "insight-gdb-launch-arguments")), "0");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(option), TRUE);
        g_assert_true(click(window, "Run"));
        phase = 8;
    }
    else if (phase == 8 && gtk_widget_is_sensitive(resume))
    {
        GtkWidget *page = gtk_widget_get_parent(option);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), gtk_notebook_page_num(GTK_NOTEBOOK(tabs), page));
        gtk_window_present(GTK_WINDOW(window));
        gdk_window_raise(gtk_widget_get_window(window));
        gtk_widget_queue_draw(window);
        phase = 9;
    }
    else if (phase == 9 && gtk_widget_is_sensitive(resume))
    {
        native_exit_gui_capture(window, "INSIGHT_GDB_ENTRY_CAPTURE");
        g_assert_true(click(window, "Save debugger profile"));
        g_assert_true(click(window, "Stop"));
        phase = 10;
    }
    else if (phase == 10 && gtk_widget_is_sensitive(run))
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(option), FALSE);
        g_assert_true(click(window, "Restore debugger profile"));
        g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(option)));
        phase = 12;
    }
    else if (phase == 12 && gtk_widget_is_sensitive(find(window, "Restart")))
    {
        g_assert_true(click(window, "Restart"));
        phase = 11;
    }
    else if (phase == 11 && gtk_widget_is_sensitive(resume))
    {
        GtkWidget *points = find(window, "insight-gdb-breakpoints");
        g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(points)), NULL),
                        ==, 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(option), FALSE);
        g_assert_true(click(window, "Continue"));
        g_print(
            "C/C++ GTK: stop at main, temporary breakpoint cleanup, profile restore and Restart passed.\n");
        phase = 2;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: End window's real GDB while paused with populated locals; verify terminal cleanup in phases 14..15.
 * Return CONTINUE while asynchronous shutdown drains, REMOVE after the assertions and GTK exit.
 * RU: Завершает настоящий GDB окна window на паузе с переменными; проверяет очистку в фазах 14..15.
 * Возвращает CONTINUE до завершения асинхронного отключения, REMOVE после проверок и выхода GTK. */
static gboolean native_backend_gui_step(GtkWidget *window)
{
    GtkWidget *command = find(window, "GDB command, e.g. break main");
    GtkWidget *resume = find(window, "Continue");
    GtkWidget *locals = find(window, "insight-gdb-frame-variables");
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(locals));
    if (phase == 14 && gtk_widget_is_sensitive(resume) &&
        gtk_widget_is_sensitive(command) && gtk_tree_model_iter_n_children(model, NULL) > 0)
    {
        gtk_entry_set_text(GTK_ENTRY(command), "quit");
        g_signal_emit_by_name(command, "activate");
        phase = 15;
    }
    else if (phase == 15)
    {
        const gchar *status = gtk_label_get_text(GTK_LABEL(find(window, "insight-gdb-stop-reason")));
        if (!strstr(status, "GDB session ended."))
            return G_SOURCE_CONTINUE;
        const gchar *inactive[] = {"Run", "Restart", "Continue", "Pause", "Stop", "Open Source"};
        for (guint i = 0; i < G_N_ELEMENTS(inactive); i++)
            g_assert_false(gtk_widget_is_sensitive(find(window, inactive[i])));
        g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 0);
        g_assert_false(gtk_widget_is_sensitive(command));
        g_print("C/C++ GTK: GDB shutdown from a populated paused context clears stale data and disables execution.\n");
        passed = TRUE;
        gtk_widget_destroy(window);
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Advance normal/nonzero/signal/relaunch checks in window using shared phase; tabs selects Console
 * for the screenshot. End GTK only after status, inactive controls and empty stopped context agree.
 * RU: Продвигает проверки нормального/ненулевого/сигнального выхода и перезапуска в window через phase;
 * tabs выбирает консоль для снимка. Завершает GTK после согласования статуса, кнопок и пустого контекста. */
static gboolean native_exit_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    if (phase >= 14)
        return native_backend_gui_step(window);
    if (phase >= 8)
        return native_entry_gui_step(window, tabs);
    GtkWidget *run = find(window, "Run");
    GtkWidget *status = find(window, "insight-gdb-stop-reason");
    GtkWidget *arguments = find(window, "insight-gdb-launch-arguments");
    if (!run || !status || !arguments || !gtk_widget_is_sensitive(run))
        return G_SOURCE_CONTINUE;
    const gchar *text = gtk_label_get_text(GTK_LABEL(status));
    if (phase == 1)
    {
        phase = 13;
        g_timeout_add(100, choose, observer);
        g_assert_true(click(window, "Load Lua Observer…"));
    }
    else if (phase == 2 && strstr(text, "Program exited with code 0."))
    {
        gtk_entry_set_text(GTK_ENTRY(arguments), "31");
        g_assert_true(click(window, "Run"));
        phase++;
    }
    else if (phase == 3 && strstr(text, "Program exited with code 31."))
    {
        GtkWidget *command = find(window, "GDB command, e.g. break main");
        if (!command || !gtk_widget_is_sensitive(command))
            return G_SOURCE_CONTINUE;
        gtk_entry_set_text(GTK_ENTRY(arguments), "signal");
        gtk_entry_set_text(GTK_ENTRY(command), "handle SIGTERM nostop noprint pass");
        g_signal_emit_by_name(command, "activate");
        phase++;
    }
    else if (phase == 4)
    {
        g_assert_true(click(window, "Run"));
        phase++;
    }
    else if (phase == 5 && strstr(text, "Program terminated by signal: SIGTERM"))
    {
        const gchar *inactive[] = {"Continue", "Pause", "Stop", "Step Into", "Step Over", "Step Out"};
        for (guint i = 0; i < G_N_ELEMENTS(inactive); i++)
            g_assert_false(gtk_widget_is_sensitive(find(window, inactive[i])));
        GtkWidget *locals = find(window, "insight-gdb-frame-variables");
        g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(locals)), NULL),
                        ==, 0);
        GtkWidget *source = find(window, "Open Source");
        g_assert_nonnull(source);
        g_assert_false(gtk_widget_is_sensitive(source));
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), 0);
        gtk_window_present(GTK_WINDOW(window));
        gdk_window_raise(gtk_widget_get_window(window));
        gtk_widget_queue_draw(window);
        phase++;
    }
    else if (phase == 6)
    {
        native_exit_gui_capture(window, "INSIGHT_GDB_EXIT_CAPTURE");
        gtk_entry_set_text(GTK_ENTRY(arguments), "0");
        g_assert_true(click(window, "Run"));
        phase++;
    }
    else if (phase == 7 && strstr(text, "Program exited with code 0."))
    {
        g_print("C/C++ GTK: normal/nonzero/signal exit status, cleared context and relaunch passed.\n");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(find(window, "insight-gdb-stop-at-main")), TRUE);
        g_assert_true(click(window, "Run"));
        phase = 14;
    }
    return G_SOURCE_CONTINUE;
}
