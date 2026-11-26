/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Snapshot named open buffers for bulk custom-command arguments at preview/run time.
 * RU: Фиксирует именованные открытые буферы для массовых аргументов при предпросмотре/запуске. */

/* EN: Capture doc's filename variants relative to root and its modified flag, without reading/saving source text.
 * Return owned record; doc must be valid and named, root is borrowed.
 * RU: Фиксирует варианты имени doc относительно root и признак изменения без чтения/сохранения исходника.
 * Возвращает собственную запись; doc должен быть действительным и именованным, root заимствован. */
static InsightCustomFile *command_file(GeanyDocument *doc, const gchar *root)
{
    InsightCustomFile *file = g_new0(InsightCustomFile, 1);
    file->values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    file->modified = doc->changed;
    gchar *name = g_path_get_basename(doc->file_name);
    gchar *stem = g_strdup(name);
    gchar *dot = strrchr(stem, '.');
    value(file->values, "f", doc->file_name);
    value(file->values, "n", name);
    if (dot && dot != stem)
    {
        *dot = 0;
        value(file->values, "e", dot + 1);
    }
    else
        value(file->values, "e", "");
    value(file->values, "b", stem);
    GFile *base = root ? g_file_new_for_path(root) : NULL;
    GFile *source = g_file_new_for_path(doc->file_name);
    gchar *relative = base ? g_file_get_relative_path(base, source) : NULL;
    value(file->values, "r", relative ? relative : doc->file_name);
    g_free(relative);
    if (base)
        g_object_unref(base);
    g_object_unref(source);
    g_free(name);
    g_free(stem);
    return file;
}

/* EN: Prepare v's current command into owned argv/directory/context, even when later validation fails.
 * Rebuild file/selection snapshots into selection and validate diagnostics into regex for every call; no processes or file creation occur. Return FALSE/error on invalid input.
 * RU: Готовит текущую команду v в собственные argv/directory/context, даже при последующей ошибке проверки.
 * Каждый вызов фиксирует файлы/выделение в selection и проверяет диагностику в regex; без запусков и создания файлов. FALSE/error при неверном вводе. */
static gboolean prepare(CommandView *v, gchar ***argv, gchar **directory,
    GHashTable **context, InsightCustomSelection **selection, GRegex **regex, GError **error)
{
    *context = context_values();
    for (guint i = 0; i < 9; i++)
    {
        gchar key[2] = {(gchar)('1' + i), 0};
        value(*context, key, gtk_entry_get_text(GTK_ENTRY(v->arguments[i])));
    }
    *regex = insight_custom_diagnostic_regex(gtk_entry_get_text(GTK_ENTRY(v->diagnostic_pattern)), error);
    if (error && *error)
        return FALSE;
    gboolean requested = insight_custom_selection_requested(gtk_entry_get_text(GTK_ENTRY(v->command)), error);
    if (error && *error)
        return FALSE;
    if (requested)
    {
        *selection = selection_capture(error);
        if (!*selection)
            return FALSE;
        gchar *path = g_filename_to_utf8((*selection)->path, -1, NULL, NULL, error);
        if (!path)
            return FALSE;
        value(*context, "s", path);
        g_free(path);
    }
    GPtrArray *files = g_ptr_array_new_with_free_func(insight_custom_file_free);
    const gchar *root = g_hash_table_lookup(*context, "j");
    guint clean = 0;
    guint modified = 0;
    for (guint i = 0; documents_array && i < documents_array->len; i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        if (!DOC_VALID(doc) || !doc->file_name)
            continue;
        guint *count = doc->changed ? &modified : &clean;
        if ((*count)++ < 129)
            g_ptr_array_add(files, command_file(doc, root));
    }
    *argv = insight_custom_argv_files(gtk_entry_get_text(GTK_ENTRY(v->command)), *context, files, error);
    g_ptr_array_unref(files);
    if (!*argv)
        return FALSE;
    gchar *expanded = insight_custom_expand(gtk_entry_get_text(GTK_ENTRY(v->directory)), *context, error);
    if (!expanded)
        return FALSE;
    if (!*expanded)
    {
        g_free(expanded);
        expanded = g_strdup(root);
    }
    if (!expanded || !g_path_is_absolute(expanded))
    {
        g_free(expanded);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Working directory must be an absolute path");
        return FALSE;
    }
    *directory = g_filename_from_utf8(expanded, -1, NULL, NULL, error);
    g_free(expanded);
    if (!*directory)
        return FALSE;
    if (!g_file_test(*directory, G_FILE_TEST_IS_DIR))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, "Working directory does not exist");
        return FALSE;
    }
    return TRUE;
}
