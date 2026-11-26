/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Open exact stdout as a new buffer or review an explicitly guarded source replacement.
 * RU: Открывает точный stdout новым буфером либо показывает явно защищённую замену исходника. */

/* EN: Fill a read-only numbered preview sci with text using the captured filetype ft; no source/Undo is changed.
 * RU: Заполняет нумерованный неизменяемый sci текстом text со стилем ft; исходник/Undo не меняются. */
static void output_preview_code(ScintillaObject *sci, const gchar *text, GeanyFiletype *ft)
{
    highlighting_set_styles(sci, ft ? ft : filetypes[GEANY_FILETYPES_NONE]);
    SEND(sci, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
    SEND(sci, SCI_SETREADONLY, FALSE, 0);
    SEND(sci, SCI_SETUNDOCOLLECTION, FALSE, 0);
    sci_set_text(sci, text);
    SEND(sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    SEND(sci, SCI_SETREADONLY, TRUE, 0);
    sci_set_line_numbers(sci, TRUE);
    SEND(sci, SCI_SETMARGINWIDTHN, 0, 0);
    SEND(sci, SCI_SETMARGINWIDTHN, 1, 48);
    SEND(sci, SCI_SETMARGINWIDTHN, 2, 0);
    SEND(sci, SCI_COLOURISE, 0, -1);
}

/* EN: Copy exact eligible stdout into a new unsaved buffer; button is unused, data is the live command view.
 * RU: Копирует точный допустимый stdout в новый несохранённый буфер; button не используется, data — окно команды. */
static void output_open(GtkWidget *button, gpointer data)
{
    (void)button;
    CommandView *v = data;
    if (!output_ready(v))
        return;
    gchar *payload = g_strndup((const gchar *)v->stdout_bytes->data, v->stdout_bytes->len);
    GeanyDocument *doc = document_new_file(NULL, NULL, NULL);
    if (doc)
    {
        sci_set_text(doc->editor->sci, payload);
        SEND(doc->editor->sci, SCI_EMPTYUNDOBUFFER, 0, 0);
        document_set_text_changed(doc, TRUE);
    }
    g_free(payload);
}

/* EN: Review v's retained successful stdout against its Run-time source selection; button is unused.
 * Revalidate before Apply, use one Undo, select inserted bytes and never write disk; stale sources remain untouched.
 * RU: Показывает успешный stdout v против выделения исходника при Run; button не используется.
 * Перед Apply проверяет снимок, использует один Undo, выделяет вставленное и не пишет на диск; устаревший исходник не меняется. */
static void output_review(GtkWidget *button, gpointer data)
{
    (void)button;
    CommandView *v = data;
    if (!output_ready(v) || !output_target_valid(v->target))
    {
        note(v, "Source/selection changed or source is read-only. Run the command again before replacing text.");
        return;
    }
    view_ref(v);
    OutputTarget *target = v->target;
    target->refs++;
    guint serial = v->output_serial;
    gchar *payload = g_strndup((const gchar *)v->stdout_bytes->data, v->stdout_bytes->len);
    gint begin = MIN(target->anchor, target->caret);
    gint end = MAX(target->anchor, target->caret);
    GError *error = NULL;
    gchar *result = insight_text_splice(target->text, strlen(target->text), payload, strlen(payload), begin, end, &error);
    if (!result)
    {
        note(v, error ? error->message : "Cannot build stdout replacement preview");
        g_clear_error(&error);
        goto done;
    }
    GtkWidget *dialog = gtk_dialog_new_with_buttons(insight_tr("Review stdout replacement"), GTK_WINDOW(v->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, insight_tr("Cancel"), GTK_RESPONSE_CANCEL,
        insight_tr("Apply"), GTK_RESPONSE_APPLY, NULL);
    g_object_ref_sink(dialog);
    gtk_widget_set_name(dialog, "insight-custom-output-review");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 1000, 600);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *status = gtk_label_new(insight_tr("Only stdout replaces the Run-time selection. Stderr stays in the log. One Undo; no disk writes."));
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_widget_set_name(status, "insight-custom-output-review-status");
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 4);
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    ScintillaObject *before = SCINTILLA(scintilla_new());
    ScintillaObject *after = SCINTILLA(scintilla_new());
    gtk_widget_set_name(GTK_WIDGET(before), "insight-custom-output-before");
    gtk_widget_set_name(GTK_WIDGET(after), "insight-custom-output-after");
    gtk_paned_pack1(GTK_PANED(paned), GTK_WIDGET(before), TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), GTK_WIDGET(after), TRUE, FALSE);
    gtk_box_pack_start(GTK_BOX(box), paned, TRUE, TRUE, 4);
    output_preview_code(before, target->text, target->filetype);
    output_preview_code(after, result, target->filetype);
    gboolean changes = strcmp(result, target->text) != 0;
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY, changes);
    if (!changes)
        gtk_label_set_text(GTK_LABEL(status), insight_tr("No text changes."));
    gtk_widget_show_all(dialog);
    for (;;)
    {
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (v->closed || response != GTK_RESPONSE_APPLY)
            break;
        if (v->output_serial != serial || v->target != target || !output_target_valid(target))
        {
            gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY, FALSE);
            gtk_label_set_text(GTK_LABEL(status), insight_tr("Source changed. Close this preview and run the command again."));
            continue;
        }
        if (!changes)
            break;
        GeanyDocument *doc = document_find_by_id(target->id);
        insight_completion_cancel(doc->editor);
        SEND(target->sci, SCI_AUTOCCANCEL, 0, 0);
        if (!output_target_valid(target))
        {
            gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY, FALSE);
            gtk_label_set_text(GTK_LABEL(status), insight_tr("Source changed. Close this preview and run the command again."));
            continue;
        }
        sci_start_undo_action(target->sci);
        SEND(target->sci, SCI_SETTARGETSTART, begin, 0);
        SEND(target->sci, SCI_SETTARGETEND, end, 0);
        SEND(target->sci, SCI_REPLACETARGET, strlen(payload), (sptr_t)payload);
        SEND(target->sci, SCI_SETSEL, begin, begin + strlen(payload));
        sci_end_undo_action(target->sci);
        SEND(target->sci, SCI_SCROLLCARET, 0, 0);
        output_target_unref(v->target);
        v->target = NULL;
        v->output_serial++;
        controls(v);
        note(v, "Stdout applied to the source buffer. Use Undo to revert.");
        break;
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
done:
    g_free(result);
    g_free(payload);
    output_target_unref(target);
    view_unref(v);
}

/* EN: Add explicit stdout actions to output page for v; availability follows process and UTF-8 capture state.
 * RU: Добавляет явные действия stdout на страницу page для v; доступность зависит от процесса и UTF-8 захвата. */
static void output_editor_buttons(CommandView *v, GtkWidget *page)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(page), row, FALSE, FALSE, 0);
    v->stdout_open = button(row, "Open stdout in new buffer", "insight-custom-stdout-open", G_CALLBACK(output_open), v, FALSE);
    v->stdout_review = button(row, "Review stdout replacement…", "insight-custom-stdout-review", G_CALLBACK(output_review), v, FALSE);
    gtk_widget_set_tooltip_text(v->stdout_review,
        insight_tr("Successful nonempty UTF-8 stdout up to 1 MiB; writable source and selection must still match Run"));
}
