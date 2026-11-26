/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise saved diagnostic patterns, automatic nonzero/stderr parsing and manual reparse/navigation in the real editor.
 * RU: Проверяет сохранённые шаблоны, авторазбор ненулевого/stderr вывода и ручной повторный разбор/переходы редактора. */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window, *tool;
static GeanyDocument *doc, *other;
static gchar *directory, *profile, *source, *other_path, *settings, *prefix;
static guint phase, ticks;
static const gchar *normal = "^S\\|(?<file>[^|]+)\\|(?<line>[0-9]+)\\|(?<column>[0-9]+)\\|(?<message>.*)$";
static const gchar *reverse = "^L(?<line>[0-9]+)@(?<file>[^|]+)\\|(?<message>.*)$";

/* EN: Find a borrowed named child in root, including menus; NULL if absent.
 * RU: Находит заимствованный элемент name в root с учётом меню; NULL при отсутствии. */
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

/* EN: Resolve required named production control or abort.
 * RU: Находит обязательный настоящий элемент name либо прерывает проверку. */
static GtkWidget *named(const gchar *name)
{
    GtkWidget *widget = find(tool, name);
    g_assert_nonnull(widget);
    return widget;
}

/* EN: Click named production button.
 * RU: Нажимает настоящую кнопку name. */
static void click(const gchar *name)
{
    gtk_button_clicked(GTK_BUTTON(named(name)));
}

/* EN: Set named entry to value without bypassing GTK notifications.
 * RU: Устанавливает value в поле name, не обходя уведомления GTK. */
static void set(const gchar *name, const gchar *value)
{
    gtk_entry_set_text(GTK_ENTRY(named(name)), value);
}

/* EN: Open the real custom-command window and return it through global tool.
 * RU: Открывает настоящее окно пользовательских команд и сохраняет его в tool. */
static void open_tool(void)
{
    GtkWidget *menu = find(window, "insight-custom-commands-menu");
    g_assert_nonnull(menu);
    gtk_menu_item_activate(GTK_MENU_ITEM(menu));
    GList *windows = gtk_window_list_toplevels();
    for (GList *p = windows; p; p = p->next)
        if (!g_strcmp0(gtk_widget_get_name(p->data), "insight-custom-commands"))
            tool = p->data;
    g_list_free(windows);
    g_assert_nonnull(tool);
}

/* EN: Count parsed production rows without changing their sort or source.
 * RU: Считает настоящие строки разбора без изменения сортировки или исходника. */
static guint count(void)
{
    return gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(named("insight-custom-command-links"))), NULL);
}

/* EN: Verify every row resolves to the original launch source and expected metadata; return path for line.
 * RU: Проверяет исходный путь запуска и метаданные каждой строки; возвращает путь строки line. */
static GtkTreePath *row(guint line, guint column, const gchar *message)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(named("insight-custom-command-links")));
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    GtkTreePath *result = NULL;
    while (valid)
    {
        gchar *path = NULL, *text = NULL;
        guint number, col;
        gtk_tree_model_get(model, &iter, 0, &path, 1, &number, 2, &col, 3, &text, -1);
        g_assert_cmpstr(path, ==, source);
        if (number == line)
        {
            g_assert_cmpuint(col, ==, column);
            g_assert_cmpstr(text, ==, message);
            result = gtk_tree_model_get_path(model, &iter);
        }
        g_free(path);
        g_free(text);
        valid = gtk_tree_model_iter_next(model, &iter);
    }
    g_assert_nonnull(result);
    return result;
}

/* EN: Verify source disk/buffer and Undo are untouched by parsing; assert actual child launch total runs.
 * RU: Проверяет неизменность исходника/Undo после разбора и фактическое число процессов runs. */
