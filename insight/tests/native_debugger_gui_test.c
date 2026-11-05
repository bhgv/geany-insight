/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "main.h"
#include "document.h"
#include "editor.h"
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

static gchar *target, *observer;
static guint phase;
static gboolean passed;

static GtkWidget *window_named(const gchar *title)
{
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *result = NULL;
    for (GList *it = windows; it && !result; it = it->next)
        if (gtk_widget_get_visible(it->data) &&
            !g_strcmp0(gtk_window_get_title(GTK_WINDOW(it->data)), title))
            result = it->data;
    g_list_free(windows);
    return result;
}

static GtkWidget *find(GtkWidget *widget, const gchar *key)
{
    if (GTK_IS_NOTEBOOK(widget) && !strcmp(key, "inspection notebook"))
        return widget;
    if (GTK_IS_BUTTON(widget) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), key))
        return widget;
    if (GTK_IS_ENTRY(widget) && !g_strcmp0(gtk_entry_get_placeholder_text(GTK_ENTRY(widget)), key))
        return widget;
    if (GTK_IS_TEXT_VIEW(widget))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
        GtkTextIter first, last;
        gtk_text_buffer_get_bounds(buffer, &first, &last);
        gchar *text = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        gboolean found = strstr(text, key) != NULL;
        g_free(text);
        if (found)
            return widget;
    }
    if (GTK_IS_MENU_ITEM(widget))
    {
        if (!g_strcmp0(gtk_menu_item_get_label(GTK_MENU_ITEM(widget)), key))
            return widget;
        GtkWidget *submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget));
        if (submenu)
        {
            GtkWidget *found = find(submenu, key);
            if (found)
                return found;
        }
    }
    if (!GTK_IS_CONTAINER(widget))
        return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *found = NULL;
    for (GList *it = children; it && !found; it = it->next)
        found = find(it->data, key);
    g_list_free(children);
    return found;
}

static gboolean click(GtkWidget *window, const gchar *label)
{
    GtkWidget *button = find(window, label);
    if (!GTK_IS_BUTTON(button) || !gtk_widget_is_sensitive(button))
        return FALSE;
    gtk_button_clicked(GTK_BUTTON(button));
    return TRUE;
}

static gboolean choose(gpointer data)
{
    const gchar *filename = data;
    GtkWidget *window = window_named(filename == target ? "Debug C/C++ Executable" : "Load Lua Observer");
    if (!window)
        return G_SOURCE_CONTINUE;
    gchar *selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(window));
    gboolean ready = !g_strcmp0(selected, filename);
    g_free(selected);
    if (!ready)
    {
        gtk_file_chooser_select_filename(GTK_FILE_CHOOSER(window), filename);
        return G_SOURCE_CONTINUE;
    }
    gtk_dialog_response(GTK_DIALOG(window), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

static gboolean timeout(gpointer data)
{
    (void)data;
    g_printerr("C/C++ GDB GTK test timed out at phase %u\n", phase);
    exit(1);
}

static gboolean drive(gpointer data)
{
    (void)data;
    if (phase == 0)
    {
        GeanyDocument *document = document_get_current();
        if (!DOC_VALID(document))
            return G_SOURCE_CONTINUE;
        GtkWidget *editor = gtk_widget_get_toplevel(GTK_WIDGET(document->editor->sci));
        GtkWidget *menu = find(editor, "Debug C/C++ Executable…");
        if (menu)
        {
            phase++;
            g_timeout_add(100, choose, target);
            gtk_menu_item_activate(GTK_MENU_ITEM(menu));
        }
        return G_SOURCE_CONTINUE;
    }
    GtkWidget *window = window_named("C/C++ Debugger");
    if (!window)
        return G_SOURCE_CONTINUE;
    GtkWidget *tabs = find(window, "inspection notebook");
    if (!GTK_IS_NOTEBOOK(tabs))
        return G_SOURCE_CONTINUE;

    if (phase == 1)
    {
        GtkWidget *location = find(window, "Function or file:line");
        if (location && gtk_widget_is_sensitive(location))
        {
            gtk_entry_set_text(GTK_ENTRY(location), "main");
            if (click(window, "Add Breakpoint"))
                phase++;
        }
    }
    else if (phase == 2)
    {
        GtkWidget *load = find(window, "Load Lua Observer…");
        if (load && gtk_widget_is_sensitive(load))
        {
            phase++;
            g_timeout_add(100, choose, observer);
            gtk_button_clicked(GTK_BUTTON(load));
        }
    }
    else if (phase == 3 && find(window, "Lua observer loaded."))
    {
        GtkWidget *expression = find(window, "GDB watch expression");
        if (!GTK_IS_ENTRY(expression) || !gtk_widget_is_sensitive(expression))
            return G_SOURCE_CONTINUE;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), 1);
        gtk_entry_set_text(GTK_ENTRY(expression), "marker");
        if (click(window, "Add Watch") && click(window, "Run"))
            phase++;
    }
    else if (phase == 4 && find(window, "Lua breakpoint: main") &&
             find(window, "Observed function = main") && click(window, "Step Over"))
        phase++;
    else if (phase == 5 && find(window, "end-stepping-range") &&
             find(window, "marker = 41") && find(window, "Pin member") && click(window, "Locals"))
        phase++;
    else if (phase == 6 && gtk_notebook_get_current_page(GTK_NOTEBOOK(tabs)) == 3 &&
             find(gtk_notebook_get_nth_page(GTK_NOTEBOOK(tabs), 3), "marker = 41"))
    {
        g_print("C/C++ GTK: real GDB breakpoint, Lua observer, watch, step and locals passed.\n");
        passed = TRUE;
        gtk_widget_destroy(window);
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        g_printerr("Usage: test_insight_native_debugger_gui EXECUTABLE OBSERVER PROFILE\n");
        return 2;
    }
    target = g_canonicalize_filename(argv[1], NULL);
    observer = g_canonicalize_filename(argv[2], NULL);
    gchar *profile = g_canonicalize_filename(argv[3], NULL);
    if (g_mkdir_with_parents(profile, 0700))
        return 1;
    gchar *editor_args[] = {argv[0], "--new-instance", "--no-session", "--no-plugins",
                           "--config", profile, observer, NULL};
    g_timeout_add(100, drive, NULL);
    g_timeout_add_seconds(60, timeout, NULL);
    gint result = main_lib(G_N_ELEMENTS(editor_args) - 1, editor_args);
    g_free(profile);
    g_free(observer);
    g_free(target);
    return passed ? result : 1;
}
