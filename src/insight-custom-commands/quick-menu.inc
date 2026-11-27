/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Build generation-checked saved command menus and accelerators after startup and explicit profile changes.
 * RU: Строит проверяемые по поколению меню и клавиши команд после старта и явных изменений профилей. */

/* EN: Return whether the current live tool owns a running job; no process is started.
 * RU: Проверяет наличие запущенного процесса в действующем окне; ничего не запускает. */
static gboolean quick_busy(void)
{
    CommandView *v = command_window ? g_object_get_data(G_OBJECT(command_window), "insight-custom-state") : NULL;
    return v && v->process;
}

/* EN: Repaint saved menu availability from current process state, preserving action snapshots and repeat eligibility.
 * RU: Обновляет доступность меню по состоянию процесса, сохраняя снимки действий и пригодность повтора. */
static void quick_sensitivity(void)
{
    if (!quick_menu)
        return;
    GList *children = gtk_container_get_children(GTK_CONTAINER(quick_menu));
    for (GList *p = children; p; p = p->next)
    {
        if (g_object_get_data(G_OBJECT(p->data), "insight-quick-action"))
            gtk_widget_set_sensitive(p->data, !quick_busy());
    }
    g_list_free(children);
}

/* EN: Invoke menu action data; item is unused and stale generations cannot launch.
 * RU: Выполняет действие data из меню; item не используется, устаревшие поколения не запускаются. */
static void quick_activated(GtkMenuItem *item, gpointer data)
{
    (void)item;
    quick_execute(data);
}

/* EN: Release accelerator action data when closure is finalized; closure is unused.
 * RU: Освобождает data действия при финализации замыкания; closure не используется. */
static void quick_closure_free(gpointer data, GClosure *closure)
{
    (void)closure;
    quick_action_free(data);
}

/* EN: Activate current saved action data for key/mods if still unreserved; group/object identify GTK dispatch only.
 * Return FALSE for stale/conflicted accelerators, TRUE after an explicit launch attempt, including busy refusals.
 * RU: Выполняет актуальное действие data для key/mods, если клавиши не заняты; group/object обозначают диспетчер GTK.
 * FALSE при устаревшем/конфликтном сочетании, TRUE после явной попытки запуска, включая отказ занятости. */
static gboolean quick_accelerated(GtkAccelGroup *group, GObject *object, guint key, GdkModifierType mods, gpointer data)
{
    (void)group;
    (void)object;
    QuickAction *action = data;
    if (!quick_menu || action->generation != quick_generation || shortcut_reserved(key, mods))
        return FALSE;
    quick_execute(action);
    return TRUE;
}

/* EN: Add a leaf item for saved command/index or repeat; attach an optional already-validated key/mods accelerator.
 * Return borrowed item. Profile names and command excerpts remain literal data during UI language changes.
 * RU: Добавляет пункт команды command/index либо повтор; подключает необязательные проверенные key/mods.
 * Возвращает заимствованный пункт. Имена и фрагменты команд остаются данными при смене языка. */
static GtkWidget *quick_item(guint index, InsightCustomCommand *command, gboolean repeat, guint key, GdkModifierType mods)
{
    gchar *label = repeat ? g_strdup_printf("%s: %s", insight_tr("Repeat saved command"), command->name) : g_strdup(command->name);
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    g_free(label);
    gchar *name = repeat ? g_strdup("insight-repeat-saved-command") : g_strdup_printf("insight-saved-command-%u", index);
    gtk_widget_set_name(item, name);
    g_free(name);
    g_object_set_data(G_OBJECT(item), "insight-no-i18n", GINT_TO_POINTER(1));
    gchar *excerpt = g_utf8_substring(command->command, 0, MIN((glong)256, g_utf8_strlen(command->command, -1)));
    gtk_widget_set_tooltip_text(item, excerpt);
    g_free(excerpt);
    QuickAction *action = quick_action_new(index, command);
    g_object_set_data_full(G_OBJECT(item), "insight-quick-action", action, quick_action_free);
    g_signal_connect(item, "activate", G_CALLBACK(quick_activated), action);
    gtk_menu_shell_append(GTK_MENU_SHELL(quick_menu), item);
    if (key)
    {
        GClosure *closure = g_cclosure_new(G_CALLBACK(quick_accelerated), quick_action_new(index, command), quick_closure_free);
        gtk_accel_group_connect(quick_accels, key, mods, GTK_ACCEL_VISIBLE, closure);
        GtkWidget *child = gtk_bin_get_child(GTK_BIN(item));
        if (GTK_IS_ACCEL_LABEL(child))
            gtk_accel_label_set_accel_closure(GTK_ACCEL_LABEL(child), closure);
        gchar *shortcut = gtk_accelerator_get_label(key, mods);
        g_object_set_data_full(G_OBJECT(item), "insight-command-shortcut", shortcut, g_free);
    }
    return item;
}

/* EN: Refresh menu snapshots and main-window accelerators from bounded on-disk profiles; errors never launch commands.
 * RU: Обновляет снимки меню и клавиши главного окна по ограниченным профилям на диске; ошибки ничего не запускают. */
