/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Scintilla selection search and source decoration.
 * RU: Поиск выделения и подсветка исходников в Scintilla. */

/* EN: Private slots preserve Geany's error, search and snippet indicators.
 * RU: Отдельные номера сохраняют индикаторы ошибок, поиска и шаблонов Geany. */
enum
{
    INSIGHT_HDL_COLOR = 24,
    INSIGHT_MATCH_FILL = 28,
    INSIGHT_MATCH_FRAME = 29,
    INSIGHT_SAME_TEXT = 30,
    INSIGHT_LINK = 31
};
static void preview_links(void);

/* EN: Configure search indicators on sci.
 * RU: Настраивает индикаторы поиска в sci. */
static void decorate_setup(ScintillaObject *sci)
{
    SSM(sci, SCI_INDICSETSTYLE, INSIGHT_MATCH_FILL, INDIC_STRAIGHTBOX);
    SSM(sci, SCI_INDICSETFORE, INSIGHT_MATCH_FILL, 0x00ffff);
    SSM(sci, SCI_INDICSETALPHA, INSIGHT_MATCH_FILL, 110);
    SSM(sci, SCI_INDICSETOUTLINEALPHA, INSIGHT_MATCH_FILL, 0);
    SSM(sci, SCI_INDICSETSTYLE, INSIGHT_MATCH_FRAME, INDIC_BOX);
    SSM(sci, SCI_INDICSETFORE, INSIGHT_MATCH_FRAME, 0x0000ff);
    SSM(sci, SCI_INDICSETSTROKEWIDTH, INSIGHT_MATCH_FRAME, 100);
    SSM(sci, SCI_INDICSETUNDER, INSIGHT_MATCH_FRAME, FALSE);
    SSM(sci, SCI_INDICSETSTYLE, INSIGHT_SAME_TEXT, INDIC_STRAIGHTBOX);
    SSM(sci, SCI_INDICSETFORE, INSIGHT_SAME_TEXT, 0x00ffff);
    SSM(sci, SCI_INDICSETALPHA, INSIGHT_SAME_TEXT, 90);
    SSM(sci, SCI_INDICSETOUTLINEALPHA, INSIGHT_SAME_TEXT, 0);
    SSM(sci, SCI_INDICSETUNDER, INSIGHT_SAME_TEXT, TRUE);
}
static void decorate_match(ScintillaObject *sci, gsize begin, gsize end);
typedef struct
{
    guint timer, apply;
    guint64 generation, text_version, requested_version;
    gint begin, end, length;
    gboolean alive, highlighted;
    gsize match_begin, match_end;
    guint64 match_version;
    gboolean match_ready;
    GCancellable *cancel;
    InsightSearchMatch *matches;
    gsize count, position;
} SelectionState;
typedef struct
{
    GWeakRef sci;
    gchar *text, *query;
    InsightSearchMatch *matches;
    gsize count;
    guint64 generation;
} SelectionJob;

/* EN: Cancel pending search work and release results in s.
 * RU: Отменяет поиск и освобождает результаты в s. */
static void selection_stop(SelectionState *s)
{
    s->generation++;
    if (s->timer)
    {
        g_source_remove(s->timer);
        s->timer = 0;
    }
    if (s->apply)
    {
        g_source_remove(s->apply);
        s->apply = 0;
    }
    if (s->cancel)
    {
        g_cancellable_cancel(s->cancel);
        g_clear_object(&s->cancel);
    }
    g_clear_pointer(&s->matches, g_free);
    s->count = s->position = 0;
}

/* EN: Cancel and release the selection state in data.
 * RU: Отменяет задачи и освобождает состояние выделения из data. */
static void selection_state_free(gpointer data)
{
    SelectionState *s = data;
    selection_stop(s);
    g_free(s);
}

/* EN: Mark data inactive and cancel its work when widget is destroyed.
 * RU: Помечает data неактивным и отменяет задачи при уничтожении widget. */
static void selection_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    SelectionState *s = data;
    s->alive = FALSE;
    selection_stop(s);
}

/* EN: Invalidate data on text changes in notification n. sci and message are unused.
 * RU: Сбрасывает data при изменении текста по уведомлению n. sci и message не используются. */
static void selection_modified(ScintillaObject *sci, gint message, SCNotification *n, gpointer data)
{
    (void)sci;
    (void)message;
    SelectionState *s = data;
    if (n->nmhdr.code == SCN_MODIFIED && (n->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT)))
    {
        s->text_version++;
        selection_stop(s);
    }
}

