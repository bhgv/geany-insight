/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-command-list.h"
#include "document.h"
#include "keybindings.h"
#include "keybindingsprivate.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include <string.h>
#define CATALOG_ROWS 10000
#define CATALOG_BYTES (1024 * 1024)
typedef struct {
    GWeakRef widget;
    gchar *path, *key, *description, *folded;
} Command;
static void command_free(gpointer data)
{
    Command *command = data;g_weak_ref_clear(&command->widget);
    g_free(command->path);g_free(command->key);g_free(command->description);g_free(command->folded);g_free(command);
}
static gchar *field(const gchar *text)
{
    gchar *value = g_strdup(text ? text : "");
    for (gchar *p = value; *p; p++) if (*p == '\t' || *p == '\r' || *p == '\n') *p = ' ';
    return value;
}
static gchar *menu_label(GtkWidget *item)
{
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(item));
    const gchar *text = GTK_IS_LABEL(child) ? gtk_label_get_text(GTK_LABEL(child)) : gtk_menu_item_get_label(GTK_MENU_ITEM(item));
    return field(text);
}
static GHashTable *shortcut_map(void)
{
    GHashTable *map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    for (guint g = 0; keybinding_groups && g < keybinding_groups->len; g++)
    {
        GeanyKeyGroup *group = g_ptr_array_index(keybinding_groups, g);
        for (guint i = 0; group->key_items && i < group->key_items->len; i++)
        {
            GeanyKeyBinding *key = g_ptr_array_index(group->key_items, i);
            if (!key || !key->menu_item || !key->key) continue;
            gchar *label = gtk_accelerator_get_label(key->key, key->mods);
            gchar *previous = g_hash_table_lookup(map, key->menu_item);
            gchar *combined = previous ? g_strconcat(previous, "; ", label, NULL) : g_strdup(label);
            g_hash_table_replace(map, key->menu_item, combined);g_free(label);
        }
    }
    return map;
}
static gboolean collect_menu(GtkWidget *menu, const gchar *prefix, GHashTable *shortcuts, GPtrArray *commands, guint depth, guint *visited, gsize *bytes)
{
    if (depth > 16) return FALSE;
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));gboolean valid = TRUE;
    for (GList *p = children; p && valid; p = p->next)
    {
        GtkWidget *item = p->data;
        if (++*visited > CATALOG_ROWS) { valid = FALSE;break; }
        if (!GTK_IS_MENU_ITEM(item) || GTK_IS_SEPARATOR_MENU_ITEM(item) || !gtk_widget_get_visible(item)) continue;
        gchar *label = menu_label(item);
        if (!*label) { g_free(label);continue; }
        gchar *path = *prefix ? g_strconcat(prefix, " → ", label, NULL) : g_strdup(label);g_free(label);
        if (strlen(path) > 8192 || !g_utf8_validate(path, -1, NULL)) { g_free(path);valid = FALSE;break; }
        GtkWidget *sub = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
        if (sub) { valid = collect_menu(sub, path, shortcuts, commands, depth + 1, visited, bytes);g_free(path);continue; }
        Command *command = g_new0(Command, 1);g_weak_ref_init(&command->widget, item);command->path = path;
        command->key = g_strdup(g_hash_table_lookup(shortcuts, item));
        gchar *tooltip = gtk_widget_get_tooltip_text(item);command->description = field(tooltip);g_free(tooltip);
        gsize size = strlen(command->path) + (command->key ? strlen(command->key) : 0) + strlen(command->description) + 3;
        if (size > CATALOG_BYTES || *bytes > CATALOG_BYTES - size) { command_free(command);valid = FALSE;break; }
        *bytes += size;
        gchar *search = g_strjoin(" ", command->path, command->key ? command->key : "", command->description, NULL);
        command->folded = g_utf8_casefold(search, -1);g_free(search);g_ptr_array_add(commands, command);
    }
    g_list_free(children);return valid;
}
static GPtrArray *collect_commands(void)
{
    GPtrArray *commands = g_ptr_array_new_with_free_func(command_free);
    GHashTable *shortcuts = shortcut_map();guint visited = 0;gsize bytes = 0;
    gboolean valid = collect_menu(ui_lookup_widget(main_widgets.window, "menubar1"), "", shortcuts, commands, 0, &visited, &bytes);
    g_hash_table_unref(shortcuts);
    if (!valid) { g_ptr_array_free(commands, TRUE);return NULL; }
    return commands;
}
static gboolean append_row(GString *text, const gchar *a, const gchar *b, const gchar *c)
{
    gchar *left = field(a), *middle = field(b), *right = field(c);
    gsize bytes = strlen(left) + strlen(middle) + strlen(right) + 3;
    gboolean valid = bytes <= CATALOG_BYTES && text->len <= CATALOG_BYTES - bytes &&
        g_utf8_validate(left, -1, NULL) && g_utf8_validate(middle, -1, NULL) && g_utf8_validate(right, -1, NULL);
    if (valid) g_string_append_printf(text, "%s\t%s\t%s\n", left, middle, right);
    g_free(left);g_free(middle);g_free(right);return valid;
}
static void create_reference(GtkMenuItem *item, gpointer data)
{
    (void)item;gboolean keys = GPOINTER_TO_INT(data);gboolean valid = TRUE;
    GString *text = g_string_new(insight_tr(keys ? "Key List" : "Command List"));g_string_append(text, "\n\n");
    if (keys)
    {
        valid = append_row(text, insight_tr("Group"), insight_tr("Command"), insight_tr("Shortcut"));guint count = 0;
        for (guint g = 0; valid && keybinding_groups && g < keybinding_groups->len; g++)
        {
            GeanyKeyGroup *group = g_ptr_array_index(keybinding_groups, g);
            for (guint i = 0; valid && group->key_items && i < group->key_items->len; i++)
            {
                GeanyKeyBinding *key = g_ptr_array_index(group->key_items, i);if (!key) continue;
                gchar *label = keybindings_get_label(key);
                gchar *shortcut = key->key ? gtk_accelerator_get_label(key->key, key->mods) : g_strdup(insight_tr("Unassigned"));
                valid = ++count <= CATALOG_ROWS && append_row(text, insight_tr(group->label), insight_tr(label), shortcut);
                g_free(shortcut);g_free(label);
            }
        }
    }
    else
    {
        GPtrArray *commands = collect_commands();valid = commands != NULL;
        if (valid) valid = append_row(text, insight_tr("Menu command"), insight_tr("Shortcut"), insight_tr("Description"));
        for (guint i = 0; valid && i < commands->len; i++)
        {
            Command *command = g_ptr_array_index(commands, i);valid = append_row(text, command->path, command->key, command->description);
        }
        if (commands) g_ptr_array_free(commands, TRUE);
    }
    if (valid)
    {
        GeanyDocument *doc = document_new_file(keys ? "Keylist.txt" : "Command List.txt", NULL, text->str);
        if (doc) document_set_text_changed(doc, TRUE);
    }
    else ui_set_statusbar(TRUE, "%s", insight_tr("Command reference exceeds the 10000-item or 1 MiB limit"));
    g_string_free(text, TRUE);
}
void insight_command_list_menu(void)
{
    GtkWidget *menu = ui_lookup_widget(main_widgets.window, "menu_help1_menu");
    const gchar *labels[] = {"Create Command List", "Create Key List"};
    const gchar *ids[] = {"insight-create-command-list", "insight-create-key-list"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *entry = gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(entry, ids[i]);
        g_signal_connect(entry, "activate", G_CALLBACK(create_reference), GINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);gtk_widget_show(entry);
    }
    GtkWidget *help = gtk_menu_item_new_with_label("Command Reference Help");
    g_signal_connect(help, "activate", G_CALLBACK(insight_help_show), "command-reference.md");gtk_menu_shell_append(GTK_MENU_SHELL(menu), help);gtk_widget_show(help);
}
