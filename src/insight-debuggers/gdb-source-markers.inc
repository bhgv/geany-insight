/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Own native-debugger margin dots and stopped-line highlights without retaining editors.
 * RU: Хранит точки на полях и подсветку остановки, не удерживая закрытые редакторы. */

typedef struct
{
    GWeakRef editor;
    gint handle;
} NativeSourceMarker;

/* EN: Remove the marker in data if its editor remains open, then free its weak reference.
 * RU: Удаляет маркер data, если редактор ещё открыт, и освобождает слабую ссылку. */
static void native_marker_free(gpointer data)
{
    NativeSourceMarker *marker = data;
    GObject *editor = g_weak_ref_get(&marker->editor);
    if (editor)
    {
        SSM(SCINTILLA(editor), SCI_MARKERDELETEHANDLE, marker->handle, 0);
        g_object_unref(editor);
    }
    g_weak_ref_clear(&marker->editor);
    g_free(marker);
}

/* EN: Add type/color marker at one-based line of filename to owned; ignore absent source files.
 * RU: Добавляет в owned маркер type/color на строке line файла filename; отсутствующие файлы пропускает. */
static void native_marker_add(GPtrArray *owned, const gchar *filename, guint line,
                              gint type, gint color)
{
    GeanyDocument *document = filename ? document_find_by_filename(filename) : NULL;
    if (!DOC_VALID(document) || !line || line > (guint)sci_get_line_count(document->editor->sci))
        return;
    ScintillaObject *editor = document->editor->sci;
    gint symbol = type == 19 ? SC_MARK_BACKGROUND : SC_MARK_CIRCLE;
    SSM(editor, SCI_MARKERDEFINE, type, symbol);
    SSM(editor, SCI_MARKERSETFORE, type, color);
    SSM(editor, SCI_MARKERSETBACK, type, color);
    gint handle = SSM(editor, SCI_MARKERADD, line - 1, type);
    if (handle >= 0)
    {
        NativeSourceMarker *marker = g_new0(NativeSourceMarker, 1);
        marker->handle = handle;
        g_weak_ref_init(&marker->editor, G_OBJECT(editor));
        g_ptr_array_add(owned, marker);
    }
}

/* EN: Return whether session's paused source coordinates identify an existing absolute local file.
 * Convert the UTF-8 GDB path to native encoding; absent/relative/unconvertible paths return FALSE.
 * RU: Возвращает, указывают ли координаты остановки session на существующий абсолютный локальный файл.
 * Преобразует путь GDB из UTF-8; отсутствующие/относительные/непреобразуемые пути дают FALSE. */
static gboolean native_source_available(NativeDebug *session)
{
    if (!session->stopped || !session->stop_file || !*session->stop_file || !session->stop_line)
        return FALSE;
    gchar *filename = utils_get_locale_from_utf8(session->stop_file);
    gboolean available = filename && g_path_is_absolute(filename) &&
        g_file_test(filename, G_FILE_TEST_IS_REGULAR);
    g_free(filename);
    return available;
}

/* EN: Update session's source action and diagnostic without changing the selected frame or execution.
 * Missing coordinates explain assembly fallback; unavailable files expose the full path in a tooltip.
 * RU: Обновляет кнопку исходника и пояснение session без изменения выбранного кадра и исполнения.
 * При отсутствии координат предлагает ассемблер; недоступный путь полностью показывается в подсказке. */
static void native_source_update(NativeDebug *session)
{
    gboolean available = native_source_available(session);
    gtk_widget_set_sensitive(session->source, available);
    gchar *message = NULL;
    if (session->stopped && !available)
    {
        if (session->stop_file && *session->stop_file && session->stop_line)
            message = g_strdup_printf(insight_tr("Source file is unavailable locally: %s. Check source mapping."), session->stop_file);
        else
            message = g_strdup(insight_tr("The selected frame has no source coordinates. Inspect the stack or assembly."));
    }
    gchar *valid = g_utf8_make_valid(message ? message : "", -1);
    gtk_label_set_text(GTK_LABEL(session->source_status), valid);
    gtk_widget_set_tooltip_text(session->source_status, *valid ? valid : NULL);
    g_free(valid);
    g_free(message);
}

/* EN: Open and highlight session's existing stopped source; clear old highlights without opening missing files.
 * RU: Открывает и подсвечивает существующий исходник остановки session; очищает старую подсветку без открытия отсутствующих файлов. */
static void native_source_stopped(NativeDebug *session)
{
    g_ptr_array_set_size(session->stop_markers, 0);
    if (!native_source_available(session))
        return;
    gchar *filename = utils_get_locale_from_utf8(session->stop_file);
    diff_source_open(filename, session->stop_line);
    g_free(filename);
    native_marker_add(session->stop_markers, session->stop_file, session->stop_line, 19, 0x80dfff);
}

/* EN: Rebuild session's dots, letting any enabled location win over disabled ones on the same line.
 * RU: Перестраивает точки session: включённая позиция имеет приоритет над выключенной на той же строке. */
static void native_source_points(NativeDebug *session)
{
    g_ptr_array_set_size(session->point_markers, 0);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GtkTreeModel *model = GTK_TREE_MODEL(session->point_rows);
    GHashTable *disabled = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GtkTreeIter parent_row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &parent_row); valid;
         valid = gtk_tree_model_iter_next(model, &parent_row))
    {
        gchar *number = NULL, *enabled = NULL;
        gtk_tree_model_get(model, &parent_row, 1, &number, 2, &enabled, -1);
        if (number && !strchr(number, '.') && !g_strcmp0(enabled, "n"))
            g_hash_table_add(disabled, g_strdup(number));
        g_free(enabled);
        g_free(number);
    }
    for (guint pass = 0; pass < 2; pass++)
    {
        GtkTreeIter row;
        for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
             valid = gtk_tree_model_iter_next(model, &row))
        {
            gchar *file = NULL, *line = NULL, *enabled = NULL, *owner = NULL;
            gtk_tree_model_get(model, &row, 6, &file, 7, &line, 2, &enabled, 1, &owner, -1);
            gchar *dot = owner ? strchr(owner, '.') : NULL;
            if (dot)
                *dot = '\0';
            guint64 number;
            gboolean active = !g_strcmp0(enabled, "y") &&
                              !(owner && g_hash_table_contains(disabled, owner));
            if (active == (pass == 0) && file && line &&
                g_ascii_string_to_unsigned(line, 10, 1, G_MAXINT, &number, NULL) &&
                g_hash_table_add(seen, native_point_key(file, number)))
                native_marker_add(session->point_markers, file, number,
                                  active ? 18 : 20, active ? 0x2020df : 0x888888);
            g_free(owner);
            g_free(enabled);
            g_free(line);
            g_free(file);
        }
    }
    g_hash_table_unref(disabled);
    g_hash_table_unref(seen);
}

/* EN: Restore dialog's dots when document opens or activates; object is unused.
 * RU: Восстанавливает точки dialog при открытии/активации document; object не используется. */
static void native_source_document(GObject *object, GeanyDocument *document, gpointer dialog)
{
    (void)object;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (!session->closed && DOC_VALID(document))
        native_source_points(session);
}
