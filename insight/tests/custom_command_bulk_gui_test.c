/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise real custom-command previews and subprocess argv across changing open/modified editor buffers.
 * RU: Проверяет настоящий предпросмотр и argv дочернего процесса при смене открытых/изменённых буферов. */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window, *tool;
static GeanyDocument *first, *second, *third, *untitled;
static gchar *directory, *paths[3], *counter, *prefix;
static guint phase, ticks;

/* EN: Find borrowed widget name beneath root, including detached menu subtrees; return NULL when absent.
 * RU: Находит заимствованный виджет name в root, включая подменю; возвращает NULL при отсутствии. */
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

/* EN: Resolve required widget name in the tool, aborting on missing production controls.
 * RU: Находит обязательный name в окне инструмента; отсутствие настоящего элемента прерывает тест. */
static GtkWidget *named(const gchar *name)
{
    GtkWidget *widget = find(tool, name);
    g_assert_nonnull(widget);
    return widget;
}

/* EN: Click a real named button without synthesizing private implementation calls.
 * RU: Нажимает настоящую кнопку name без вызова закрытых функций реализации. */
static void click(const gchar *name)
{
    gtk_button_clicked(GTK_BUTTON(named(name)));
}

/* EN: Borrow text from name's GTK text buffer as an owned copy.
 * RU: Возвращает собственную копию текста GTK-буфера виджета name. */
static gchar *text(const gchar *name)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(name)));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

/* EN: Switch to doc's native notebook page, leaving its text and selection intact.
 * RU: Переключает настоящую вкладку doc без изменения текста и выделения. */
static void reveal(GeanyDocument *doc)
{
    GtkWidget *child = GTK_WIDGET(doc->editor->sci);
    for (GtkWidget *parent = gtk_widget_get_parent(child); parent;
         child = parent, parent = gtk_widget_get_parent(parent))
        if (GTK_IS_NOTEBOOK(parent))
            gtk_notebook_set_current_page(GTK_NOTEBOOK(parent), gtk_notebook_page_num(GTK_NOTEBOOK(parent), child));
}

/* EN: Set the argv template suffix and invoke Preview or Run using production controls.
 * RU: Устанавливает суффикс шаблона suffix и вызывает настоящий предпросмотр либо запуск по run. */
static void command(const gchar *suffix, gboolean run)
{
    gchar *template = g_strconcat(prefix, suffix, NULL);
    gtk_entry_set_text(GTK_ENTRY(named("insight-custom-command-template")), template);
    g_free(template);
    click(run ? "insight-custom-command-run" : "insight-custom-command-preview");
}

/* EN: Assert subprocess output has exactly the expected ARG lines and count accepted launches.
 * RU: Проверяет точные строки ARG процесса по expected и число принятых запусков runs. */
static void arguments(const gchar **expected, guint runs)
{
    gchar *output = text("insight-custom-command-output");
    gchar **lines = g_strsplit(output, "\n", -1);
    guint count = 0;
    for (guint i = 0; lines[i]; i++)
    {
        if (!g_str_has_prefix(lines[i], "ARG:"))
            continue;
        g_assert_nonnull(expected[count]);
        g_assert_cmpstr(lines[i] + 4, ==, expected[count]);
        count++;
    }
    g_assert_null(expected[count]);
    g_strfreev(lines);
    g_free(output);
    gchar *number = NULL;
    g_assert_true(g_file_get_contents(counter, &number, NULL, NULL));
    g_assert_cmpuint(g_ascii_strtoull(number, NULL, 10), ==, runs);
    g_free(number);
    for (guint i = 0; i < 3; i++)
    {
        gchar *disk = NULL;
        g_assert_true(g_file_get_contents(paths[i], &disk, NULL, NULL));
        g_assert_cmpstr(disk, ==, "disk\n");
        g_free(disk);
    }
}

/* EN: Require a rejected run to display message and leave the subprocess launch count at four.
 * RU: Требует сообщение message после отказа запуска и неизменный счётчик четырёх дочерних процессов. */
