/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Validate saved command accelerators against typing keys, native/plugin bindings and other profiles.
 * RU: Проверяет клавиши команд против набора текста, нативных/плагинных сочетаний и других профилей. */
static GtkWidget *quick_menu;
static GtkAccelGroup *quick_accels;
static guint64 quick_generation;
static guint quick_idle;
static InsightCustomCommand *quick_last;

typedef struct {
    guint64 generation;
    guint index;
    InsightCustomCommand *command;
} QuickAction;

/* EN: Compare complete saved command values a/b, normalizing absent optional text to empty strings.
 * RU: Сравнивает все значения сохранённых a/b, считая отсутствующий необязательный текст пустой строкой. */
static gboolean quick_equal(const InsightCustomCommand *a, const InsightCustomCommand *b)
{
    return a && b && !g_strcmp0(a->name, b->name) && !g_strcmp0(a->command, b->command) &&
        !g_strcmp0(a->directory, b->directory) && a->parse_on_finish == b->parse_on_finish &&
        !strcmp(a->diagnostic_pattern ? a->diagnostic_pattern : "", b->diagnostic_pattern ? b->diagnostic_pattern : "") &&
        !strcmp(a->shortcut ? a->shortcut : "", b->shortcut ? b->shortcut : "");
}

/* EN: Parse optional text into normalized key/mods; refuse malformed or plain typing shortcuts with error.
 * RU: Разбирает необязательный text в нормализованные key/mods; отклоняет неверные сочетания и обычный набор с error. */
static gboolean shortcut_parse(const gchar *text, guint *key, GdkModifierType *mods, GError **error)
{
    *key = 0;
    *mods = 0;
    if (!text || !*text)
        return TRUE;
    gtk_accelerator_parse(text, key, mods);
    *key = gdk_keyval_to_lower(*key);
    *mods &= gtk_accelerator_get_default_mod_mask();
    gboolean function = *key >= GDK_KEY_F1 && *key <= GDK_KEY_F35;
    if (!gtk_accelerator_valid(*key, *mods) ||
        (!function && !(*mods & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK | GDK_HYPER_MASK))))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
            "Use a valid GTK accelerator with Ctrl, Alt or Super, or a function key");
        return FALSE;
    }
    return TRUE;
}

/* EN: Detect key/mods already claimed by native/plugin keybindings or another GTK group, including fixed editor actions; exclude this feature's group.
 * RU: Проверяет занятость key/mods нативными/плагинными сочетаниями либо другой GTK-группой и фиксированными действиями, исключая собственную. */
static gboolean shortcut_reserved(guint key, GdkModifierType mods)
{
    if ((!mods && (key == GDK_KEY_F2 || key == GDK_KEY_F10)) ||
        (mods == GDK_MOD1_MASK && key >= GDK_KEY_0 && key <= GDK_KEY_9) ||
        (mods == (GEANY_PRIMARY_MOD_MASK | GDK_SHIFT_MASK) && (key == GDK_KEY_Page_Up || key == GDK_KEY_Page_Down)))
        return TRUE;
    for (guint g = 0; keybinding_groups && g < keybinding_groups->len; g++)
    {
        GeanyKeyGroup *group = g_ptr_array_index(keybinding_groups, g);
        for (guint i = 0; group->key_items && i < group->key_items->len; i++)
        {
            GeanyKeyBinding *binding = g_ptr_array_index(group->key_items, i);
            if (binding && binding->key && gdk_keyval_to_lower(binding->key) == key &&
                (binding->mods & gtk_accelerator_get_default_mod_mask()) == mods)
                return TRUE;
        }
    }
    GSList *groups = gtk_accel_groups_from_object(G_OBJECT(main_widgets.window));
    for (GSList *p = groups; p; p = p->next)
    {
        guint count = 0;
        if (p->data != quick_accels && gtk_accel_group_query(p->data, key, mods, &count) && count)
            return TRUE;
    }
    return FALSE;
}

/* EN: Validate profile shortcuts in list before persistence; reject duplicate or reserved combinations without changing settings.
 * RU: Проверяет сочетания list перед сохранением; отклоняет дубли и занятые комбинации без изменения настроек. */
static gboolean shortcuts_valid(GPtrArray *list, GError **error)
{
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gboolean valid = TRUE;
    for (guint i = 0; valid && i < list->len; i++)
    {
        InsightCustomCommand *command = g_ptr_array_index(list, i);
        guint key;
        GdkModifierType mods;
        valid = shortcut_parse(command->shortcut, &key, &mods, error);
        if (!valid || !key)
            continue;
        gchar *identity = gtk_accelerator_name(key, mods);
        if (shortcut_reserved(key, mods) || g_hash_table_contains(seen, identity))
        {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                "Shortcut is already assigned to another editor, plugin or saved command");
            valid = FALSE;
        }
        g_hash_table_add(seen, identity);
    }
    g_hash_table_unref(seen);
    return valid;
}

/* EN: Test whether v's editable fields still equal its selected saved profile, excluding numbered session arguments.
 * RU: Проверяет совпадение редактируемых полей v с выбранным профилем, исключая нумерованные аргументы сеанса. */
static gboolean quick_clean(CommandView *v)
{
    gint at = gtk_combo_box_get_active(GTK_COMBO_BOX(v->profiles));
    InsightCustomCommand draft = {0};
    draft.name = (gchar *)gtk_entry_get_text(GTK_ENTRY(v->name));
    draft.command = (gchar *)gtk_entry_get_text(GTK_ENTRY(v->command));
    draft.directory = (gchar *)gtk_entry_get_text(GTK_ENTRY(v->directory));
    draft.diagnostic_pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(v->diagnostic_pattern));
    draft.shortcut = (gchar *)gtk_entry_get_text(GTK_ENTRY(v->shortcut));
    draft.parse_on_finish = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->parse_on_finish));
    if (at < 0)
        return !*draft.name && !*draft.command && !*draft.diagnostic_pattern && !*draft.shortcut && !draft.parse_on_finish &&
            (!*draft.directory || !strcmp(draft.directory, "%j"));
    return at < (gint)v->commands->len && quick_equal(&draft, g_ptr_array_index(v->commands, at));
}
