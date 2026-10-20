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
enum { COL_PATH, COL_KEY, COL_DESCRIPTION, COL_INDEX, COL_STATE, COL_COUNT };
typedef struct {
    GtkWidget *dialog, *search, *view, *status;
    GtkListStore *store;
    GtkTreeModel *filter, *sorted;
    GPtrArray *commands;
    gchar **tokens;
} Browser;
/* A menu is detached from its owning menu item in GTK's widget hierarchy.
 * Walk attach widgets explicitly; a retained but destroyed/detached item is
 * never runnable. Ignore popup visibility, but require visible menu items. */
static gboolean command_available(GtkWidget *item)
{
    for (guint depth = 0; item && depth < 18; depth++)
    {
        if (!GTK_IS_MENU_ITEM(item) || !gtk_widget_get_visible(item) || !gtk_widget_is_sensitive(item)) return FALSE;
        GtkWidget *parent = gtk_widget_get_parent(item);
        if (parent == ui_lookup_widget(main_widgets.window, "menubar1")) return TRUE;
        if (!GTK_IS_MENU(parent)) return FALSE;
        item = gtk_menu_get_attach_widget(GTK_MENU(parent));
    }
    return FALSE;
}
static void refresh_command_state(GtkWidget *item)
{
    GPtrArray *menus = g_ptr_array_new_with_free_func(g_object_unref);
    GtkWidget *current = item;
    for (guint depth = 0; current && depth < 18; depth++)
    {
        GtkWidget *parent = gtk_widget_get_parent(current);
        if (!GTK_IS_MENU(parent)) break;
        g_ptr_array_add(menus, g_object_ref(parent));current = gtk_menu_get_attach_widget(GTK_MENU(parent));
    }
    for (guint i = menus->len; i > 0; i--) g_signal_emit_by_name(g_ptr_array_index(menus, i - 1), "show");
    g_ptr_array_free(menus, TRUE);
}
static Command *selected_command(Browser *browser)
{
    GtkTreeIter iter;GtkTreeModel *model;guint index;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->view)), &model, &iter)) return NULL;
    gtk_tree_model_get(model, &iter, COL_INDEX, &index, -1);
    return browser->commands && index < browser->commands->len ? g_ptr_array_index(browser->commands, index) : NULL;
}
static void browser_selection(GtkTreeSelection *selection, gpointer data)
{
    (void)selection;Browser *browser = data;Command *command = selected_command(browser);
    GtkWidget *item = command ? g_weak_ref_get(&command->widget) : NULL;
    gtk_dialog_set_response_sensitive(GTK_DIALOG(browser->dialog), GTK_RESPONSE_ACCEPT, item && command_available(item));
    if (item) g_object_unref(item);
}
static gboolean browser_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    Browser *browser = data;guint index;gtk_tree_model_get(model, iter, COL_INDEX, &index, -1);
    if (!browser->commands || index >= browser->commands->len) return FALSE;
    Command *command = g_ptr_array_index(browser->commands, index);
    for (guint i = 0; browser->tokens && browser->tokens[i]; i++)
        if (*browser->tokens[i] && !strstr(command->folded, browser->tokens[i])) return FALSE;
    return TRUE;
}
static void browser_count(Browser *browser)
{
    guint visible = gtk_tree_model_iter_n_children(browser->filter, NULL);
    gchar *text = g_strdup_printf("%u / %u", visible, browser->commands ? browser->commands->len : 0);
    gtk_label_set_text(GTK_LABEL(browser->status), text);g_free(text);
}
static void browser_changed(GtkEditable *editable, gpointer data)
{
    Browser *browser = data;gchar *query = g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(editable)), -1);
    g_strfreev(browser->tokens);browser->tokens = g_strsplit_set(query, " \t\r\n", -1);g_free(query);
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(browser->filter));
    GtkTreeIter first;
    if (gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(browser->view)), &first))
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->view)), &first);
    browser_selection(NULL, browser);browser_count(browser);
}
static gboolean browser_fill(Browser *browser)
{
    GPtrArray *commands = collect_commands();
    if (!commands) { gtk_label_set_text(GTK_LABEL(browser->status), insight_tr("Command reference exceeds the 10000-item or 1 MiB limit"));return FALSE; }
    gtk_list_store_clear(browser->store);
    if (browser->commands) g_ptr_array_free(browser->commands, TRUE);
    browser->commands = commands;
    for (guint i = 0; i < commands->len; i++)
    {
        Command *command = g_ptr_array_index(commands, i);GtkTreeIter iter;GtkWidget *item = g_weak_ref_get(&command->widget);
        gtk_list_store_append(browser->store, &iter);
        gtk_list_store_set(browser->store, &iter, COL_PATH, command->path, COL_KEY, command->key,
            COL_DESCRIPTION, command->description, COL_INDEX, i,
            COL_STATE, insight_tr(item && command_available(item) ? "Available" : "Unavailable"), -1);
        if (item) g_object_unref(item);
    }
    browser_changed(GTK_EDITABLE(browser->search), browser);return TRUE;
}
static void browser_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data)
{
    (void)view;(void)path;(void)column;Browser *browser = data;
    gtk_dialog_response(GTK_DIALOG(browser->dialog), GTK_RESPONSE_ACCEPT);
}
static void show_browser(GtkMenuItem *menu_item, gpointer data)
{
    (void)menu_item;(void)data;Browser browser = {0};
    browser.dialog = gtk_dialog_new_with_buttons(insight_tr("Command Browser"), GTK_WINDOW(main_widgets.window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, insight_tr("Refresh"), GTK_RESPONSE_APPLY,
        insight_tr("Create Command List"), 201, insight_tr("Create Key List"), 202,
        insight_tr("Close"), GTK_RESPONSE_CLOSE, insight_tr("Run"), GTK_RESPONSE_ACCEPT, NULL);
    g_object_ref_sink(browser.dialog);gtk_widget_set_name(browser.dialog, "insight-command-browser");gtk_window_set_default_size(GTK_WINDOW(browser.dialog), 1000, 540);
    browser.search = gtk_search_entry_new();gtk_widget_set_name(browser.search, "insight-command-search");
    gtk_entry_set_max_length(GTK_ENTRY(browser.search), 256);gtk_entry_set_placeholder_text(GTK_ENTRY(browser.search), insight_tr("Search commands, menu paths and shortcuts"));
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(browser.dialog));gtk_box_pack_start(GTK_BOX(box), browser.search, FALSE, FALSE, 6);
    browser.store = gtk_list_store_new(COL_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
    browser.filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(browser.store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(browser.filter), browser_visible, &browser, NULL);
    browser.sorted = gtk_tree_model_sort_new_with_model(browser.filter);
    browser.view = gtk_tree_view_new_with_model(browser.sorted);gtk_widget_set_name(browser.view, "insight-command-view");
    const gchar *labels[] = {"Menu command", "Shortcut", "Description", "State"};const gint columns[] = {COL_PATH, COL_KEY, COL_DESCRIPTION, COL_STATE};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(insight_tr(labels[i]), renderer, "text", columns[i], NULL);
        const gint widths[] = {430, 140, 250, 110};gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);gtk_tree_view_column_set_fixed_width(column, widths[i]);
        gtk_tree_view_column_set_expand(column, i == 2);
        gtk_tree_view_column_set_resizable(column, TRUE);gtk_tree_view_column_set_sort_column_id(column, columns[i]);gtk_tree_view_append_column(GTK_TREE_VIEW(browser.view), column);
    }
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(browser.view), COL_PATH);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), browser.view);gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    browser.status = gtk_label_new("");gtk_widget_set_name(browser.status, "insight-command-count");g_object_set_data(G_OBJECT(browser.status), "insight-no-i18n", GINT_TO_POINTER(1));gtk_box_pack_start(GTK_BOX(box), browser.status, FALSE, FALSE, 4);
    g_signal_connect(browser.search, "changed", G_CALLBACK(browser_changed), &browser);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser.view)), "changed", G_CALLBACK(browser_selection), &browser);
    g_signal_connect(browser.view, "row-activated", G_CALLBACK(browser_activated), &browser);
    gtk_dialog_set_default_response(GTK_DIALOG(browser.dialog), GTK_RESPONSE_ACCEPT);gtk_entry_set_activates_default(GTK_ENTRY(browser.search), TRUE);
    browser_fill(&browser);gtk_widget_show_all(browser.dialog);gtk_widget_grab_focus(browser.search);
    GtkWidget *run = NULL;gint response;
    for (;;)
    {
        response = gtk_dialog_run(GTK_DIALOG(browser.dialog));
        if (response == GTK_RESPONSE_APPLY) { browser_fill(&browser);continue; }
        if (response != GTK_RESPONSE_ACCEPT) break;
        Command *command = selected_command(&browser);run = command ? g_weak_ref_get(&command->widget) : NULL;
        if (run) refresh_command_state(run);
        if (run && !gtk_menu_item_get_submenu(GTK_MENU_ITEM(run)) && command_available(run)) break;
        if (run) g_object_unref(run);
        run = NULL;browser_fill(&browser);
        gtk_label_set_text(GTK_LABEL(browser.status), insight_tr("Command is no longer available; refreshed the list"));
    }
    /* Disconnect before destroying widgets: selection teardown can emit changed. */
    g_signal_handlers_disconnect_by_data(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser.view)), &browser);
    g_signal_handlers_disconnect_by_data(browser.search, &browser);
    gtk_widget_destroy(browser.dialog);g_object_unref(browser.dialog);
    g_object_unref(browser.sorted);g_object_unref(browser.filter);g_object_unref(browser.store);g_strfreev(browser.tokens);
    if (browser.commands) g_ptr_array_free(browser.commands, TRUE);
    gtk_window_present(GTK_WINDOW(main_widgets.window));
    GeanyDocument *doc = document_get_current();if (doc) gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
    if (run) { if (command_available(run)) gtk_menu_item_activate(GTK_MENU_ITEM(run));g_object_unref(run); }
    else if (response == 201 || response == 202) create_reference(NULL, GINT_TO_POINTER(response == 202));
}

void insight_command_browser_show(void) { show_browser(NULL, NULL); }

void insight_command_list_menu(void)
{
    GtkWidget *menu = ui_lookup_widget(main_widgets.window, "menu_help1_menu");
    GtkWidget *browser = gtk_menu_item_new_with_label("Command Browser…");gtk_widget_set_name(browser, "insight-command-browser-command");
    keybindings_attach_menu(GEANY_KEY_GROUP_HELP, GEANY_KEYS_INSIGHT_COMMANDBROWSER, browser);
    g_signal_connect(browser, "activate", G_CALLBACK(show_browser), NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu), browser);gtk_widget_show(browser);
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