/* EN: Get or create the selection state owned by sci.
 * RU: Возвращает или создаёт состояние выделения, которым владеет sci. */
static SelectionState *selection_state(ScintillaObject *sci)
{
    SelectionState *s = g_object_get_data(G_OBJECT(sci), "insight-selection-worker");
    if (!s)
    {
        s = g_new0(SelectionState, 1);
        s->alive = TRUE;
        s->begin = s->end = -1;
        g_object_set_data_full(G_OBJECT(sci), "insight-selection-worker", s, selection_state_free);
        g_signal_connect(sci, "sci-notify", G_CALLBACK(selection_modified), s);
        g_signal_connect(sci, "destroy", G_CALLBACK(selection_destroy), s);
    }
    return s;
}

/* EN: Highlight the range begin to end in sci.
 * RU: Подсвечивает диапазон от begin до end в sci. */
static void decorate_match(ScintillaObject *sci, gsize begin, gsize end)
{
    SelectionState *state = selection_state(sci);
    if (state->match_ready && state->match_begin == begin &&
        state->match_end == end && state->match_version == state->text_version) return;
    state->match_ready = TRUE;
    state->match_begin = begin;
    state->match_end = end;
    state->match_version = state->text_version;
    decorate_setup(sci);
    /* EN: Keep a selected search hit yellow.
     * RU: Сохраняем жёлтый цвет выделенного результата поиска. */
    SSM(sci, SCI_SETSELBACK, TRUE, 0x00ffff);
    SSM(sci, SCI_SETSELFORE, FALSE, 0);
    SSM(sci, SCI_SETSELALPHA, SC_ALPHA_NOALPHA, 0);
    for (gint indicator = INSIGHT_MATCH_FILL;indicator <= INSIGHT_MATCH_FRAME;indicator++)
    {
        SSM(sci, SCI_SETINDICATORCURRENT, indicator, 0);
        SSM(sci, SCI_INDICATORCLEARRANGE, 0, sci_get_length(sci));
        if (end > begin && end <= (gsize)sci_get_length(sci))
            SSM(sci, SCI_INDICATORFILLRANGE, begin, end - begin);
    }
}

/* EN: Paint the next batch of matches in the editor from data.
 * RU: Подсвечивает следующую порцию совпадений в редакторе из data. */
static gboolean selection_apply(gpointer data)
{
    ScintillaObject *sci = data;
    SelectionState *s = selection_state(sci);
    gint64 deadline = g_get_monotonic_time()+3000;
    if (!s->alive)
    {
        s->apply = 0;
        return G_SOURCE_REMOVE;
    }
    SSM(sci, SCI_SETINDICATORCURRENT, INSIGHT_SAME_TEXT, 0);
    while (s->position < s->count)
    {
        InsightSearchMatch *hit = &s->matches[s->position++];
        SSM(sci, SCI_INDICATORFILLRANGE, hit->start, hit->end - hit->start);
        s->highlighted = TRUE;
        if (g_get_monotonic_time() >= deadline)
            return G_SOURCE_CONTINUE;
    }
    s->apply = 0;
    g_clear_pointer(&s->matches, g_free);
    return G_SOURCE_REMOVE;
}

/* EN: Release the search job and its owned buffers from data.
 * RU: Освобождает задачу и её буферы из data. */
static void selection_job_free(gpointer data)
{
    SelectionJob *j = data;
    g_weak_ref_clear(&j->sci);
    g_free(j->text);
    g_free(j->query);
    g_free(j->matches);
    g_free(j);
}

/* EN: Collect matches for data and complete task. cancel stops the work. source is unused.
 * RU: Собирает совпадения для data и завершает task. cancel отменяет работу. source не используется. */
static void selection_job_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    SelectionJob *j = data;
    j->matches = g_new(InsightSearchMatch, 10000);
    gsize total = 0;
    if (!g_cancellable_is_cancelled(cancel) && insight_context_collect(j->text, strlen(j->text), j->query,
                                INSIGHT_SEARCH_CASE | INSIGHT_SEARCH_WORD,
                                j->matches, 10000, &total, NULL))
        j->count = MIN(total, 10000);
    g_task_return_boolean(task, TRUE);
}

/* EN: Apply result if the originating editor still needs it. source and data are unused.
 * RU: Применяет result, если редактор ещё ждёт его. source и data не используются. */
