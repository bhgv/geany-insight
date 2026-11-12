/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise the native GDB window, variables, watches and thread stack tree.
 * RU: Проверяет окно GDB, переменные, наблюдения и дерево стеков потоков. */
#include <gtk/gtk.h>
#include "geany.h"
#include "Scintilla.h"
#include "ScintillaWidget.h"
#include "../desktop/glib_compat.h"
#include "main.h"
#include "document.h"
#include "editor.h"
#include "sciwrappers.h"
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
    if (!g_strcmp0(gtk_widget_get_name(widget), key))
        return widget;
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

/* EN: Check that window's automatically refreshed GDB table contains name and value.
 * RU: Проверяет наличие name и value в автоматически обновляемой таблице GDB окна window. */
static gboolean has_local(GtkWidget *window, const gchar *name, const gchar *value)
{
    GtkWidget *view = find(window, "insight-gdb-frame-variables");
    if (!GTK_IS_TREE_VIEW(view))
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *label = NULL, *actual = NULL;
        gtk_tree_model_get(model, &row, 0, &label, 1, &actual, -1);
        gboolean matches = !g_strcmp0(label, name) && !g_strcmp0(actual, value);
        g_free(label);
        g_free(actual);
        if (matches)
            return TRUE;
    }
    return FALSE;
}

static gboolean click(GtkWidget *window, const gchar *label)
{
    GtkWidget *button = find(window, label);
    if (!GTK_IS_BUTTON(button) || !gtk_widget_is_sensitive(button))
        return FALSE;
    gtk_button_clicked(GTK_BUTTON(button));
    return TRUE;
}

/* EN: Check the first breakpoint's column in window and select its row for editing.
 * RU: Проверяет column первой точки окна window и выбирает строку для редактирования. */
static gboolean point_column(GtkWidget *window, gint column, const gchar *expected)
{
    GtkWidget *tree = find(window, "insight-gdb-breakpoints");
    if (!GTK_IS_TREE_VIEW(tree))
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter_first(model, &row))
        return FALSE;
    gchar *value = NULL;
    gtk_tree_model_get(model, &row, column, &value, -1);
    gboolean matches = !g_strcmp0(value, expected);
    g_free(value);
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &row);
    return matches;
}

/* EN: Fill the modal breakpoint rule named by data, or wait until it opens.
 * RU: Заполняет модальное правило точки с названием data либо ждёт открытия диалога. */
static gboolean point_rule(gpointer data)
{
    const gchar *title = data;
    GtkWidget *window = window_named(title);
    if (!window)
        return G_SOURCE_CONTINUE;
    GtkWidget *entry = find(window, "insight-gdb-breakpoint-rule");
    if (!entry)
        return G_SOURCE_CONTINUE;
    gtk_entry_set_text(GTK_ENTRY(entry), !strcmp(title, "Breakpoint condition") ? "1" : "0");
    gtk_dialog_response(GTK_DIALOG(window), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* EN: Verify visible native breakpoint and stopped-line markers in the current source.
 * RU: Проверяет метку точки и подсветку остановки в текущем исходнике. */
static gboolean source_markers(void)
{
    GeanyDocument *document = document_get_current();
    if (!DOC_VALID(document))
        return FALSE;
    gboolean point = FALSE, stop = FALSE;
    for (gint line = 0; line < sci_get_line_count(document->editor->sci); line++)
    {
        guint marks = scintilla_send_message(document->editor->sci, SCI_MARKERGET, line, 0);
        point |= (marks & (1u << 18)) != 0;
        stop |= (marks & (1u << 19)) != 0;
    }
    return point && stop;
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

/* EN: Print bounded text views and string model rows under widget to diagnose an actual GUI timeout.
 * RU: Печатает ограниченные текстовые поля и строки моделей widget для диагностики сбоя интерфейса. */
static void native_gui_dump(GtkWidget *widget)
{
    if (GTK_IS_TEXT_VIEW(widget))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
        GtkTextIter first, last;
        gtk_text_buffer_get_end_iter(buffer, &last);
        gtk_text_buffer_get_iter_at_offset(buffer, &first, MAX(0, gtk_text_iter_get_offset(&last) - 16000));
        gchar *text = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        g_printerr("GDB GUI text %s:\n%s\n", gtk_widget_get_name(widget), text);
        g_free(text);
    }
    if (GTK_IS_TREE_VIEW(widget))
    {
        GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
        GtkTreeIter row;
        guint count = 0;
        for (gboolean valid = model && gtk_tree_model_get_iter_first(model, &row); valid && count++ < 16;
             valid = gtk_tree_model_iter_next(model, &row))
        {
            g_printerr("GDB GUI row %s:", gtk_widget_get_name(widget));
            for (gint i = 0; i < MIN(10, gtk_tree_model_get_n_columns(model)); i++)
            {
                if (gtk_tree_model_get_column_type(model, i) == G_TYPE_STRING)
                {
                    gchar *value = NULL;
                    gtk_tree_model_get(model, &row, i, &value, -1);
                    g_printerr(" [%d]=%.1024s", i, value ? value : "");
                    g_free(value);
                }
            }
            g_printerr("\n");
        }
    }
    if (GTK_IS_CONTAINER(widget))
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList *it = children; it; it = it->next)
            native_gui_dump(it->data);
        g_list_free(children);
    }
}

/* EN: Report the failed phase and bounded debugger widgets; data is unused and the test exits with failure.
 * RU: Показывает этап и ограниченные данные окна отладчика; data не используется, тест завершается с ошибкой. */
static gboolean timeout(gpointer data)
{
    (void)data;
    g_printerr("C/C++ GDB GTK test timed out at phase %u\n", phase);
    GtkWidget *window = window_named("C/C++ Debugger");
    if (window)
        native_gui_dump(window);
    exit(1);
}

