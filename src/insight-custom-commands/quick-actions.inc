/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Launch verified saved profiles without overwriting drafts; retain session repeat state and shortcut editing controls.
 * RU: Запускает проверенные профили без потери черновиков; хранит повтор сеанса и элементы редактирования клавиш. */

/* EN: Release owned action data and its complete saved profile snapshot; NULL is safe.
 * RU: Освобождает собственные data действия и снимок профиля; NULL безопасен. */
static void quick_action_free(gpointer data)
{
    QuickAction *action = data;
    if (!action)
        return;
    insight_custom_command_free(action->command);
    g_free(action);
}

/* EN: Allocate an owned action for index/command in the current menu generation.
 * RU: Создаёт собственное действие для index/command в текущем поколении меню. */
static QuickAction *quick_action_new(guint index, const InsightCustomCommand *command)
{
    QuickAction *action = g_new0(QuickAction, 1);
    action->generation = quick_generation;
    action->index = index;
    action->command = insight_custom_command_copy(command);
    return action;
}

/* EN: Resolve action against fresh on-disk settings, preserve dirty drafts and refuse concurrent jobs before loading/running it.
 * RU: Проверяет action по свежим настройкам на диске, сохраняет черновики и отклоняет параллельный запуск перед загрузкой/Run. */
static void quick_execute(QuickAction *action)
{
    if (!quick_menu || action->generation != quick_generation)
    {
        ui_set_statusbar(TRUE, "%s", insight_tr("Saved command changed. Reload the saved command menu."));
        return;
    }
    gchar *path = g_build_filename(app->configdir, "insight", "custom-commands.conf", NULL);
    GError *error = NULL;
    GPtrArray *list = insight_custom_commands_load(path, &error);
    g_free(path);
    if (!list || action->index >= list->len || !quick_equal(action->command, g_ptr_array_index(list, action->index)))
    {
        ui_set_statusbar(TRUE, "%s", error ? error->message : insight_tr("Saved command changed. Reload the saved command menu."));
        g_clear_error(&error);
        if (list)
            g_ptr_array_unref(list);
        quick_schedule_refresh();
        return;
    }
    show(NULL, NULL);
    CommandView *v = g_object_get_data(G_OBJECT(command_window), "insight-custom-state");
    if (v->process)
    {
        note(v, "A custom command is already running. Stop it or wait for completion.");
        g_ptr_array_unref(list);
        return;
    }
    if (!quick_clean(v))
    {
        note(v, "Save or discard custom command edits before quick launch");
        g_ptr_array_unref(list);
        return;
    }
    guint index = action->index;
    g_ptr_array_unref(v->commands);
    v->commands = list;
    rebuild(v, index);
    run(NULL, v);
}

/* EN: Remember v's successful saved-profile launch, excluding unsaved drafts and preserving settings across tool closure.
 * RU: Запоминает успешный запуск профиля v, исключая черновики и сохраняя состояние после закрытия окна инструмента. */
static void quick_remember(CommandView *v)
{
    gint at = gtk_combo_box_get_active(GTK_COMBO_BOX(v->profiles));
    if (at < 0 || at >= (gint)v->commands->len || !quick_clean(v))
        return;
    insight_custom_command_free(quick_last);
    quick_last = insight_custom_command_copy(g_ptr_array_index(v->commands, at));
    quick_schedule_refresh();
}

/* EN: Validate the edited shortcut against copied saved profiles and current native/plugin assignments; button is unused.
 * RU: Проверяет редактируемое сочетание против копии профилей и текущих нативных/плагинных клавиш; button не используется. */
static void check_shortcut(GtkWidget *button, gpointer data)
{
    (void)button;
    CommandView *v = data;
    GPtrArray *list = clone_commands(v);
    gint at = gtk_combo_box_get_active(GTK_COMBO_BOX(v->profiles));
    if (at < 0)
    {
        at = list->len;
        g_ptr_array_add(list, g_new0(InsightCustomCommand, 1));
    }
    InsightCustomCommand *command = g_ptr_array_index(list, at);
    g_free(command->shortcut);
    command->shortcut = g_strdup(gtk_entry_get_text(GTK_ENTRY(v->shortcut)));
    GError *error = NULL;
    gboolean valid = shortcuts_valid(list, &error);
    note(v, valid ? "Saved command shortcut is available" : error->message);
    g_clear_error(&error);
    g_ptr_array_unref(list);
}

/* EN: Explicitly discard draft fields in view data without touching saved files or numbered arguments; button is unused.
 * RU: Явно отменяет черновые поля окна data без изменения файлов настроек и нумерованных аргументов; button не используется. */
static void discard_draft(GtkWidget *button, gpointer data)
{
    (void)button;
    CommandView *v = data;
    if (v->process)
        return;
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(v->profiles)) >= 0)
        select_profile(GTK_COMBO_BOX(v->profiles), v);
    else
        clear_profile(v);
    note(v, "Custom command draft edits discarded");
}

/* EN: Add per-profile GTK accelerator input and explicit conflict validation to page for v.
 * RU: Добавляет ввод GTK-сочетания профиля и явную проверку конфликтов на page для v. */
static void shortcut_options(CommandView *v, GtkWidget *page)
{
    v->shortcut = entry(page, "Saved command shortcut (optional)", "insight-custom-shortcut",
        "GTK accelerator, for example <Primary><Shift>F9. Existing editor and plugin shortcuts are reserved.", v);
    gtk_entry_set_max_length(GTK_ENTRY(v->shortcut), 128);
    gtk_entry_set_placeholder_text(GTK_ENTRY(v->shortcut), "<Primary><Shift>F9");
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(page), row, FALSE, FALSE, 0);
    button(row, "Check shortcut", "insight-custom-check-shortcut", G_CALLBACK(check_shortcut), v, FALSE);
    button(row, "Discard draft edits", "insight-custom-discard-draft", G_CALLBACK(discard_draft), v, TRUE);
}
