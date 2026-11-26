/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Retain and revalidate the exact source destination captured when a custom command starts.
 * RU: Удерживает и проверяет точное назначение исходника, зафиксированное при запуске команды. */
struct OutputTarget {
    guint refs, id;
    ScintillaObject *sci;
    gchar *text, *name;
    GeanyFiletype *filetype;
    gint anchor, caret;
    guint64 revision;
    gulong watch;
};

/* EN: Record source text edits (including Undo/Redo) for retained target data; sci/unused identify the signal only.
 * RU: Учитывает правки исходника, включая Undo/Redo, для target data; sci/unused лишь определяют сигнал. */
static void output_target_modified(ScintillaObject *sci, gint unused, SCNotification *nt, gpointer data)
{
    (void)sci;
    (void)unused;
    OutputTarget *target = data;
    if (nt->nmhdr.code == SCN_MODIFIED && (nt->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)))
        target->revision++;
}

/* EN: Release one reference to target, disconnecting its watcher and freeing retained source data on the last reference.
 * RU: Освобождает ссылку target; последняя отключает наблюдение и освобождает снимок исходника. */
static void output_target_unref(OutputTarget *target)
{
    if (!target || --target->refs)
        return;
    if (target->watch && g_signal_handler_is_connected(target->sci, target->watch))
        g_signal_handler_disconnect(target->sci, target->watch);
    g_object_unref(target->sci);
    g_free(target->text);
    g_free(target->name);
    g_free(target);
}

/* EN: Permit one ordinary UTF-8 selection in sci, excluding virtual space.
 * RU: Разрешает одно обычное выделение UTF-8 в sci без виртуальных позиций. */
static gboolean output_target_stream(ScintillaObject *sci)
{
    return SEND(sci, SCI_GETCODEPAGE, 0, 0) == SC_CP_UTF8 &&
        SEND(sci, SCI_GETSELECTIONS, 0, 0) == 1 && sci_get_selection_mode(sci) == SC_SEL_STREAM &&
        !SEND(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0) &&
        !SEND(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0);
}

/* EN: Capture current source and exact selection without edits; return owned target or NULL for unsupported/bounded input.
 * RU: Фиксирует текущий исходник и точное выделение без правок; собственный target либо NULL при неподдерживаемом вводе/объёме. */
static OutputTarget *output_target_capture(void)
{
    GeanyDocument *doc = document_get_current();
    if (!DOC_VALID(doc) || !output_target_stream(doc->editor->sci))
        return NULL;
    ScintillaObject *sci = doc->editor->sci;
    gint length = sci_get_length(sci);
    if (length < 0 || length > INSIGHT_TEXT_TRANSFER_LIMIT)
        return NULL;
    gchar *text = sci_get_contents(sci, -1);
    if (memchr(text, 0, length) || !g_utf8_validate(text, length, NULL))
    {
        g_free(text);
        return NULL;
    }
    OutputTarget *target = g_new0(OutputTarget, 1);
    target->refs = 1;
    target->id = doc->id;
    target->sci = g_object_ref(sci);
    target->text = text;
    target->name = g_strdup(doc->file_name);
    target->filetype = doc->file_type;
    target->anchor = SEND(sci, SCI_GETANCHOR, 0, 0);
    target->caret = sci_get_current_position(sci);
    target->watch = g_signal_connect(sci, "sci-notify", G_CALLBACK(output_target_modified), target);
    return target;
}

/* EN: Revalidate target identity, text revision/bytes, active tab, filename/type, selection and current write permission.
 * RU: Проверяет личность target, поколение/байты, активную вкладку, имя/тип, выделение и текущую доступность записи. */
static gboolean output_target_valid(OutputTarget *target)
{
    if (!target)
        return FALSE;
    GeanyDocument *doc = document_find_by_id(target->id);
    if (!DOC_VALID(doc) || document_get_current() != doc || doc->editor->sci != target->sci ||
        target->revision || g_strcmp0(doc->file_name, target->name) || doc->file_type != target->filetype ||
        doc->readonly || SEND(target->sci, SCI_GETREADONLY, 0, 0) || !output_target_stream(target->sci) ||
        SEND(target->sci, SCI_GETANCHOR, 0, 0) != target->anchor ||
        sci_get_current_position(target->sci) != target->caret)
        return FALSE;
    gint length = sci_get_length(target->sci);
    if (length != (gint)strlen(target->text))
        return FALSE;
    gchar *text = sci_get_contents(target->sci, -1);
    gboolean same = !memcmp(text, target->text, length);
    g_free(text);
    return same;
}

/* EN: Test whether v has complete, successful, nonempty UTF-8 stdout eligible for editor actions.
 * RU: Проверяет наличие полного успешного непустого UTF-8 stdout в v для действий редактора. */
static gboolean output_ready(CommandView *v)
{
    return !v->closed && !v->process && !v->stopped && !v->limited && !v->stdout_lost && v->exit_code == 0 &&
        v->stdout_bytes && v->stdout_bytes->len &&
        !memchr(v->stdout_bytes->data, 0, v->stdout_bytes->len) &&
        g_utf8_validate((const gchar *)v->stdout_bytes->data, v->stdout_bytes->len, NULL);
}

/* EN: Discard v's previous stdout and destination; clearing during capture disables later partial editor actions.
 * RU: Отбрасывает прежние stdout/назначение v; очистка во время захвата запрещает действия с последующим неполным результатом. */
static void output_reset(CommandView *v)
{
    output_target_unref(v->target);
    v->target = NULL;
    g_clear_pointer(&v->stdout_bytes, g_byte_array_unref);
    v->stdout_bytes = g_byte_array_new();
    v->stdout_lost = v->process != NULL;
    v->output_serial++;
}
