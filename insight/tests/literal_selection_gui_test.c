/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify bracket-or-quote selection through the installed editor's real structural menu.
 * RU: Проверяет выделение скобок/кавычек настоящим структурным меню установленного редактора. */
#include "geany.h"
#include "document.h"
#include "filetypes.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)

/* EN: Find a borrowed widget with name below root, traversing menu subtrees too.
 * RU: Ищет заимствованный виджет name в root, включая подменю. */
static GtkWidget *named(GtkWidget *root, const gchar *name)
{
    if (!g_strcmp0(gtk_widget_get_name(root), name))
        return root;
    if (GTK_IS_MENU_ITEM(root))
    {
        GtkWidget *menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));
        if (menu)
        {
            GtkWidget *found = named(menu, name);
            if (found) return found;
        }
    }
    if (!GTK_IS_CONTAINER(root)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *found = NULL;
    for (GList *it = children; it && !found; it = it->next)
        found = named(it->data, name);
    g_list_free(children);
    return found;
}

/* EN: Replace doc content with text/type, invoke the real item at caret and assert selected bytes equal expected or remain empty.
 * readonly enables the editor's read-only flags; source bytes, Undo and clipboard must stay untouched.
 * RU: Заменяет doc текстом text/type, вызывает настоящее меню у caret и проверяет выделение expected либо его отсутствие.
 * readonly включает только чтение; исходные байты, Undo и буфер обмена должны сохраниться. */
