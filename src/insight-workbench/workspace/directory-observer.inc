/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Directory watches and change stamps.
 * RU: Наблюдение за каталогами и метки изменений. */


/* EN: Record the current stamp and register path with the workspace watcher.
 * RU: Запоминает текущую метку и ставит path под наблюдение. */
static void watch_directory(const char *path)
{
    gchar *stamp = insight_project_db_stamp(path, TRUE);
    if (stamp)
        g_hash_table_replace(wb.directory_stamps, g_strdup(path), stamp);
    GError *error = NULL;
    if (!insight_workspace_watch_add(wb.watch, path, &error))
        wb.watch_incomplete = TRUE;
    g_clear_error(&error);
}
