/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify %s through real editor previews/processes, including Stop and closed-window cleanup.
 * RU: Проверяет %s настоящим редактором/процессами, включая Stop и очистку после закрытия окна. */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window, *tool;
static GeanyDocument *doc, *untitled;
static gchar *directory, *source, *record, *prefix, *planned, *active;
static guint phase, ticks, baseline;
static const gchar *content = "Aя\r\nline\tZ";

/* EN: Find borrowed widget name under root, including menus; return NULL when absent.
 * RU: Находит заимствованный виджет name в root, включая меню; NULL при отсутствии. */
static GtkWidget *find(GtkWidget *root, const gchar *name)
{
    if (!g_strcmp0(gtk_widget_get_name(root), name))
        return root;
    if (GTK_IS_MENU_ITEM(root))
    {
        GtkWidget *menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));
        GtkWidget *found = menu ? find(menu, name) : NULL;
        if (found)
            return found;
    }
    if (!GTK_IS_CONTAINER(root))
        return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *found = NULL;
    for (GList *p = children; p && !found; p = p->next)
        found = find(p->data, name);
    g_list_free(children);
    return found;
}

/* EN: Resolve a required production control name or abort.
 * RU: Находит обязательный настоящий элемент name либо прерывает тест. */
static GtkWidget *named(const gchar *name)
{
    GtkWidget *widget = find(tool, name);
    g_assert_nonnull(widget);
    return widget;
}

/* EN: Click the named production button.
 * RU: Нажимает настоящую кнопку name. */
static void click(const gchar *name)
{
    gtk_button_clicked(GTK_BUTTON(named(name)));
}

/* EN: Copy the entire text buffer of named tool widget into owned memory.
 * RU: Копирует весь текст виджета name в собственную память. */
static gchar *text(const gchar *name)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(name)));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

/* EN: Count selection-snapshot directories in the shared temp root for no-file checks.
 * RU: Считает каталоги снимков в общем временном корне для проверки отсутствия созданных файлов. */
static guint snapshots(void)
{
    GDir *dir = g_dir_open(g_get_tmp_dir(), 0, NULL);
    g_assert_nonnull(dir);
    const gchar *name;
    guint count = 0;
    while ((name = g_dir_read_name(dir)))
        if (g_str_has_prefix(name, "geany-insight-selection-"))
            count++;
    g_dir_close(dir);
    return count;
}

/* EN: Configure the child mode and dispatch Preview/Run; the two %s tokens must share one file.
 * RU: Задаёт режим mode дочернего процесса и вызывает предпросмотр/запуск; два %s используют один файл. */
static void command(const gchar *mode, gboolean run)
{
    gchar *template = g_strconcat(prefix, mode, NULL);
    gtk_entry_set_text(GTK_ENTRY(named("insight-custom-command-template")), template);
    g_free(template);
    click(run ? "insight-custom-command-run" : "insight-custom-command-preview");
}

/* EN: Assert source disk bytes, current named-buffer bytes/Undo and clipboard remain as expected.
 * RU: Проверяет исходник на диске, ожидаемые байты buffer/Undo именованного буфера и буфер обмена. */
static void unchanged(const gchar *buffer)
{
    gchar *disk = NULL;
    g_assert_true(g_file_get_contents(source, &disk, NULL, NULL));
    g_assert_cmpstr(disk, ==, "disk\n");
    g_free(disk);
    gchar *actual = sci_get_contents(doc->editor->sci, -1);
    g_assert_cmpstr(actual, ==, buffer);
    g_free(actual);
    g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
    gchar *clipboard = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    g_assert_cmpstr(clipboard, ==, "unchanged clipboard");
    g_free(clipboard);
}

/* EN: Require the last subprocess's exact payload hex and shared path; verify cleanup after completion.
 * RU: Проверяет точный hex payload и общий путь последнего процесса; после завершения проверяет очистку. */
static void completed(const gchar *hex)
{
    gchar *output = text("insight-custom-command-output");
    gchar *expected = g_strconcat("\nDATA:", hex, "\nSAME:True\n", NULL);
    g_assert_nonnull(strstr(output, expected));
    g_free(expected);
    g_free(output);
    gchar *path = NULL;
    g_assert_true(g_file_get_contents(record, &path, NULL, NULL));
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    gchar *parent = g_path_get_dirname(path);
    g_assert_false(g_file_test(parent, G_FILE_TEST_EXISTS));
    g_free(parent);
    g_free(path);
    g_assert_cmpuint(snapshots(), ==, baseline);
}

/* EN: Assert an immediate rejected launch and no materialized selection directory.
 * RU: Проверяет немедленный отказ запуска и отсутствие созданного каталога снимка. */
