/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Persist unified paste options and restore controls after failed writes.
 * RU: Сохраняет единые настройки вставки и восстанавливает элементы при ошибке записи. */

/* EN: Persist next smart mode, position and select override atomically; update runtime generations only on success.
 * RU: Атомарно сохраняет smart, position и select; меняет состояние и поколение только при успехе. */
static gboolean save_paste_preferences(gboolean smart, PastePosition position, gboolean select, GError **error)
{
    GKeyFile *key = g_key_file_new();
    gchar *path = config_path();
    g_key_file_load_from_file(key, path, 0, NULL);
    g_key_file_set_boolean(key, "typing", "intelligent-paste", smart);
    g_key_file_set_integer(key, "typing", "paste-position", position);
    g_key_file_set_boolean(key, "typing", "always-select-pasted-text", select);
    gsize length;
    gchar *text = g_key_file_to_data(key, &length, NULL);
    gboolean ok = g_file_set_contents(path, text, length, error);
    if (ok)
    {
        enabled = smart;
        paste_position = position;
        always_select = select;
        preference_generation++;
    }
    g_free(text);
    g_free(path);
    g_key_file_unref(key);
    return ok;
}

/* EN: Persist the intelligent-paste checkbox item; data is unused. Restore the previous check on failure.
 * RU: Сохраняет флажок умной вставки item; data не используется. При ошибке возвращает прежний флажок. */
static void toggle(GtkCheckMenuItem *item, gpointer data)
{
    (void)data;
    GError *error = NULL;
    if (!save_paste_preferences(gtk_check_menu_item_get_active(item), paste_position, always_select, &error))
    {
        g_signal_handlers_block_by_func(item, toggle, NULL);
        gtk_check_menu_item_set_active(item, enabled);
        g_signal_handlers_unblock_by_func(item, toggle, NULL);
        ui_set_statusbar(TRUE, "Cannot save intelligent paste: %s", error->message);
        g_clear_error(&error);
    }
}

/* EN: Persist item's always-select override; data is unused. Failed writes leave runtime/menu state unchanged.
 * RU: Сохраняет приоритетный флажок выделения item; data не используется. Ошибка сохраняет состояние меню и режима. */
static void select_toggled(GtkCheckMenuItem *item, gpointer data)
{
    (void)data;
    GError *error = NULL;
    if (!save_paste_preferences(enabled, paste_position, gtk_check_menu_item_get_active(item), &error))
    {
        g_signal_handlers_block_by_func(item, select_toggled, NULL);
        gtk_check_menu_item_set_active(item, always_select);
        g_signal_handlers_unblock_by_func(item, select_toggled, NULL);
        ui_set_statusbar(TRUE, "Cannot save paste position: %s", error->message);
        g_clear_error(&error);
    }
}

/* EN: Persist selected radio item using position encoded in data; restore the old group selection on failure.
 * RU: Сохраняет выбранный item с позицией из data; при ошибке восстанавливает прежний выбор группы. */
static void position_toggled(GtkCheckMenuItem *item, gpointer data)
{
    if (!gtk_check_menu_item_get_active(item)) return;
    PastePosition next = GPOINTER_TO_INT(data);
    if (next == paste_position) return;
    GError *error = NULL;
    if (!save_paste_preferences(enabled, next, always_select, &error))
    {
        GSList *group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
        for (GSList *p = group; p; p = p->next)
            g_signal_handlers_block_by_func(p->data, position_toggled,
                g_object_get_data(G_OBJECT(p->data), "paste-position"));
        for (GSList *p = group; p; p = p->next)
            gtk_check_menu_item_set_active(p->data,
                GPOINTER_TO_INT(g_object_get_data(G_OBJECT(p->data), "paste-position")) == (gint)paste_position);
        for (GSList *p = group; p; p = p->next)
            g_signal_handlers_unblock_by_func(p->data, position_toggled,
                g_object_get_data(G_OBJECT(p->data), "paste-position"));
        ui_set_statusbar(TRUE, "Cannot save paste position: %s", error->message);
        g_clear_error(&error);
    }
}

/* EN: Add legacy-compatible intelligent and unified position options to menu, including an independent select override.
 * RU: Добавляет совместимые прежние и единые настройки вставки в menu, включая независимое приоритетное выделение. */
void insight_paste_menu(GtkWidget *menu)
{
    load();
    GtkWidget *item = gtk_check_menu_item_new_with_label("Intelligent paste");
    gtk_widget_set_name(item, "insight-intelligent-paste-option");
    gtk_widget_set_tooltip_text(item, "Place whole lines above or below the current line and match its indentation");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), enabled);
    g_signal_connect(item, "toggled", G_CALLBACK(toggle), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    GtkWidget *parent = gtk_menu_item_new_with_label("After paste");
    gtk_widget_set_name(parent, "insight-paste-position");
    gtk_widget_set_tooltip_text(parent, "Cursor or selection after ordinary or intelligent text paste");
    GtkWidget *submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), submenu);
    const gchar *labels[] = {"Cursor at end of pasted text", "Cursor at start of pasted text", "Select pasted text"};
    GSList *group = NULL;
    for (gint i = 0; i < PASTE_POSITION_COUNT; i++)
    {
        GtkWidget *choice = gtk_radio_menu_item_new_with_label(group, labels[i]);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(choice));
        gchar *name = g_strdup_printf("insight-paste-position-%d", i);
        gtk_widget_set_name(choice, name);
        g_free(name);
        g_object_set_data(G_OBJECT(choice), "paste-position", GINT_TO_POINTER(i));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(choice), i == (gint)paste_position);
        g_signal_connect(choice, "toggled", G_CALLBACK(position_toggled), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), choice);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(submenu), gtk_separator_menu_item_new());
    GtkWidget *select = gtk_check_menu_item_new_with_label("Always select pasted text");
    gtk_widget_set_name(select, "insight-paste-always-select");
    gtk_widget_set_tooltip_text(select, "Override the cursor position choice without changing indentation rules");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(select), always_select);
    g_signal_connect(select, "toggled", G_CALLBACK(select_toggled), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(submenu), select);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent);
}
