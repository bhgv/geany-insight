/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: HDL color invalidation and pending paint state.
 * RU: Сброс цветов HDL и состояние отложенной подсветки. */

static guint hdl_color_timer, hdl_snapshot_idle, hdl_paint_idle, hdl_paint_cursor, hdl_paint_first, hdl_paint_last, hdl_paint_doc;
static guint64 hdl_color_revision;
static GCancellable *hdl_color_worker;

/* EN: Clear HDL indicators and metadata in sci.
 * RU: Очищает индикаторы HDL и их метаданные в sci. */
static void hdl_clear_document_colors(ScintillaObject *sci)
{
    if (!g_object_get_data(G_OBJECT(sci), "insight-hdl-painted")) return;
    g_object_set_data(G_OBJECT(sci), "insight-hdl-painted", NULL);
    g_object_set_data(G_OBJECT(sci), "insight-hdl-spans", NULL);
    SSM(sci, SCI_SETINDICATORCURRENT, INSIGHT_HDL_COLOR, 0);
    SSM(sci, SCI_INDICATORCLEARRANGE, 0, sci_get_length(sci));
}

/* EN: Cancel pending color work and invalidate older results.
 * RU: Отменяет задачи подсветки и помечает прежние результаты устаревшими. */
static void hdl_cancel_colors(void)
{
    hdl_color_revision++;
    if (hdl_color_timer)
    {
        g_source_remove(hdl_color_timer);
        hdl_color_timer = 0;
    }
    if (hdl_snapshot_idle)
    {
        g_source_remove(hdl_snapshot_idle);
        hdl_snapshot_idle = 0;
    }
    if (hdl_paint_idle)
    {
        g_source_remove(hdl_paint_idle);
        hdl_paint_idle = 0;
    }
    if (hdl_color_worker)
        g_cancellable_cancel(hdl_color_worker);
}

/* EN: Clear HDL color state across open documents and schedule a refresh.
 * RU: Сбрасывает подсветку HDL в открытых документах и планирует обновление. */
static void hdl_invalidate_colors(void)
{
    hdl_cancel_colors();
    for (guint i = 0;i<documents_array->len;i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        if (DOC_VALID(doc)) hdl_clear_document_colors(doc->editor->sci);
    }
    hdl_schedule_colors();
}
