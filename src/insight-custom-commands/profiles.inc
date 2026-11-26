/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Edit saved custom commands and retain diagnostic configuration across copy, reorder and failed persistence.
 * RU: Редактирует сохранённые команды, сохраняя диагностику при копировании, перестановке и ошибке записи. */

/* EN: Load the profile selected in box into view data; loading guards programmatic dropdown rebuilds.
 * RU: Загружает профиль из box в окно data; loading защищает программную перестройку списка. */
static void select_profile(GtkComboBox *box, gpointer data)
{
    CommandView *v = data;
    if (v->loading)
        return;
    gint at = gtk_combo_box_get_active(box);
    if (at < 0 || at >= (gint)v->commands->len)
        return;
    InsightCustomCommand *command = g_ptr_array_index(v->commands, at);
    gtk_entry_set_text(GTK_ENTRY(v->name), command->name);
    gtk_entry_set_text(GTK_ENTRY(v->command), command->command);
    gtk_entry_set_text(GTK_ENTRY(v->directory), command->directory);
    gtk_entry_set_text(GTK_ENTRY(v->diagnostic_pattern), command->diagnostic_pattern ? command->diagnostic_pattern : "");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->parse_on_finish), command->parse_on_finish);
}

/* EN: Deep-copy v's saved commands before a transactional edit; caller owns the returned list.
 * RU: Глубоко копирует команды v перед транзакционной правкой; список принадлежит вызывающему. */
static GPtrArray *clone_commands(CommandView *v)
{
    GPtrArray *list = g_ptr_array_new_with_free_func(insight_custom_command_free);
    for (guint i = 0; i < v->commands->len; i++)
        g_ptr_array_add(list, insight_custom_command_copy(g_ptr_array_index(v->commands, i)));
    return list;
}

/* EN: Rebuild v's profile dropdown and select at without callbacks observing incomplete state.
 * RU: Перестраивает список профилей v и выбирает at без наблюдения незавершённого состояния обработчиками. */
static void rebuild(CommandView *v, gint at)
{
    v->loading = TRUE;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(v->profiles));
    for (guint i = 0; i < v->commands->len; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(v->profiles), ((InsightCustomCommand *)g_ptr_array_index(v->commands, i))->name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(v->profiles), at);
    v->loading = FALSE;
    if (at >= 0)
        select_profile(GTK_COMBO_BOX(v->profiles), v);
}

/* EN: Persist owned list before replacing v's saved state and selecting at; failures keep prior state and free list.
 * RU: Сохраняет собственный list до замены состояния v и выбора at; ошибка сохраняет прежнее состояние и освобождает list. */
static gboolean persist(CommandView *v, GPtrArray *list, gint at)
{
    GError *error = NULL;
    gchar *dir = g_path_get_dirname(v->path);
    gint made = g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    if (made || !insight_custom_commands_save(v->path, list, &error))
    {
        note(v, error ? error->message : "Cannot create custom command settings directory");
        g_clear_error(&error);
        g_ptr_array_unref(list);
        return FALSE;
    }
    g_ptr_array_unref(v->commands);
    v->commands = list;
    rebuild(v, at);
    note(v, "Custom commands saved");
    return TRUE;
}

/* EN: Reset v's draft fields without changing saved profiles or numbered argument values.
 * RU: Сбрасывает черновые поля v без изменения профилей и нумерованных аргументов. */
static void clear_profile(CommandView *v)
{
    gtk_entry_set_text(GTK_ENTRY(v->name), "");
    gtk_entry_set_text(GTK_ENTRY(v->command), "");
    gtk_entry_set_text(GTK_ENTRY(v->directory), "%j");
    gtk_entry_set_text(GTK_ENTRY(v->diagnostic_pattern), "");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->parse_on_finish), FALSE);
}

/* EN: Execute button's New/Save/Delete/Move operation on view data, preserving all diagnostic settings and atomic-save semantics.
 * RU: Выполняет New/Save/Delete/Move из button для окна data, сохраняя диагностику и атомарность записи. */
static void edit_profile(GtkWidget *button, gpointer data)
{
    CommandView *v = data;
    if (v->process)
        return;
    gint action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "custom-action"));
    gint at = gtk_combo_box_get_active(GTK_COMBO_BOX(v->profiles));
    if (action == 0)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(v->profiles), -1);
        clear_profile(v);
        gtk_widget_grab_focus(v->name);
        return;
    }
    GPtrArray *list = clone_commands(v);
    if (action == 1)
    {
        InsightCustomCommand *command = g_new0(InsightCustomCommand, 1);
        command->name = g_strdup(gtk_entry_get_text(GTK_ENTRY(v->name)));
        command->command = g_strdup(gtk_entry_get_text(GTK_ENTRY(v->command)));
        command->directory = g_strdup(gtk_entry_get_text(GTK_ENTRY(v->directory)));
        command->diagnostic_pattern = g_strdup(gtk_entry_get_text(GTK_ENTRY(v->diagnostic_pattern)));
        command->parse_on_finish = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->parse_on_finish));
        if (at >= 0)
        {
            insight_custom_command_free(g_ptr_array_index(list, at));
            g_ptr_array_index(list, at) = command;
        }
        else
        {
            at = list->len;
            g_ptr_array_add(list, command);
        }
    }
    else if (at < 0)
    {
        g_ptr_array_unref(list);
        return;
    }
    else if (action == 2)
    {
        g_ptr_array_remove_index(list, at);
        at = MIN(at, (gint)list->len - 1);
    }
    else
    {
        gint next = at + (action == 3 ? -1 : 1);
        if (next < 0 || next >= (gint)list->len)
        {
            g_ptr_array_unref(list);
            return;
        }
        gpointer swap = g_ptr_array_index(list, at);
        g_ptr_array_index(list, at) = g_ptr_array_index(list, next);
        g_ptr_array_index(list, next) = swap;
        at = next;
    }
    if (persist(v, list, at) && at < 0)
        clear_profile(v);
}
