/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Retain bounded per-editor structural selection ranges and restore them without text Undo.
 * RU: Хранит ограниченную историю структурных выделений редактора и восстанавливает их без Undo текста. */
typedef struct
{
    gint anchor, caret;
} StructuralRange;

typedef struct
{
    GArray *ranges;
    StructuralRange expected;
    gpointer filetype;
    guint document;
    gboolean applying;
} StructuralHistory;

/* EN: Return whether sci has one stream selection without virtual-space endpoints.
 * RU: Проверяет одно обычное выделение sci без виртуальных пробелов на концах. */
static gboolean structural_normal(ScintillaObject *sci)
{
    return sci_get_selection_mode(sci) == SC_SEL_STREAM && SSM(sci, SCI_GETSELECTIONS, 0, 0) == 1 &&
        !SSM(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0) &&
        !SSM(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0);
}

/* EN: Read the exact anchor and caret from sci, preserving reversed selections.
 * RU: Читает точные якорь и курсор sci, сохраняя обратные выделения. */
static StructuralRange structural_range(ScintillaObject *sci)
{
    StructuralRange range = {SSM(sci, SCI_GETANCHOR, 0, 0), sci_get_current_position(sci)};
    return range;
}

/* EN: Free editor-owned history data after its widget is finalized.
 * RU: Освобождает историю data во владении редактора при завершении жизни виджета. */
static void structural_history_free(gpointer data)
{
    StructuralHistory *history = data;
    g_array_unref(history->ranges);
    g_free(history);
}

/* EN: Clear history in data on text edits or manual selection changes reported by sci/notification; message is unused.
 * RU: Сбрасывает историю data при правках текста или ручном выделении из sci/notification; message не используется. */
static void structural_history_notify(ScintillaObject *sci, gint message, SCNotification *notification, gpointer data)
{
    (void)message;
    StructuralHistory *history = data;
    gboolean edited = notification->nmhdr.code == SCN_MODIFIED &&
        (notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT));
    StructuralRange current = structural_range(sci);
    gboolean moved = !history->applying && notification->nmhdr.code == SCN_UPDATEUI &&
        (!structural_normal(sci) || current.anchor != history->expected.anchor || current.caret != history->expected.caret);
    if (edited || moved)
        g_array_set_size(history->ranges, 0);
}

/* EN: Clear the history in data before ordinary mouse handling in widget; event is unused and GTK keeps processing it.
 * RU: Сбрасывает историю data до обычной обработки мыши widget; event не используется, GTK продолжает обработку. */
static gboolean structural_history_mouse(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget;
    (void)event;
    StructuralHistory *history = data;
    if (!history->applying)
        g_array_set_size(history->ranges, 0);
    return FALSE;
}

/* EN: Invalidate widget data's history when doc changes file type; object/previous identify the application event.
 * RU: Сбрасывает историю виджета data при смене языка doc; object/previous задают событие приложения. */
static void structural_history_filetype(GObject *object, GeanyDocument *doc, GeanyFiletype *previous, gpointer data)
{
    (void)object;
    (void)previous;
    if (!doc || GTK_WIDGET(doc->editor->sci) != data)
        return;
    StructuralHistory *history = g_object_get_data(G_OBJECT(data), "insight-structural-history");
    if (history)
    {
        g_array_set_size(history->ranges, 0);
        history->filetype = doc->file_type;
    }
}

/* EN: Get doc's widget-owned history, creating only when requested; invalidate mismatched document/type/selection state.
 * RU: Возвращает историю виджета doc, создавая лишь при create; сбрасывает несовпадение документа/языка/выделения. */
static StructuralHistory *structural_history(GeanyDocument *doc, gboolean create)
{
    ScintillaObject *sci = doc->editor->sci;
    StructuralHistory *history = g_object_get_data(G_OBJECT(sci), "insight-structural-history");
    if (!history && create)
    {
        history = g_new0(StructuralHistory, 1);
        history->ranges = g_array_new(FALSE, FALSE, sizeof(StructuralRange));
        g_object_set_data_full(G_OBJECT(sci), "insight-structural-history", history, structural_history_free);
        g_signal_connect(sci, "sci-notify", G_CALLBACK(structural_history_notify), history);
        g_signal_connect(sci, "button-press-event", G_CALLBACK(structural_history_mouse), history);
        g_signal_connect_object(geany_object, "document-filetype-set", G_CALLBACK(structural_history_filetype), sci, 0);
    }
    if (!history)
        return NULL;
    StructuralRange current = structural_range(sci);
    if (history->document != doc->id || history->filetype != doc->file_type || !structural_normal(sci) ||
        current.anchor != history->expected.anchor || current.caret != history->expected.caret)
        g_array_set_size(history->ranges, 0);
    history->document = doc->id;
    history->filetype = doc->file_type;
    return history;
}

/* EN: Select anchor/caret in doc, optionally recording the old selection; keep at most 128 prior ranges and no text changes.
 * RU: Выделяет anchor/caret в doc, при remember сохраняя прежнее выделение; хранит до 128 диапазонов без изменения текста. */
static void structural_apply(GeanyDocument *doc, gint anchor, gint caret, gboolean remember)
{
    ScintillaObject *sci = doc->editor->sci;
    StructuralHistory *history = structural_history(doc, TRUE);
    StructuralRange old = structural_range(sci);
    if (remember && (old.anchor != anchor || old.caret != caret))
    {
        if (history->ranges->len == 128)
            g_array_remove_index(history->ranges, 0);
        g_array_append_val(history->ranges, old);
    }
    history->applying = TRUE;
    history->expected.anchor = anchor;
    history->expected.caret = caret;
    sci_set_selection(sci, anchor, caret);
    SSM(sci, SCI_SCROLLCARET, 0, 0);
    document_grab_focus(doc);
    history->applying = FALSE;
}

/* EN: Restore doc's previous structural range if its source/selection is still valid; report an empty history without moving.
 * RU: Восстанавливает прежний структурный диапазон doc при актуальном исходнике/выделении; сообщает пустую историю без перемещения. */
static void structural_shrink(GeanyDocument *doc)
{
    StructuralHistory *history = structural_history(doc, FALSE);
    if (!history || !history->ranges->len)
    {
        ui_set_statusbar(TRUE, "No previous structural selection to restore");
        return;
    }
    StructuralRange range = g_array_index(history->ranges, StructuralRange, history->ranges->len - 1);
    g_array_set_size(history->ranges, history->ranges->len - 1);
    structural_apply(doc, range.anchor, range.caret, FALSE);
}
