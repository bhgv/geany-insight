/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Drive real GDB exit/relaunch states and capture the production debugger's human status.
 * RU: Проверяет настоящие выходы/повторные запуски GDB и снимает понятный статус окна отладчика. */

/* EN: Capture window to INSIGHT_GDB_EXIT_CAPTURE when requested; assert a valid metadata-free JPEG.
 * RU: Снимает window в INSIGHT_GDB_EXIT_CAPTURE по запросу; проверяет создание JPEG без метаданных. */
static void native_exit_gui_capture(GtkWidget *window)
{
    const gchar *path = g_getenv("INSIGHT_GDB_EXIT_CAPTURE");
    if (!path || !*path)
        return;
    GdkWindow *surface = gtk_widget_get_window(window);
    GdkPixbuf *pixels = gdk_pixbuf_get_from_window(surface, 0, 0, gdk_window_get_width(surface),
                                                   gdk_window_get_height(surface));
    g_assert_nonnull(pixels);
    GError *error = NULL;
    g_assert_true(gdk_pixbuf_save(pixels, path, "jpeg", &error, "quality", "88", NULL));
    g_assert_no_error(error);
    g_object_unref(pixels);
}

/* EN: Advance normal/nonzero/signal/relaunch checks in window using shared phase; tabs selects Console
 * for the screenshot. End GTK only after status, inactive controls and empty stopped context agree.
 * RU: Продвигает проверки нормального/ненулевого/сигнального выхода и перезапуска в window через phase;
 * tabs выбирает консоль для снимка. Завершает GTK после согласования статуса, кнопок и пустого контекста. */
static gboolean native_exit_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    GtkWidget *run = find(window, "Run");
    GtkWidget *status = find(window, "insight-gdb-stop-reason");
    GtkWidget *arguments = find(window, "insight-gdb-launch-arguments");
    if (!run || !status || !arguments || !gtk_widget_is_sensitive(run))
        return G_SOURCE_CONTINUE;
    const gchar *text = gtk_label_get_text(GTK_LABEL(status));
    if (phase == 1)
    {
        gtk_entry_set_text(GTK_ENTRY(arguments), "0");
        g_assert_true(click(window, "Run"));
        phase++;
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
        phase++;
    }
    else if (phase == 6)
    {
        native_exit_gui_capture(window);
        gtk_entry_set_text(GTK_ENTRY(arguments), "0");
        g_assert_true(click(window, "Run"));
        phase++;
    }
    else if (phase == 7 && strstr(text, "Program exited with code 0."))
    {
        g_print("C/C++ GTK: normal/nonzero/signal exit status, cleared context and relaunch passed.\n");
        passed = TRUE;
        gtk_widget_destroy(window);
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