#include "native_debugger_catchpoints_gui.inc"
#include "native_debugger_values_gui.inc"
#include "native_debugger_disassembly_gui.inc"
#include "native_debugger_instruction_menu_gui.inc"
#include "native_debugger_remote_gui.inc"

/* EN: Advance the debugger GUI scenario; data is unused. Stop after all checks pass.
 * RU: Продвигает сценарий интерфейса; data не используется. Завершает успешную проверку. */
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

    if (g_getenv("INSIGHT_GDB_REMOTE_GUI"))
        return native_remote_gui_step(window, tabs);
    if (phase == 1 && !native_catch_gui_step(window))
        return G_SOURCE_CONTINUE;
    if (phase >= 84 && phase <= 92)
        return instruction_menu_gui_step(window, tabs);
    if (phase >= 70 && phase <= 83)
        return native_assembly_gui_step(window, tabs);
    if (phase >= 16)
        return native_values_gui_step(window, tabs);
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
             find(window, "insight-gdb-stack") &&
             has_local(window, "marker", "41") && find(window, "marker = 41") &&
             find(window, "Pin member") && click(window, "Locals"))
        phase++;
    else if (phase == 6 && gtk_notebook_get_current_page(GTK_NOTEBOOK(tabs)) == 3 &&
             find(gtk_notebook_get_nth_page(GTK_NOTEBOOK(tabs), 3), "marker = 41"))
    {
        GtkWidget *page = find(window, "insight-gdb-all-thread-stacks");
        while (page && gtk_widget_get_parent(page) != tabs)
            page = gtk_widget_get_parent(page);
        if (!page)
            return G_SOURCE_CONTINUE;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), gtk_notebook_page_num(GTK_NOTEBOOK(tabs), page));
        phase++;
    }
    else if (phase == 7)
    {
        GtkWidget *tree = find(window, "insight-gdb-all-thread-stacks");
        if (!GTK_IS_TREE_VIEW(tree) || !gtk_widget_is_sensitive(tree))
            return G_SOURCE_CONTINUE;
        GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
        GtkTreeIter thread, frame;
        if (!gtk_tree_model_get_iter_first(model, &thread) ||
            !gtk_tree_model_iter_children(model, &frame, &thread))
            return G_SOURCE_CONTINUE;
        gchar *name = NULL;
        gtk_tree_model_get(model, &frame, 1, &name, -1);
        gboolean correct = !g_strcmp0(name, "main");
        g_free(name);
        if (!correct)
            return G_SOURCE_CONTINUE;
        GtkTreePath *path = gtk_tree_model_get_path(model, &frame);
        gtk_tree_view_row_activated(GTK_TREE_VIEW(tree), path, NULL);
        gtk_tree_path_free(path);
        phase++;
    }
    else if (phase == 8 && has_local(window, "marker", "41") && source_markers() &&
             point_column(window, 2, "y") && click(window, "Enable / Disable"))
        phase++;
    else if (phase == 9 && point_column(window, 2, "n") && click(window, "Enable / Disable"))
        phase++;
    else if (phase == 10 && point_column(window, 2, "y"))
    {
        GtkWidget *button = find(window, "Breakpoint condition");
        if (!button || !gtk_widget_is_sensitive(button))
            return G_SOURCE_CONTINUE;
        phase++;
        g_timeout_add(100, point_rule, "Breakpoint condition");
        gtk_button_clicked(GTK_BUTTON(button));
    }
    else if (phase == 11 && point_column(window, 3, "1"))
    {
        GtkWidget *button = find(window, "Ignore first hits");
        if (!button || !gtk_widget_is_sensitive(button))
            return G_SOURCE_CONTINUE;
        phase++;
        g_timeout_add(100, point_rule, "Ignore first hits");
        gtk_button_clicked(GTK_BUTTON(button));
    }
    else if (phase == 12 && point_column(window, 3, "1") && click(window, "Stop"))
        phase++;
    else if (phase == 13)
    {
        GtkWidget *run = find(window, "Run");
        if (run && gtk_widget_is_sensitive(run) && !has_local(window, "marker", "41") &&
            click(window, "Restart"))
            phase++;
    }
    else if (phase == 14 && point_column(window, 3, "1") && click(window, "Step Over"))
        phase++;
    else if (phase == 15 && has_local(window, "marker", "41") &&
             source_markers() && point_column(window, 3, "1"))
    {
        g_print("C/C++ GTK: GDB frames, markers, breakpoint edits, stop and restart passed.\n");
        phase = 70;
        return G_SOURCE_CONTINUE;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Run the editor GUI scenario using target, observer and profile arguments from argv.
 * RU: Запускает сценарий редактора с программой, наблюдателем и профилем из argv. */
int main(int argc, char **argv)
{
    if (argc != 4)
    {
        g_printerr("Usage: test_insight_native_debugger_gui EXECUTABLE OBSERVER PROFILE\n");
        return 2;
    }
    target = insight_canonicalize_filename(argv[1], NULL);
    observer = insight_canonicalize_filename(argv[2], NULL);
    gchar *profile = insight_canonicalize_filename(argv[3], NULL);
    if (g_mkdir_with_parents(profile, 0700))
        return 1;
    gchar *editor_args[] = {argv[0], "--new-instance", "--no-session", "--no-plugins",
                           "--config", profile, observer, NULL};
    g_timeout_add(100, drive, NULL);
    g_timeout_add_seconds(120, timeout, NULL);
    gint result = main_lib(G_N_ELEMENTS(editor_args) - 1, editor_args);
    g_free(profile);
    g_free(observer);
    g_free(target);
    return passed ? result : 1;
}
