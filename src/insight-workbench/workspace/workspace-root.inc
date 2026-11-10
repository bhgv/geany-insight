/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Workspace folder selection and scan cancellation.
 * RU: Выбор папки проекта и отмена сканирования. */


/* EN: Open a folder picker and scan the selected root. button and data are unused.
 * RU: Выбирает папку и запускает сканирование. button и data не используются. */
static void choose_root(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open source workspace", GTK_WINDOW(main_widgets.window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Cancel", GTK_RESPONSE_CANCEL, "Open", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gboolean previous = wb.workspace_from_project;
        wb.workspace_from_project = FALSE;
        if (!start_scan(path))
        {
            wb.workspace_from_project = previous;
            ui_set_statusbar(TRUE, "Cannot index source folder");
        }
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}

/* EN: Cancel the current scan and refresh panels from the partial index. button and data are unused.
 * RU: Отменяет сканирование и обновляет панели из частичного индекса. button и data не используются. */
static void cancel_scan(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    insight_master_sync_pause(wb.master_sync);
    stop_scan();
    insight_workspace_watch_clear(wb.watch);
    wb.watch_changed = FALSE;
    if (wb.watch_idle)
    {
        g_source_remove(wb.watch_idle);
        wb.watch_idle = 0;
    }
    refresh_index();
    browser_filter(NULL, NULL);
    files_filter(NULL, NULL);
    tree_refresh();
    file_types_refresh();
    categories_filter(NULL, NULL);
    local_filter(NULL, NULL);
    insight_workbench_refresh();
    ui_set_statusbar(TRUE, "Workspace scan cancelled; partial index retained");
}