static void quick_refresh(void)
{
    if (!quick_menu)
        return;
    quick_generation++;
    GList *children = gtk_container_get_children(GTK_CONTAINER(quick_menu));
    for (GList *p = children; p; p = p->next)
        gtk_widget_destroy(p->data);
    g_list_free(children);
    if (quick_accels)
    {
        gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window), quick_accels);
        g_object_unref(quick_accels);
    }
    quick_accels = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(main_widgets.window), quick_accels);
    gchar *path = g_build_filename(app->configdir, "insight", "custom-commands.conf", NULL);
    GError *error = NULL;
    GPtrArray *list = insight_custom_commands_load(path, &error);
    g_free(path);
    gint repeat = -1;
    for (guint i = 0; list && i < list->len; i++)
        if (quick_equal(quick_last, g_ptr_array_index(list, i)))
            repeat = i;
    if (repeat >= 0)
        quick_item(repeat, g_ptr_array_index(list, repeat), TRUE, 0, 0);
    else
    {
        g_clear_pointer(&quick_last, insight_custom_command_free);
        GtkWidget *item = gtk_menu_item_new_with_label(insight_tr("Repeat saved command"));
        gtk_widget_set_name(item, "insight-repeat-saved-command");
        gtk_widget_set_sensitive(item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(quick_menu), item);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(quick_menu), gtk_separator_menu_item_new());
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; list && i < list->len; i++)
    {
        InsightCustomCommand *command = g_ptr_array_index(list, i);
        guint key;
        GdkModifierType mods;
        if (shortcut_parse(command->shortcut, &key, &mods, NULL) && key)
        {
            gchar *identity = gtk_accelerator_name(key, mods);
            guint count = GPOINTER_TO_UINT(g_hash_table_lookup(seen, identity));
            g_hash_table_replace(seen, identity, GUINT_TO_POINTER(count + 1));
        }
    }
    for (guint i = 0; list && i < list->len; i++)
    {
        InsightCustomCommand *command = g_ptr_array_index(list, i);
        guint key;
        GdkModifierType mods;
        GError *failure = NULL;
        gboolean valid = shortcut_parse(command->shortcut, &key, &mods, &failure);
        gchar *identity = key ? gtk_accelerator_name(key, mods) : NULL;
        if (key && (shortcut_reserved(key, mods) || GPOINTER_TO_UINT(g_hash_table_lookup(seen, identity)) > 1))
            valid = FALSE;
        GtkWidget *item = quick_item(i, command, FALSE, valid ? key : 0, mods);
        if (!valid)
            gtk_widget_set_tooltip_text(item, insight_tr("Saved shortcut is unavailable; edit the profile to resolve its conflict or syntax"));
        g_clear_error(&failure);
        g_free(identity);
    }
    g_hash_table_unref(seen);
    if (!list || !list->len)
    {
        GtkWidget *item = gtk_menu_item_new_with_label(insight_tr(error ? "Cannot load saved commands" : "No saved commands"));
        gtk_widget_set_sensitive(item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(quick_menu), item);
    }
    g_clear_error(&error);
    if (list)
        g_ptr_array_unref(list);
    quick_sensitivity();
    gtk_widget_show_all(quick_menu);
}

/* EN: Rebuild once on the main loop; data is unused and destroyed menu owners make the request inert.
 * RU: Однократно перестраивает меню в главном цикле; data не используется, уничтоженные владельцы отменяют действие. */
static gboolean quick_refresh_idle(gpointer data)
{
    (void)data;
    quick_idle = 0;
    quick_refresh();
    return G_SOURCE_REMOVE;
}

/* EN: Coalesce startup, save and locale refreshes without destroying a currently activating menu item.
 * RU: Объединяет обновления старта, сохранения и языка без уничтожения активируемого пункта меню. */
static void quick_schedule_refresh(void)
{
    if (quick_menu && !quick_idle)
        quick_idle = g_idle_add(quick_refresh_idle, NULL);
}

/* EN: Refresh labels/snapshots after locale changes; data is unused and no processes run.
 * RU: Обновляет надписи/снимки после смены языка; data не используется, процессы не запускаются. */
static void quick_locale(gpointer data)
{
    (void)data;
    quick_schedule_refresh();
}

/* EN: Release global menu/session references when owner is destroyed; data is unused.
 * RU: Освобождает глобальные ссылки меню/сеанса при уничтожении owner; data не используется. */
static void quick_destroyed(GtkWidget *owner, gpointer data)
{
    (void)owner;
    (void)data;
    quick_menu = NULL;
    if (quick_idle)
    {
        g_source_remove(quick_idle);
        quick_idle = 0;
    }
    g_clear_object(&quick_accels);
    g_clear_pointer(&quick_last, insight_custom_command_free);
}

/* EN: Queue explicit menu reload; item/data are unused and no process is started.
 * RU: Планирует явное обновление меню; item/data не используются, процесс не запускается. */
static void quick_reload(GtkMenuItem *item, gpointer data)
{
    (void)item;
    (void)data;
    quick_schedule_refresh();
}

/* EN: Add saved launch menu and reload action to tools; startup only loads profiles and never executes them.
 * RU: Добавляет меню запуска и обновление в tools; старт лишь загружает профили, не выполняя их. */
static void quick_setup(GtkWidget *tools)
{
    GtkWidget *parent = gtk_menu_item_new_with_label("");
    gtk_widget_set_name(parent, "insight-saved-commands-menu");
    insight_i18n_key(G_OBJECT(parent), "label", "Run saved command");
    quick_menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), quick_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools), parent);
    gtk_widget_show(parent);
    GtkWidget *reload = gtk_menu_item_new_with_label("");
    gtk_widget_set_name(reload, "insight-reload-saved-commands");
    insight_i18n_key(G_OBJECT(reload), "label", "Reload saved commands");
    g_signal_connect(reload, "activate", G_CALLBACK(quick_reload), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools), reload);
    gtk_widget_show(reload);
    g_signal_connect(quick_menu, "destroy", G_CALLBACK(quick_destroyed), NULL);
    insight_i18n_on_refresh(G_OBJECT(quick_menu), quick_locale, NULL);
    quick_schedule_refresh();
}