static void unchanged(guint runs)
{
    gchar *text = sci_get_contents(doc->editor->sci, -1);
    g_assert_cmpstr(text, ==, "one\ntwo\nthree\n");
    g_free(text);
    g_assert_false(doc->changed);
    g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
    g_assert_true(g_file_get_contents(source, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, "one\ntwo\nthree\n");
    g_free(text);
    gchar *counter = g_build_filename(directory, "runs", NULL);
    g_assert_true(g_file_get_contents(counter, &text, NULL, NULL));
    g_assert_cmpuint(g_ascii_strtoull(text, NULL, 10), ==, runs);
    g_free(text);
    g_free(counter);
}

/* EN: Drive real profiles, producers, automatic and explicit parsing; data unused and ticks bound hangs.
 * RU: Проверяет реальные профили/процессы, автоматический и ручной разбор; data не используется, ticks ограничивает ожидание. */
static gboolean step(gpointer data)
{
    (void)data;
    if (++ticks > 250)
        g_error("Diagnostic GUI timeout phase %u", phase);
    if (phase != 4 && tool && g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"))
        return G_SOURCE_CONTINUE;
    if (phase == 0)
    {
        doc = document_get_current();
        if (!doc)
            return G_SOURCE_CONTINUE;
        window = gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
        open_tool();
        set("insight-custom-command-name", "Auto diagnostics");
        set("insight-custom-command-template", prefix);
        set("insight-custom-command-directory", directory);
        set("insight-custom-diagnostic-pattern", normal);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named("insight-custom-auto-parse")), TRUE);
        click("insight-custom-check-pattern");
        click("insight-custom-command-edit-1");
        set("insight-custom-command-argument-1", "S");
        click("insight-custom-command-run");
    }
    else if (phase == 1)
    {
        g_assert_cmpuint(count(), ==, 2);
        GtkTreePath *path = row(2, 3, "diag");
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(named("insight-custom-command-links")), path, NULL, FALSE);
        gtk_tree_path_free(path);
        path = row(3, 1, "stderr diag");
        gtk_tree_path_free(path);
        g_assert_true(document_get_current() == doc);
        unchanged(1);
        click("insight-custom-command-open-link");
        g_assert_cmpint(sci_get_line_from_position(doc->editor->sci, sci_get_current_position(doc->editor->sci)), ==, 1);
        click("insight-custom-command-edit-0");
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named("insight-custom-diagnostic-pattern"))), ==, "");
        g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named("insight-custom-auto-parse"))));
        set("insight-custom-command-name", "Manual reverse");
        set("insight-custom-command-template", prefix);
        set("insight-custom-command-directory", directory);
        set("insight-custom-diagnostic-pattern", reverse);
        click("insight-custom-command-edit-1");
        click("insight-custom-command-edit-3");
        gtk_combo_box_set_active(GTK_COMBO_BOX(named("insight-custom-command-profiles")), 1);
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named("insight-custom-diagnostic-pattern"))), ==, normal);
        g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named("insight-custom-auto-parse"))));
        gtk_combo_box_set_active(GTK_COMBO_BOX(named("insight-custom-command-profiles")), 0);
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named("insight-custom-diagnostic-pattern"))), ==, reverse);
        set("insight-custom-command-argument-1", "R");
        click("insight-custom-command-run");
    }
    else if (phase == 2)
    {
        g_assert_cmpuint(count(), ==, 0);
        click("insight-custom-command-parse-links");
        g_assert_cmpuint(count(), ==, 1);
        GtkTreePath *path = row(2, 0, "rev");
        gtk_tree_path_free(path);
        unchanged(2);
        gchar *before = NULL, *after = NULL;
        g_assert_true(g_file_get_contents(settings, &before, NULL, NULL));
        set("insight-custom-diagnostic-pattern", "(");
        click("insight-custom-check-pattern");
        click("insight-custom-command-edit-1");
        g_assert_true(g_file_get_contents(settings, &after, NULL, NULL));
        g_assert_cmpstr(before, ==, after);
        g_free(before);
        g_free(after);
        click("insight-custom-command-run");
        g_assert_null(g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"));
        unchanged(2);
        click("insight-custom-command-parse-links");
        g_assert_cmpuint(count(), ==, 1);
        gtk_widget_destroy(tool);
        tool = NULL;
        open_tool();
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named("insight-custom-diagnostic-pattern"))), ==, reverse);
        g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named("insight-custom-auto-parse"))));
        gtk_combo_box_set_active(GTK_COMBO_BOX(named("insight-custom-command-profiles")), 1);
        set("insight-custom-command-argument-1", "S");
        click("insight-custom-command-run");
        other = document_open_file(other_path, FALSE, NULL, NULL);
        g_assert_nonnull(other);
    }
    else if (phase == 3)
    {
        g_assert_true(document_get_current() == other);
        g_assert_cmpuint(count(), ==, 2);
        GtkTreePath *path = row(3, 1, "stderr diag");
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(named("insight-custom-command-links")), path, NULL, FALSE);
        gtk_tree_path_free(path);
        unchanged(3);
        click("insight-custom-command-open-link");
        g_assert_true(document_get_current() == doc);
        g_assert_cmpint(sci_get_line_from_position(doc->editor->sci, sci_get_current_position(doc->editor->sci)), ==, 2);
        set("insight-custom-command-argument-1", "hold");
        click("insight-custom-command-run");
    }
    else if (phase == 4)
    {
        gchar *counter = g_build_filename(directory, "runs", NULL);
        gchar *number = NULL;
        gboolean ready = g_file_get_contents(counter, &number, NULL, NULL) && g_ascii_strtoull(number, NULL, 10) == 4;
        g_free(counter);
        g_free(number);
        if (!ready)
            return G_SOURCE_CONTINUE;
        g_assert_nonnull(g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"));
        click("insight-custom-command-stop");
    }
    else if (phase == 5)
    {
        g_assert_cmpuint(count(), ==, 0);
        unchanged(4);
        document_set_text_changed(other, FALSE);
        g_print("Diagnostic GUI: saved patterns/flags, reorder/reopen, automatic stderr/nonzero links, manual custom override, malformed-save/Run refusal, captured launch paths, explicit source navigation and no auto-parse on Stop passed\n");
        gtk_widget_destroy(tool);
        tool = NULL;
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
        return G_SOURCE_REMOVE;
    }
    phase++;
    return G_SOURCE_CONTINUE;
}

