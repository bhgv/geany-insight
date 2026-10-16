/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static gchar *directory, *profile, *source, *denied;
static GeanyDocument *doc;
static guint phase;
static gboolean restarted, capture_started, capture_done;
static GtkWidget *option, *window;
static const gchar *block = "    first();\n        second();\n";
static const gchar *base = "void f() {\n    target();\n}\n";
static GtkWidget *named(GtkWidget *w, const gchar *name)
{
    if (!g_strcmp0(gtk_widget_get_name(w), name)) return w;
    if (GTK_IS_MENU_ITEM(w))
    {
        GtkWidget *m = gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));
        GtkWidget *v = m ? named(m, name) : NULL;
        if (v) return v;
    }
    if (GTK_IS_CONTAINER(w))
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(w));
        GtkWidget *v = NULL;
        for (GList *p = children; p && !v; p = p->next) v = named(p->data, name);
        g_list_free(children);return v;
    }
    return NULL;
}
static void text_is(const gchar *expected)
{
    gchar *actual = sci_get_contents(doc->editor->sci, sci_get_length(doc->editor->sci) + 2);
    g_assert_cmpstr(actual, ==, expected);g_free(actual);
}
static void reset(const gchar *text, gint caret)
{
    SEND(doc->editor->sci, SCI_SETTEXT, 0, (sptr_t)text);
    SEND(doc->editor->sci, SCI_SETSEL, caret, caret);
    SEND(doc->editor->sci, SCI_SETUSETABS, FALSE, 0);
    SEND(doc->editor->sci, SCI_SETTABWIDTH, 4, 0);
    SEND(doc->editor->sci, SCI_SETEOLMODE, SC_EOL_LF, 0);
    SEND(doc->editor->sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
}
static void paste(const gchar *text)
{
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
    keybindings_send_command(GEANY_KEY_GROUP_CLIPBOARD, GEANY_KEYS_CLIPBOARD_PASTE);
}
static void undo(void)
{
    keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_UNDO);
}
static void line_action(const gchar *id)
{
    gchar *name = g_strconcat("insight-", id, NULL);
    GtkWidget *item = named(window, name);g_assert_nonnull(item);
    gtk_menu_item_activate(GTK_MENU_ITEM(item));g_free(name);
}
static void check_line_moves(void)
{
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option), FALSE);
    reset("top\n    move();\n", 10);
    keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_MOVELINEUP);
    text_is("    move();\ntop\n");undo();text_is("top\n    move();\n");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option), TRUE);
    reset("top\n    move();\n", 10);
    keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_MOVELINEUP);
    text_is("move();\ntop\n");g_assert_cmpint(sci_get_current_position(doc->editor->sci), ==, 2);
    undo();text_is("top\n    move();\n");
    keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_REDO);
    text_is("move();\ntop\n");
    const gchar *multi = "    a();\n        b();\n        bottom();\n";
    reset(multi, 0);SEND(doc->editor->sci, SCI_SETSEL, strlen("    a();\n        b();\n"), 0);
    line_action("drag-line-down");
    text_is("        bottom();\n        a();\n            b();\n");
    g_assert_cmpint(SEND(doc->editor->sci, SCI_GETANCHOR, 0, 0), >, sci_get_current_position(doc->editor->sci));
    undo();text_is(multi);
    const gchar *more = "head\n  one\n  two\n  three\n  four\n  five\n    move\n";
    reset(more, strstr(more, "move") - more + 2);line_action("drag-line-up-more");
    text_is("head\n  move\n  one\n  two\n  three\n  four\n  five\n");undo();text_is(more);
    more = "    move\n  one\n  two\n  three\n  four\n  five\nend\n";
    reset(more, 6);line_action("drag-line-down-more");
    text_is("  one\n  two\n  three\n  four\n  five\n  move\nend\n");undo();text_is(more);
    reset("top\n    last();", 10);line_action("drag-line-up");
    text_is("last();\ntop");undo();text_is("top\n    last();");
    reset("top\r\n\tПривет();\r\n", strlen("top\r\n\tПри"));
    SEND(doc->editor->sci, SCI_SETEOLMODE, SC_EOL_CRLF, 0);
    SEND(doc->editor->sci, SCI_SETUSETABS, TRUE, 0);
    line_action("drag-line-up");text_is("Привет();\r\ntop\r\n");
    g_assert_cmpint(sci_get_current_position(doc->editor->sci), ==, strlen("При"));
    reset("only\n", 1);line_action("drag-line-up-more");text_is("only\n");
    g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
    SEND(doc->editor->sci, SCI_SETREADONLY, TRUE, 0);line_action("drag-line-down");text_is("only\n");
    SEND(doc->editor->sci, SCI_SETREADONLY, FALSE, 0);
    reset("top\n    move\n", 5);SEND(doc->editor->sci, SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE, 0);
    line_action("drag-line-up");text_is("top\n    move\n");
    SEND(doc->editor->sci, SCI_SETSELECTIONMODE, SC_SEL_STREAM, 0);
}
static void clipboard_is(const gchar *expected)
{
    gchar *text = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    g_assert_cmpstr(text, ==, expected);g_free(text);
}
static void clipboard_set(const gchar *text)
{
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
}
static void check_line_clipboard(void)
{
    const gchar *original = "one\r\ntwo\r\nthree";
    reset(original, 2);
    keybindings_send_command(GEANY_KEY_GROUP_CLIPBOARD, GEANY_KEYS_CLIPBOARD_COPYLINE);
    clipboard_is("one\r\n");text_is(original);
    g_assert_cmpint(sci_get_selection_start(doc->editor->sci), ==, 0);
    g_assert_cmpint(sci_get_selection_end(doc->editor->sci), ==, 5);
    line_action("copy-line");clipboard_is("one\r\ntwo\r\n");
    line_action("copy-line");clipboard_is(original);
    line_action("copy-line");clipboard_is(original);
    g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
    reset("one\ntwo\nthree\n", 0);SEND(doc->editor->sci, SCI_SETSEL, 6, 1);
    line_action("copy-line");clipboard_is("one\ntwo\n");
    line_action("copy-line");clipboard_is("one\ntwo\nthree\n");
    original = "αβ\tПривет();\r\nnext\r\n";
    gint caret = strlen("αβ\tПри");
    reset(original, caret);SEND(doc->editor->sci, SCI_SETSEL, 0, caret);
    line_action("copy-line-right");clipboard_is("вет();");text_is(original);
    g_assert_cmpint(SEND(doc->editor->sci, SCI_GETANCHOR, 0, 0), ==, 0);
    g_assert_cmpint(sci_get_current_position(doc->editor->sci), ==, caret);
    g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
    line_action("cut-line-right");clipboard_is("вет();");text_is("αβ\tПри\r\nnext\r\n");
    undo();text_is(original);
    keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_REDO);
    text_is("αβ\tПри\r\nnext\r\n");
    reset(original, caret);line_action("cut-line-left");clipboard_is("αβ\tПри");
    text_is("вет();\r\nnext\r\n");undo();text_is(original);
    reset("one\ntwo\nthree\n", 0);SEND(doc->editor->sci, SCI_SETSEL, 0, 5);
    keybindings_send_command(GEANY_KEY_GROUP_CLIPBOARD, GEANY_KEYS_CLIPBOARD_CUTLINE);
    clipboard_is("two\n");text_is("one\nthree\n");undo();text_is("one\ntwo\nthree\n");
    reset("one\nlast", 6);line_action("cut-line");clipboard_is("last");text_is("one\n");
    undo();text_is("one\nlast");
    reset("a\n\nb", 2);line_action("cut-line");clipboard_is("\n");text_is("a\nb");undo();text_is("a\n\nb");
    clipboard_set("unchanged");reset("abc\n", 0);line_action("cut-line-left");clipboard_is("unchanged");
    SEND(doc->editor->sci, SCI_SETSEL, 3, 3);
    line_action("copy-line-right");line_action("cut-line-right");clipboard_is("unchanged");text_is("abc\n");
    reset("abc\n", 4);line_action("copy-line");clipboard_is("unchanged");
    g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
    reset("abc\n", 1);SEND(doc->editor->sci, SCI_SETREADONLY, TRUE, 0);
    line_action("cut-line");line_action("cut-line-left");line_action("cut-line-right");
    line_action("cut-selection-or-paste");clipboard_is("unchanged");text_is("abc\n");
    line_action("copy-line-right");clipboard_is("bc");
    line_action("copy-line");clipboard_is("abc\n");text_is("abc\n");
    SEND(doc->editor->sci, SCI_SETREADONLY, FALSE, 0);
    reset("abc\n", 1);clipboard_set("unchanged");
    SEND(doc->editor->sci, SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE, 0);
    line_action("cut-line");line_action("copy-line");clipboard_is("unchanged");text_is("abc\n");
    SEND(doc->editor->sci, SCI_SETSELECTIONMODE, SC_SEL_STREAM, 0);
    SEND(doc->editor->sci, SCI_ADDSELECTION, 3, 3);
    line_action("copy-line-right");line_action("cut-line-right");clipboard_is("unchanged");text_is("abc\n");
    gchar *large = g_strnfill(1024 * 1024 + 1, 'x');reset(large, 0);
    line_action("copy-line-right");clipboard_is("unchanged");text_is(large);g_free(large);
    g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
    reset("", 0);SEND(doc->editor->sci, SCI_ADDTEXT, 4, (sptr_t)"a\0b\n");
    SEND(doc->editor->sci, SCI_SETSEL, 0, 0);line_action("copy-line");
    g_assert_cmpint(sci_get_length(doc->editor->sci), ==, 4);
    reset("\xff\n", 0);line_action("copy-line");
    reset("valid\n", 0);clipboard_is("unchanged");
}
static void finish(void)
{
    SEND(doc->editor->sci, SCI_SETREADONLY, FALSE, 0);
    SEND(doc->editor->sci, SCI_SETSAVEPOINT, 0, 0);
    gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
}
static gboolean capture_menu(gpointer data)
{
    GtkWidget *menu = data;
    const gchar *path = g_getenv("GEANY_LINE_OPERATIONS_CAPTURE");
    GdkWindow *surface = gtk_widget_get_window(menu);
    g_assert_nonnull(surface);
    GdkPixbuf *image = gdk_pixbuf_get_from_window(surface, 0, 0,
        gtk_widget_get_allocated_width(menu), gtk_widget_get_allocated_height(menu));
    g_assert_nonnull(image);
    g_assert_true(gdk_pixbuf_save(image, path, "jpeg", NULL, "quality", "82", NULL));
    g_object_unref(image);gtk_menu_popdown(GTK_MENU(menu));
    capture_done = TRUE;return G_SOURCE_REMOVE;
}
static gboolean step(gpointer data)
{
    (void)data;
    if (!document_get_current()) return G_SOURCE_CONTINUE;
    g_printerr("intelligent paste phase %u\n", phase);
    if (phase == 0)
    {
        doc = document_open_file(source, FALSE, NULL, NULL);g_assert_nonnull(doc);
        window = gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
        option = named(window, "insight-intelligent-paste-option");g_assert_nonnull(option);
        g_assert_cmpint(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(option)), ==, restarted);
        if (!restarted && g_getenv("GEANY_LINE_OPERATIONS_CAPTURE") && !capture_done)
        {
            if (!capture_started)
            {
                GtkWidget *parent = named(window, "insight-line-operations");g_assert_nonnull(parent);
                GtkWidget *menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(parent));g_assert_nonnull(menu);
                gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(doc->editor->sci),
                    GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
                capture_started = TRUE;g_timeout_add(150, capture_menu, menu);
            }
            return G_SOURCE_CONTINUE;
        }
        if (restarted) { finish();return G_SOURCE_REMOVE; }
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option), TRUE);
        reset(base, 18);paste(block);
    }
    else if (phase == 1)
    {
        text_is("void f() {\n    first();\n        second();\n    target();\n}\n");
        undo();text_is(base);
        keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_REDO);
        text_is("void f() {\n    first();\n        second();\n    target();\n}\n");
        reset(base, 24); /* end of target line: paste below */
        g_assert_cmpint(SEND(doc->editor->sci, SCI_GETCHARAT, 24, 0), ==, '\n');
        paste(block);
    }
    else if (phase == 2)
    {
        text_is("void f() {\n    target();\n    first();\n        second();\n}\n");
        reset("  last();", 9);paste("    next();\n");
    }
    else if (phase == 3)
    {
        text_is("  last();\n  next();\n");undo();text_is("  last();");
        reset("\ttarget();\r\n", 4);
        SEND(doc->editor->sci, SCI_SETUSETABS, TRUE, 0);
        SEND(doc->editor->sci, SCI_SETEOLMODE, SC_EOL_CRLF, 0);
        paste(block);
    }
    else if (phase == 4)
    {
        text_is("\tfirst();\r\n\t\tsecond();\r\n\ttarget();\r\n");
        reset("    target();\n", 6);paste("X");
    }
    else if (phase == 5)
    {
        text_is("    taXrget();\n");
        reset("    target();\n", 4);
        SEND(doc->editor->sci, SCI_SETSEL, 4, 10);paste("x\n");
    }
    else if (phase == 6)
    {
        text_is("    x\n();\n");
        reset("    target();\n    other();\n", 0);
        SEND(doc->editor->sci, SCI_SETSEL, 0, 14);paste(block);
    }
    else if (phase == 7)
    {
        text_is("    first();\n        second();\n    other();\n");undo();text_is("    target();\n    other();\n");
        reset(base, 18);paste(block);
        SEND(doc->editor->sci, SCI_REPLACESEL, 0, (sptr_t)"!");
    }
    else if (phase == 8)
    {
        text_is("void f() {\n    tar!get();\n}\n");
        reset(base, 18);SEND(doc->editor->sci, SCI_SETREADONLY, TRUE, 0);paste(block);
    }
    else if (phase == 9)
    {
        text_is(base);SEND(doc->editor->sci, SCI_SETREADONLY, FALSE, 0);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option), FALSE);
        reset("target();\n", 3);paste("    x();\n");
    }
    else if (phase == 10)
    {
        text_is("tar    x();\nget();\n");
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option), TRUE);
        doc = document_open_file(denied, FALSE, NULL, NULL);g_assert_nonnull(doc);
        reset("target();\n", 3);paste("    x();\n");
    }
    else if (phase == 11)
    {
        text_is("tar    x();\nget();\n");
        reset("top\n    x();\n", 8);
        keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_MOVELINEUP);
        text_is("    x();\ntop\n");
        SEND(doc->editor->sci, SCI_SETSAVEPOINT, 0, 0);
        doc = document_open_file(source, FALSE, NULL, NULL);
        reset(block, 0);SEND(doc->editor->sci, SCI_SETSEL, 0, strlen(block));
        GtkWidget *query = named(window, "insight-clips-query");g_assert_nonnull(query);
        gtk_entry_set_text(GTK_ENTRY(query), "Paste fixture");
        GtkWidget *copy = named(window, "insight-clip-copy");g_assert_nonnull(copy);
        gtk_button_clicked(GTK_BUTTON(copy));
        gtk_entry_set_text(GTK_ENTRY(query), "");
        GtkWidget *view = named(window, "insight-clips");g_assert_nonnull(view);
        GtkTreePath *path = gtk_tree_path_new_from_indices(0, -1);
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), path);
        gtk_tree_path_free(path);
        reset(base, 18);
        GtkWidget *clip_paste = named(window, "insight-clip-paste");g_assert_nonnull(clip_paste);
        gtk_button_clicked(GTK_BUTTON(clip_paste));
    }
    else if (phase == 12)
    {
        text_is("void f() {\n    first();\n        second();\n    target();\n}\n");undo();text_is(base);
        gchar *disk = NULL;g_assert_true(g_file_get_contents(source, &disk, NULL, NULL));
        g_assert_cmpstr(disk, ==, base);g_free(disk);
        g_print("Native intelligent paste: above/below, final line, CRLF/tabs, Undo/Redo, partial/native fallback, selection, stale/read-only, file-type permission, Clips and restart passed\n");
        check_line_moves();
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option), FALSE);
        reset("    target();\n", 8);
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "X", -1);
        line_action("paste-line");
    }
    else if (phase == 13)
    {
        text_is("X    target();\n");undo();text_is("    target();\n");
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option), TRUE);
        reset("    target();\n", 8);
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), block, -1);
        line_action("paste-line");
    }
    else if (phase == 14)
    {
        text_is("    first();\n        second();\n    target();\n");undo();text_is("    target();\n");
        g_print("Native line operations: Paste Line, menu/keyboard moves, five-line moves, selection direction, CRLF/Unicode, final line, Undo/Redo and guards passed\n");
        check_line_clipboard();
        reset("left RIGHT\n", 0);SEND(doc->editor->sci, SCI_SETSEL, 5, 10);
        line_action("cut-selection-or-paste");clipboard_is("RIGHT");text_is("left \n");
        SEND(doc->editor->sci, SCI_SETSEL, 0, 0);line_action("cut-selection-or-paste");
    }
    else if (phase == 15)
    {
        text_is("RIGHTleft \n");undo();text_is("left \n");undo();text_is("left RIGHT\n");
        keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_REDO);
        keybindings_send_command(GEANY_KEY_GROUP_EDITOR, GEANY_KEYS_EDITOR_REDO);text_is("RIGHTleft \n");
        reset("  x();\n    target();\n", 0);
        SEND(doc->editor->sci, SCI_SETSEL, 0, strlen("  x();\n"));
        line_action("cut-selection-or-paste");clipboard_is("  x();\n");text_is("    target();\n");
        SEND(doc->editor->sci, SCI_SETSEL, 7, 7);line_action("cut-selection-or-paste");
    }
    else if (phase == 16)
    {
        text_is("    x();\n    target();\n");undo();text_is("    target();\n");
        undo();text_is("  x();\n    target();\n");
        g_print("Native line clipboard: repeated Copy Line, keyboard/menu, CRLF/Unicode, left/right cuts, readonly/copy, empty/size/binary guards and Cut Selection or Paste with Undo/Redo passed\n");
        finish();return G_SOURCE_REMOVE;
    }
    phase++;return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data)
{
    (void)data;g_error("Intelligent paste timeout phase %u", phase);return G_SOURCE_REMOVE;
}
int main(int argc, char **argv)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
    restarted = argc == 3 && !strcmp(argv[1], "--restart");
    directory = restarted ? g_strdup(argv[2]) : g_dir_make_tmp("insight-paste-XXXXXX", NULL);
    profile = g_build_filename(directory, "profile", NULL);
    source = g_build_filename(directory, "paste.cpp", NULL);
    denied = g_build_filename(directory, "denied.cpp", NULL);
    if (!restarted)
    {
        gchar *folder = g_build_filename(profile, "languages", "paste-denied", NULL);
        g_assert_cmpint(g_mkdir_with_parents(folder, 0700), ==, 0);
        gchar *path = g_build_filename(folder, "language.conf", NULL);
        gchar *config = g_strdup_printf("[language]\nname=Paste Denied\nparser=C++\nfiles=%s;\n[editing]\nversion=1\nenabled=true\nallow-intelligent-paste=false\n", denied);
        g_assert_true(g_file_set_contents(path, config, -1, NULL));g_free(config);g_free(path);g_free(folder);
        path = g_build_filename(profile, "geany.conf", NULL);
        g_assert_true(g_file_set_contents(path, "[geany]\nconfirm_exit=false\n", -1, NULL));g_free(path);
        g_assert_true(g_file_set_contents(source, base, -1, NULL));
        g_assert_true(g_file_set_contents(denied, base, -1, NULL));
    }
    gchar *args[] = {"paste-native", "--new-instance", "--no-session", "--config", profile, source, NULL};
    g_timeout_add(250, step, NULL);g_timeout_add_seconds(30, timeout, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    gint result = main_lib(6, args);
    if (!restarted)
    {
        gchar *executable = g_canonicalize_filename(argv[0], NULL), *child[] = {executable, "--restart", directory, NULL};
        GError *error = NULL;gint status;
        g_assert_true(g_spawn_sync(NULL, child, NULL, 0, NULL, NULL, NULL, NULL, &status, &error));
        g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status, &error));g_assert_no_error(error);g_free(executable);
    }
    g_free(source);g_free(denied);g_free(profile);g_free(directory);return result;
}