static void rejected(const gchar *message)
{
    g_assert_null(g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"));
    const gchar *status = gtk_label_get_text(GTK_LABEL(named("insight-custom-command-status")));
    g_assert_nonnull(strstr(status, message));
    gchar *number = NULL;
    g_assert_true(g_file_get_contents(counter, &number, NULL, NULL));
    g_assert_cmpstr(number, ==, "4");
    g_free(number);
}

/* EN: Advance asynchronous scenarios after initialization/process completion; data is unused, timeout aborts.
 * RU: Продвигает отложенные сценарии после инициализации/процесса; data не используется, таймаут прерывает тест. */
static gboolean step(gpointer data)
{
    (void)data;
    if (++ticks > 250)
        g_error("Bulk custom command GUI timeout, phase %u", phase);
    if (tool && g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"))
        return G_SOURCE_CONTINUE;
    if (phase == 0)
    {
        first = document_get_current();
        if (!first)
            return G_SOURCE_CONTINUE;
        window = gtk_widget_get_toplevel(GTK_WIDGET(first->editor->sci));
        second = document_open_file(paths[1], FALSE, NULL, NULL);
        third = document_open_file(paths[2], FALSE, NULL, NULL);
        g_assert_nonnull(second);
        g_assert_nonnull(third);
        sci_set_text(second->editor->sci, "unsaved\n");
        SEND(second->editor->sci, SCI_EMPTYUNDOBUFFER, 0, 0);
        document_set_text_changed(second, TRUE);
        SEND(third->editor->sci, SCI_SETREADONLY, TRUE, 0);
        untitled = document_new_file(NULL, NULL, "draft");
        document_set_text_changed(untitled, TRUE);
        reveal(first);
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
        gtk_entry_set_text(GTK_ENTRY(named("insight-custom-command-name")), "Bulk compiler fixture");
        command("%f%o", FALSE);
        gchar *preview = text("insight-custom-command-context");
        for (guint i = 0; i < 3; i++)
            g_assert_nonnull(strstr(preview, paths[i]));
        g_free(preview);
        g_assert_false(g_file_test(counter, G_FILE_TEST_EXISTS));
        click("insight-custom-command-edit-1");
        command("%f%o", TRUE);
    }
    else if (phase == 1)
    {
        const gchar *expected[] = {paths[0], paths[1], paths[2], NULL};
        arguments(expected, 1);
        g_assert_true(second->changed);
        g_assert_false(SEND(second->editor->sci, SCI_CANUNDO, 0, 0));
        gchar *buffer = sci_get_contents(second->editor->sci, -1);
        g_assert_cmpstr(buffer, ==, "unsaved\n");
        g_free(buffer);
        command("%f%m", TRUE);
    }
    else if (phase == 2)
    {
        const gchar *expected[] = {paths[1], NULL};
        arguments(expected, 2);
        command("%f%m", FALSE);
        document_set_text_changed(second, FALSE);
        sci_set_text(first->editor->sci, "new unsaved source\n");
        document_set_text_changed(first, TRUE);
        click("insight-custom-command-run");
    }
    else if (phase == 3)
    {
        const gchar *expected[] = {paths[0], NULL};
        arguments(expected, 3);
        g_assert_true(document_close(second));
        g_assert_true(document_close(third));
        document_set_text_changed(untitled, FALSE);
        g_assert_true(document_close(untitled));
        reveal(first);
        command("%f%o", TRUE);
    }
    else if (phase == 4)
    {
        const gchar *expected[] = {paths[0], NULL};
        arguments(expected, 4);
        document_set_text_changed(first, FALSE);
        command("%f%m", TRUE);
        rejected("No named modified files are open");
        command("%f%o:%n%m", TRUE);
        rejected("Use one bulk file modifier per argument");
        g_assert_true(document_close(first));
        command("%f%o", TRUE);
        rejected("No named files are open");
        gchar *settings = g_build_filename(directory, "profile", "insight", "custom-commands.conf", NULL);
        gchar *saved = NULL;
        g_assert_true(g_file_get_contents(settings, &saved, NULL, NULL));
        g_assert_nonnull(strstr(saved, "%f%o"));
        g_free(saved);
        g_free(settings);
        g_print("Bulk commands GUI: exact subprocess argv, named/read-only/modified/untitled buffers, fresh Run after Preview, closed files, no auto-save/Undo changes, saved profile and refused empty/ambiguous sets passed\n");
        gtk_widget_destroy(tool);
        tool = NULL;
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
        return G_SOURCE_REMOVE;
    }
    phase++;
    return G_SOURCE_CONTINUE;
}

/* EN: Start an isolated native editor and a Python fixture that records actual subprocess arguments; no user settings are used.
 * RU: Запускает изолированный редактор и Python-сценарий записи argv процесса; настройки пользователя не используются. */
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY"))
        return 77;
    directory = g_dir_make_tmp("bulk-commands-XXXXXX", NULL);
    gchar *profile = g_build_filename(directory, "profile", NULL);
    g_assert_cmpint(g_mkdir(profile, 0700), ==, 0);
    gchar *config = g_build_filename(profile, "geany.conf", NULL);
    g_assert_true(g_file_set_contents(config, "[geany]\nconfirm_exit=false\n", -1, NULL));
    paths[0] = g_build_filename(directory, "first %o;$(literal).v", NULL);
    paths[1] = g_build_filename(directory, "Пример name.inc", NULL);
    paths[2] = g_build_filename(directory, "read-only.cpp", NULL);
    for (guint i = 0; i < 3; i++)
        g_assert_true(g_file_set_contents(paths[i], "disk\n", -1, NULL));
    counter = g_build_filename(directory, "runs", NULL);
    const gchar *script = "import pathlib,sys; p=pathlib.Path(sys.argv[1]); p.write_text(str(int(p.read_text())+1) if p.exists() else '1'); [print('ARG:'+x) for x in sys.argv[2:]]";
    gchar *quoted_script = g_shell_quote(script);
    gchar *quoted_counter = g_shell_quote(counter);
    prefix = g_strdup_printf("python3 -c %s %s ", quoted_script, quoted_counter);
    gchar *argv[] = {"bulk-command-native", "--new-instance", "--no-session", "--config", profile, paths[0], NULL};
    g_timeout_add(200, step, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    return main_lib(6, argv);
}