static void selection_job_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    SelectionJob *j = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    ScintillaObject *sci = g_weak_ref_get(&j->sci);
    if (!sci) return;
    SelectionState *s = selection_state(sci);
    if (ok && s->alive && s->generation == j->generation)
    {
        g_clear_object(&s->cancel);
        s->matches = j->matches;
        j->matches = NULL;
        s->count = j->count;
        s->position = 0;
        s->apply = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, selection_apply, g_object_ref(sci), g_object_unref);
    }
    g_object_unref(sci);
}

/* EN: Start the deferred search for the editor from data.
 * RU: Запускает отложенный поиск для редактора из data. */
static gboolean selection_timeout(gpointer data)
{
    ScintillaObject *sci = data;
    SelectionState *s = selection_state(sci);
    s->timer = 0;
    if (!s->alive)
        return G_SOURCE_REMOVE;
    SelectionJob *j = g_new0(SelectionJob, 1);
    g_weak_ref_init(&j->sci, sci);
    j->generation = s->generation;
    j->text = sci_get_contents(sci, s->length+1);
    j->query = g_strndup(j->text+s->begin, (gsize)(s->end-s->begin));
    s->cancel = g_cancellable_new();
    GTask *task = g_task_new(NULL, s->cancel, selection_job_done, NULL);
    g_task_set_task_data(task, j, selection_job_free);
    g_task_run_in_thread(task, selection_job_run);
    g_object_unref(task);
    return G_SOURCE_REMOVE;
}

/* EN: Schedule a search for text selected in sci.
 * RU: Планирует поиск текста, выделенного в sci. */
static void decorate_selection(ScintillaObject *sci)
{
    SelectionState *s = selection_state(sci);
    gint length = sci_get_length(sci),
         begin = (gint)SSM(sci, SCI_GETSELECTIONSTART, 0, 0),
         end = (gint)SSM(sci, SCI_GETSELECTIONEND, 0, 0);
    if (!s->alive || (s->length == length && s->begin == begin && s->end == end &&
                      s->requested_version == s->text_version)) return;
    selection_stop(s);
    s->length = length;
    s->begin = begin;
    s->end = end;
    s->requested_version = s->text_version;
    if (s->highlighted)
    {
        SSM(sci, SCI_SETINDICATORCURRENT, INSIGHT_SAME_TEXT, 0);
        SSM(sci, SCI_INDICATORCLEARRANGE, 0, MAX(0, length));
        s->highlighted = FALSE;
    }
    if (length < 0 || length > 2 * 1024 * 1024 || begin < 0 || end > length || end <= begin || end - begin > 1024) return;
    decorate_setup(sci);
    s->timer = g_timeout_add_full(G_PRIORITY_DEFAULT, 150, selection_timeout, g_object_ref(sci), g_object_unref);
}

/* EN: Go to line in doc and highlight the first occurrence of name.
 * RU: Переходит к строке line в doc и подсвечивает первое вхождение name. */
static void editor_mark_symbol(GeanyDocument *doc, gulong line, const gchar *name)
{
    ScintillaObject *sci = doc->editor->sci;
    SSM(sci, SCI_ENSUREVISIBLE, line-1, 0);
    SSM(sci, SCI_GOTOLINE, line-1, 0);
    gchar *text = sci_get_line(sci, (gint)line-1);
    InsightSearchMatch match;
    gsize total = 0;
    gint start = sci_get_position_from_line(sci, (gint)line-1);
    if (name && *name && insight_context_collect(text, strlen(text), name,
                               INSIGHT_SEARCH_CASE | INSIGHT_SEARCH_WORD, &match, 1, &total, NULL) && total)
    {
        SSM(sci, SCI_SETSEL, start+match.start, start+match.end);
        decorate_match(sci, start+match.start, start+match.end);
    }
    else decorate_match(sci, 0, 0);
    g_free(text);
}

/* EN: Highlight name on the current preview line.
 * RU: Подсвечивает name в текущей строке предпросмотра. */
static void preview_mark_symbol(const char *name)
{
    if (!name || !*name) return;
    gint line = sci_get_current_line(wb.preview), begin = sci_get_position_from_line(wb.preview, line);
    gchar *text = sci_get_line(wb.preview, line);
    InsightSearchMatch match;
    gsize total = 0;
    if (insight_context_collect(text, strlen(text), name,
                               INSIGHT_SEARCH_CASE | INSIGHT_SEARCH_WORD, &match, 1, &total, NULL) && total)
    {
        SSM(wb.preview, SCI_SETSEL, (gsize)begin+match.start, (gsize)begin+match.end);
        decorate_match(wb.preview, (gsize)begin+match.start, (gsize)begin+match.end);
    }
    g_free(text);
}