static void rejected(void)
{
    g_assert_null(g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"));
    g_assert_cmpuint(snapshots(), ==, baseline);
}

/* EN: Select doc's page without changing source text.
 * RU: Выбирает вкладку doc без изменения исходника. */
static void reveal(GeanyDocument *target)
{
    GtkWidget *child = GTK_WIDGET(target->editor->sci);
    for (GtkWidget *parent = gtk_widget_get_parent(child); parent;
         child = parent, parent = gtk_widget_get_parent(parent))
        if (GTK_IS_NOTEBOOK(parent))
            gtk_notebook_set_current_page(GTK_NOTEBOOK(parent), gtk_notebook_page_num(GTK_NOTEBOOK(parent), child));
}

/* EN: Wait for the held child to record its live snapshot; return FALSE while pending, retain active path on success.
 * RU: Ждёт записи живого пути удерживаемым процессом; FALSE при ожидании, при успехе сохраняет active. */
static gboolean live_snapshot(void)
{
    gchar *path = NULL;
    if (!g_file_get_contents(record, &path, NULL, NULL))
        return FALSE;
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
    {
        g_free(path);
        return FALSE;
    }
    g_free(active);
    active = path;
    return TRUE;
}

/* EN: Advance real asynchronous scenarios; data is unused, stalled processes abort the test.
 * RU: Продвигает настоящие отложенные сценарии; data не используется, зависание прерывает тест. */
static gboolean step(gpointer data)
{
    (void)data;
    if (++ticks > 300)
        g_error("Selection custom command GUI timeout phase %u", phase);
    gboolean busy = tool && g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy");
    if (busy && phase != 4 && phase != 6)
        return G_SOURCE_CONTINUE;
    if (phase == 0)
    {
        doc = document_get_current();
        if (!doc)
            return G_SOURCE_CONTINUE;
        window = gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
        sci_set_text(doc->editor->sci, content);
        SEND(doc->editor->sci, SCI_SETSEL, 10, 1);
        SEND(doc->editor->sci, SCI_EMPTYUNDOBUFFER, 0, 0);
        document_set_text_changed(doc, TRUE);
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "unchanged clipboard", -1);
        baseline = snapshots();
        GtkWidget *menu = find(window, "insight-custom-commands-menu");
        g_assert_nonnull(menu);
        gtk_menu_item_activate(GTK_MENU_ITEM(menu));
        GList *tops = gtk_window_list_toplevels();
        for (GList *p = tops; p; p = p->next)
            if (!g_strcmp0(gtk_widget_get_name(p->data), "insight-custom-commands"))
                tool = p->data;
        g_list_free(tops);
        g_assert_nonnull(tool);
        gtk_entry_set_text(GTK_ENTRY(named("insight-custom-command-directory")), directory);
        command("ok", FALSE);
        gchar *preview = text("insight-custom-command-context");
        gchar **lines = g_strsplit(preview, "\n", -1);
        for (guint i = 0; lines[i]; i++)
            if (g_str_has_prefix(lines[i], "%s = "))
                planned = g_strdup(lines[i] + 5);
        g_assert_nonnull(planned);
        g_assert_false(g_file_test(planned, G_FILE_TEST_EXISTS));
        g_assert_cmpuint(snapshots(), ==, baseline);
        g_strfreev(lines);
        g_free(preview);
        SEND(doc->editor->sci, SCI_SETSEL, 5, 9);
        click("insight-custom-command-run");
    }
    else if (phase == 1)
    {
        completed("6c696e65");
        unchanged(content);
        g_assert_true(doc->changed);
        gchar *path = NULL;
        g_assert_true(g_file_get_contents(record, &path, NULL, NULL));
        g_assert_cmpstr(path, !=, planned);
        g_free(path);
        SEND(doc->editor->sci, SCI_SETSEL, 10, 1);
        SEND(doc->editor->sci, SCI_SETREADONLY, TRUE, 0);
        command("exit7", TRUE);
    }
    else if (phase == 2)
    {
        completed("d18f0d0a6c696e6509");
        unchanged(content);
        const gchar *status = gtk_label_get_text(GTK_LABEL(named("insight-custom-command-status")));
        g_assert_nonnull(strstr(status, "7"));
        gtk_entry_set_text(GTK_ENTRY(named("insight-custom-command-template")), "/no-such-insight-executable %s");
        click("insight-custom-command-run");
        rejected();
        SEND(doc->editor->sci, SCI_SETSEL, 0, 0);
        command("ok", TRUE);
        rejected();
        SEND(doc->editor->sci, SCI_SETSEL, 0, 1);
        SEND(doc->editor->sci, SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE, 0);
        command("ok", TRUE);
        rejected();
        SEND(doc->editor->sci, SCI_SETSELECTIONMODE, SC_SEL_STREAM, 0);
        SEND(doc->editor->sci, SCI_CLEARSELECTIONS, 0, 0);
        SEND(doc->editor->sci, SCI_SETSEL, 0, 1);
        SEND(doc->editor->sci, SCI_ADDSELECTION, 3, 4);
        command("ok", TRUE);
        rejected();
        SEND(doc->editor->sci, SCI_CLEARSELECTIONS, 0, 0);
        SEND(doc->editor->sci, SCI_SETSEL, 0, 1);
        SEND(doc->editor->sci, SCI_SETSELECTIONNCARETVIRTUALSPACE, 0, 1);
        command("ok", TRUE);
        rejected();
        SEND(doc->editor->sci, SCI_SETSELECTIONNCARETVIRTUALSPACE, 0, 0);
        untitled = document_new_file(NULL, NULL, "Привет");
        SEND(untitled->editor->sci, SCI_SETSEL, 0, strlen("Привет"));
        SEND(untitled->editor->sci, SCI_SETREADONLY, TRUE, 0);
        command("ok", TRUE);
    }
    else if (phase == 3)
    {
        completed("d09fd180d0b8d0b2d0b5d182");
        document_set_text_changed(untitled, FALSE);
        g_assert_true(document_close(untitled));
        reveal(doc);
        SEND(doc->editor->sci, SCI_SETSEL, 10, 1);
        command("hold", TRUE);
        SEND(doc->editor->sci, SCI_SETREADONLY, FALSE, 0);
        sci_set_text(doc->editor->sci, "changed later");
        SEND(doc->editor->sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    }
    else if (phase == 4)
    {
        if (!live_snapshot())
            return G_SOURCE_CONTINUE;
        gchar *payload = NULL;
        g_assert_true(g_file_get_contents(active, &payload, NULL, NULL));
        g_assert_cmpstr(payload, ==, "я\r\nline\t");
        g_free(payload);
        click("insight-custom-command-stop");
    }
    else if (phase == 5)
    {
        g_assert_false(g_file_test(active, G_FILE_TEST_EXISTS));
        g_assert_cmpuint(snapshots(), ==, baseline);
        unchanged("changed later");
        SEND(doc->editor->sci, SCI_SETSEL, 0, strlen("changed later"));
        command("hold", TRUE);
    }
    else if (phase == 6)
    {
        if (!live_snapshot())
            return G_SOURCE_CONTINUE;
        gtk_widget_destroy(tool);
        tool = NULL;
    }
    else if (phase == 7)
    {
        if (g_file_test(active, G_FILE_TEST_EXISTS))
            return G_SOURCE_CONTINUE;
        gchar *parent = g_path_get_dirname(active);
        if (g_file_test(parent, G_FILE_TEST_EXISTS))
        {
            g_free(parent);
            return G_SOURCE_CONTINUE;
        }
        g_free(parent);
        g_assert_cmpuint(snapshots(), ==, baseline);
        unchanged("changed later");
        document_set_text_changed(doc, FALSE);
        g_print("Selection command GUI: no-file preview, fresh Run, exact reverse UTF-8/CRLF, repeated %%s, read-only/untitled, no source/clipboard/Undo edits, empty/rectangle/multiple/virtual refusal, spawn failure and exit/Stop/close cleanup passed\n");
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
        return G_SOURCE_REMOVE;
    }
    phase++;
    return G_SOURCE_CONTINUE;
}

/* EN: Start an isolated editor and a Python child reporting snapshot bytes and path; source disk bytes remain unchanged.
 * RU: Запускает изолированный редактор и Python-процесс, сообщающий байты/путь снимка; исходник на диске не меняется. */
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY"))
        return 77;
    directory = g_dir_make_tmp("selection-command-XXXXXX", NULL);
    gchar *profile = g_build_filename(directory, "profile", NULL);
    g_assert_cmpint(g_mkdir(profile, 0700), ==, 0);
    gchar *config = g_build_filename(profile, "geany.conf", NULL);
    g_assert_true(g_file_set_contents(config, "[geany]\nconfirm_exit=false\n", -1, NULL));
    source = g_build_filename(directory, "source.inc", NULL);
    record = g_build_filename(directory, "snapshot-path", NULL);
    g_assert_true(g_file_set_contents(source, "disk\n", -1, NULL));
    const gchar *script = "import pathlib,sys,time; p=pathlib.Path(sys.argv[1]); pathlib.Path(sys.argv[3]).write_text(str(p)); print('DATA:'+p.read_bytes().hex(),flush=True); print('SAME:'+str(sys.argv[1]==sys.argv[2]),flush=True); time.sleep(30 if sys.argv[4]=='hold' else 0); sys.exit(7 if sys.argv[4]=='exit7' else 0)";
    gchar *quoted_script = g_shell_quote(script);
    gchar *quoted_record = g_shell_quote(record);
    prefix = g_strdup_printf("python3 -c %s %%s %%s %s ", quoted_script, quoted_record);
    gchar *argv[] = {"selection-command-native", "--new-instance", "--no-session", "--config", profile, source, NULL};
    g_timeout_add(200, step, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    return main_lib(6, argv);
}
