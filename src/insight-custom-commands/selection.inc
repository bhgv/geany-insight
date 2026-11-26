/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Capture one editor selection for %s without changing its source, clipboard or Undo history.
 * RU: Фиксирует одно выделение редактора для %s без изменения исходника, буфера обмена или Undo. */

/* EN: Capture the current nonempty normal UTF-8 selection, including read-only and unnamed buffers.
 * Return an owned unmaterialized plan or NULL/error for unsupported modes or invalid/bounded input.
 * RU: Фиксирует текущее непустое обычное выделение UTF-8, включая read-only и буферы без имени.
 * Возвращает собственный нереализованный план либо NULL/error для неподдерживаемого режима/текста/объёма. */
static InsightCustomSelection *selection_capture(GError **error)
{
    GeanyDocument *doc = document_get_current();
    if (!DOC_VALID(doc))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
            "Select text in a source document for %s");
        return NULL;
    }
    ScintillaObject *sci = doc->editor->sci;
    gint start = sci_get_selection_start(sci);
    gint end = sci_get_selection_end(sci);
    if (SEND(sci, SCI_GETCODEPAGE, 0, 0) != SC_CP_UTF8 ||
        SEND(sci, SCI_GETSELECTIONS, 0, 0) != 1 || sci_get_selection_mode(sci) != SC_SEL_STREAM ||
        SEND(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0) ||
        SEND(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0) ||
        end <= start || end - start > 1024 * 1024)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
            "Selection snapshot needs one nonempty normal selection up to 1 MiB");
        return NULL;
    }
    gchar *text = sci_get_contents_range(sci, start, end);
    InsightCustomSelection *snapshot = insight_custom_selection_plan(text, end - start, error);
    g_free(text);
    return snapshot;
}