/* EN: Start an isolated editor and a Python diagnostic producer using disk fixtures only.
 * RU: Запускает изолированный редактор и Python-генератор диагностики на временных дисковых файлах. */
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY"))
        return 77;
    directory = g_dir_make_tmp("diagnostic-gui-XXXXXX", NULL);
    profile = g_build_filename(directory, "profile", NULL);
    g_assert_cmpint(g_mkdir(profile, 0700), ==, 0);
    gchar *config = g_build_filename(profile, "geany.conf", NULL);
    g_assert_true(g_file_set_contents(config, "[geany]\nconfirm_exit=false\n", -1, NULL));
    source = g_build_filename(directory, "unit name.v", NULL);
    g_assert_true(g_file_set_contents(source, "one\ntwo\nthree\n", -1, NULL));
    gchar *outside = g_build_filename(directory, "outside", NULL);
    g_assert_cmpint(g_mkdir(outside, 0700), ==, 0);
    other_path = g_build_filename(outside, "other.v", NULL);
    g_assert_true(g_file_set_contents(other_path, "other\n", -1, NULL));
    settings = g_build_filename(profile, "insight", "custom-commands.conf", NULL);
    const gchar *script = "import pathlib,sys,time; p=pathlib.Path('runs'); p.write_text(str(int(p.read_text())+1) if p.exists() else '1'); f=sys.argv[1]; m=sys.argv[2]; print(('L2@'+f+'|rev') if m=='R' else ('S|'+f+'|2|3|diag'),flush=True); print((f+':1: error: builtin') if m=='R' else ('S|'+f+'|3|1|stderr diag'),file=sys.stderr,flush=True); time.sleep(30 if m=='hold' else 0); sys.exit(7)";
    gchar *quoted = g_shell_quote(script);
    prefix = g_strdup_printf("python3 -c %s %%n %%1", quoted);
    gchar *argv[] = {"diagnostic-native", "--new-instance", "--no-session", "--config", profile, source, NULL};
    g_timeout_add(200, step, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    return main_lib(6, argv);
}
