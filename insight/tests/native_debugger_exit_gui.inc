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
        phase = 9;
    }
    else if (phase == 9 && gtk_widget_is_sensitive(resume))
    {
        const gchar *path = g_getenv("INSIGHT_GDB_ENTRY_CAPTURE");
        if (path && *path)
        {
            GdkWindow *surface = gtk_widget_get_window(window);
            GdkPixbuf *pixels = gdk_pixbuf_get_from_window(surface, 0, 0, gdk_window_get_width(surface),
                                                           gdk_window_get_height(surface));
            g_assert_nonnull(pixels);
            g_assert_true(gdk_pixbuf_save(pixels, path, "jpeg", NULL, "quality", "88", NULL));
            g_object_unref(pixels);
        }
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

/* EN: Advance normal/nonzero/signal/relaunch checks in window using shared phase; tabs selects Console
 * for the screenshot. End GTK only after status, inactive controls and empty stopped context agree.
 * RU: Продвигает проверки нормального/ненулевого/сигнального выхода и перезапуска в window через phase;
 * tabs выбирает консоль для снимка. Завершает GTK после согласования статуса, кнопок и пустого контекста. */
static gboolean native_exit_gui_step(GtkWidget *window, GtkWidget *tabs)
{
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