static void check(GeanyDocument *doc, GtkWidget *item, const gchar *type, const gchar *text,
                  gint caret, const gchar *expected, gboolean readonly)
{
    ScintillaObject *sci = doc->editor->sci;
    doc->readonly = FALSE;
    SEND(sci, SCI_SETREADONLY, 0, 0);
    SEND(sci, SCI_CLEARSELECTIONS, 0, 0);
    SEND(sci, SCI_SETSELECTIONMODE, SC_SEL_STREAM, 0);
    SEND(sci, SCI_SETSELECTIONNCARETVIRTUALSPACE, 0, 0);
    SEND(sci, SCI_SETSELECTIONNANCHORVIRTUALSPACE, 0, 0);
    document_set_filetype(doc, filetypes_lookup_by_name(type));
    sci_set_text(sci, text);
    SEND(sci, SCI_SETSEL, caret, caret);
    SEND(sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    doc->readonly = readonly;
    SEND(sci, SCI_SETREADONLY, readonly, 0);
    gtk_menu_item_activate(GTK_MENU_ITEM(item));
    gchar *selected = sci_get_selection_contents(sci);
    g_assert_cmpstr(selected, ==, expected ? expected : "");
    g_free(selected);
    gchar *actual = sci_get_contents(sci, -1);
    g_assert_cmpstr(actual, ==, text);
    g_free(actual);
    g_assert_false(SEND(sci, SCI_CANUNDO, 0, 0));
}

/* EN: Assert exact selected bytes in doc without editing or touching the clipboard.
 * RU: Проверяет точные байты выделения doc без правок и изменения буфера обмена. */
static void selection_is(GeanyDocument *doc, const gchar *expected)
{
    gchar *text = sci_get_selection_contents(doc->editor->sci);
    g_assert_cmpstr(text, ==, expected);
    g_free(text);
}

/* EN: Exercise real structural history menus in window/doc, including reverse anchors, documents and invalidation.
 * RU: Проверяет настоящее меню истории window/doc, включая обратные якоря, документы и сброс состояния. */
static void history_cases(GeanyDocument *doc, GtkWidget *window, GtkWidget *match)
{
    GtkWidget *expand = named(window, "insight-select-enclosing-block");
    GtkWidget *shrink = named(window, "insight-shrink-structural-selection");
    GtkWidget *line = named(window, "insight-select-structural-line");
    GtkWidget *function = named(window, "insight-select-function");
    g_assert_nonnull(expand);
    g_assert_nonnull(shrink);
    g_assert_nonnull(line);
    g_assert_nonnull(function);
    const gchar *body = "void outer() { if (ready) { call(\"abc\"); } }\n";
    gint start = strstr(body, "\"abc") - body;
    ScintillaObject *sci = doc->editor->sci;
    check(doc, match, "C", body, start, "\"abc\"", TRUE);
    gtk_menu_item_activate(GTK_MENU_ITEM(expand));
    selection_is(doc, "(\"abc\")");
    gtk_menu_item_activate(GTK_MENU_ITEM(expand));
    selection_is(doc, "{ call(\"abc\"); }");
    guint key;
    GdkModifierType modifiers;
    gtk_accelerator_parse("<Primary><Alt><Shift>b", &key, &modifiers);
    g_assert_true(gtk_accel_groups_activate(G_OBJECT(window), key, modifiers));
    selection_is(doc, "(\"abc\")");
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    selection_is(doc, "\"abc\"");
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    g_assert_cmpint(sci_get_current_position(sci), ==, start);
    g_assert_cmpint(SEND(sci, SCI_GETANCHOR, 0, 0), ==, start);
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    g_assert_cmpint(sci_get_current_position(sci), ==, start);
    SEND(sci, SCI_SETSEL, start + 3, start + 1);
    gtk_menu_item_activate(GTK_MENU_ITEM(match));
    selection_is(doc, "\"abc\"");
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    g_assert_cmpint(SEND(sci, SCI_GETANCHOR, 0, 0), ==, start + 3);
    g_assert_cmpint(sci_get_current_position(sci), ==, start + 1);
    SEND(sci, SCI_SETSEL, start, start);
    gtk_menu_item_activate(GTK_MENU_ITEM(line));
    selection_is(doc, body);
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    g_assert_cmpint(sci_get_current_position(sci), ==, start);
    gtk_menu_item_activate(GTK_MENU_ITEM(function));
    gchar *selected = sci_get_selection_contents(sci);
    g_assert_nonnull(strstr(selected, "void outer"));
    g_free(selected);
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    g_assert_cmpint(sci_get_current_position(sci), ==, start);
    gtk_menu_item_activate(GTK_MENU_ITEM(match));
    GeanyDocument *other = document_new_file("selection-other.c", filetypes_lookup_by_name("C"), "\"other\"");
    SEND(other->editor->sci, SCI_SETSEL, 0, 0);
    gtk_menu_item_activate(GTK_MENU_ITEM(match));
    selection_is(other, "\"other\"");
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    g_assert_cmpint(sci_get_current_position(other->editor->sci), ==, 0);
    document_show_tab(doc);
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    g_assert_cmpint(sci_get_current_position(sci), ==, start);
    document_set_text_changed(other, FALSE);
    g_assert_true(document_close(other));
    document_show_tab(doc);
    doc->readonly = FALSE;
    SEND(sci, SCI_SETREADONLY, 0, 0);
    gtk_menu_item_activate(GTK_MENU_ITEM(match));
    SEND(sci, SCI_INSERTTEXT, 0, (sptr_t)" ");
    SEND(sci, SCI_UNDO, 0, 0);
    gint anchor = SEND(sci, SCI_GETANCHOR, 0, 0), caret = sci_get_current_position(sci);
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    g_assert_cmpint(SEND(sci, SCI_GETANCHOR, 0, 0), ==, anchor);
    g_assert_cmpint(sci_get_current_position(sci), ==, caret);
    SEND(sci, SCI_SETSEL, start, start);
    gtk_menu_item_activate(GTK_MENU_ITEM(match));
    SEND(sci, SCI_SETSEL, 1, 1);
    while (g_main_context_iteration(NULL, FALSE)) {}
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    g_assert_cmpint(sci_get_current_position(sci), ==, 1);
    SEND(sci, SCI_SETSEL, start, start);
    gtk_menu_item_activate(GTK_MENU_ITEM(match));
    document_set_filetype(doc, filetypes_lookup_by_name("Python"));
    document_set_filetype(doc, filetypes_lookup_by_name("C"));
    anchor = SEND(sci, SCI_GETANCHOR, 0, 0);
    caret = sci_get_current_position(sci);
    gtk_menu_item_activate(GTK_MENU_ITEM(shrink));
    g_assert_cmpint(SEND(sci, SCI_GETANCHOR, 0, 0), ==, anchor);
    g_assert_cmpint(sci_get_current_position(sci), ==, caret);
    gchar *actual = sci_get_contents(sci, -1);
    g_assert_cmpstr(actual, ==, body);
    g_free(actual);
    g_print("Structural history: nested expand/shrink, accelerator, reverse anchors, line/function, per-document history, read-only, edit+Undo and manual/language invalidation passed\n");
}

/* EN: Drive supported dialects and literal exclusions once the main editor is ready; data is unused.
 * RU: Проверяет диалекты и исключения литералов после запуска редактора; data не используется. */
static gboolean run(gpointer data)
{
    (void)data;
    GeanyDocument *doc = document_get_current();
    if (!doc) return G_SOURCE_CONTINUE;
    GtkWidget *window = gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
    GtkWidget *item = named(window, "insight-select-matching");
    g_assert_nonnull(item);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "literal-sentinel", -1);
    const gchar *cpp = "// я\r\nauto s = R\"tag(first \"quote\"\r\nsecond)tag\";\r\n";
    const gchar *raw = strstr(cpp, "R\"");
    gchar *literal = g_strndup(raw, strstr(raw, ")tag\"") + 5 - raw);
    check(doc, item, "C++", cpp, raw - cpp + 1, literal, TRUE);
    g_free(literal);
    check(doc, item, "C", "f(\"я \\\"ok\\\"\");", 2, "\"я \\\"ok\\\"\"", FALSE);
    check(doc, item, "Verilog", "$display(\"data\");", 9, "\"data\"", FALSE);
    check(doc, item, "Verilog", "8'hff;", 1, NULL, FALSE);
    check(doc, item, "Python", "x = '''first\nsecond'''", 4, "'''first\nsecond'''", FALSE);
    check(doc, item, "Lua", "x = [=[first\nsecond]=]", 4, "[=[first\nsecond]=]", FALSE);
    check(doc, item, "C", "/* \"comment\" */", 3, NULL, FALSE);
    check(doc, item, "C", "\"unfinished", 0, NULL, FALSE);
    check(doc, item, "C", "f(x)", 1, "(x)", FALSE);
    history_cases(doc, window, item);
    gchar *clipboard = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    g_assert_cmpstr(clipboard, ==, "literal-sentinel");
    g_free(clipboard);
    g_print("Literal GUI: real menu, C++ raw/CRLF/UTF-8, escaped C, Verilog, Python/Lua, read-only, exclusions and bracket fallback passed\n");
    document_set_text_changed(doc, FALSE);
    gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
    return G_SOURCE_REMOVE;
}

/* EN: Fail a stalled native editor test; data is unused.
 * RU: Завершает зависшую проверку редактора ошибкой; data не используется. */
static gboolean timeout(gpointer data)
{
    (void)data;
    g_error("Literal selection GUI timeout");
    return G_SOURCE_REMOVE;
}

/* EN: Start an isolated installed editor with a temporary config; skip unless explicit GUI testing is enabled.
 * RU: Запускает установленный редактор с временным конфигом; пропускает проверку без явного разрешения GUI. */
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
    gchar *directory = g_dir_make_tmp("literal-gui-XXXXXX", NULL);
    gchar *config = g_build_filename(directory, "geany.conf", NULL);
    gchar *path = g_build_filename(directory, "literal.cpp", NULL);
    g_assert_true(g_file_set_contents(path, "int main() {}", -1, NULL));
    g_assert_true(g_file_set_contents(config, "[geany]\nconfirm_exit=false\n", -1, NULL));
    gchar *argv[] = {"literal-selection", "--new-instance", "--no-session", "--config", directory, path, NULL};
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_timeout_add(400, run, NULL);
    g_timeout_add_seconds(35, timeout, NULL);
    return main_lib(6, argv);
}
