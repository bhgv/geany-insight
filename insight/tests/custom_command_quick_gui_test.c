/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify saved menu/accelerator/repeat launches, stale and dirty guards, shortcut conflicts and live command references.
 * RU: Проверяет запуск меню/клавишами/повтором, защиту от устаревания/черновиков, конфликты и справочники команд. */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window, *tool, *stale;
static GeanyDocument *doc;
static gchar *directory, *settings, *calls;
static guint phase, ticks, key;
static GdkModifierType mods;
static const gchar *alpha_key = "<Control><Alt><Shift>F9";
static const gchar *beta_key = "<Control><Alt><Shift>F10";

/* EN: Find borrowed name in root, including menus; NULL for absent controls.
 * RU: Находит заимствованный name в root, включая меню; NULL при отсутствии. */
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

/* EN: Borrow a named top-level window, or NULL before it exists.
 * RU: Возвращает заимствованное окно name либо NULL до его создания. */
static GtkWidget *top(const gchar *name)
{
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *found = NULL;
    for (GList *p = windows; p; p = p->next)
        if (!g_strcmp0(gtk_widget_get_name(p->data), name))
            found = p->data;
    g_list_free(windows);
    return found;
}

/* EN: Resolve required named widget in root or abort with a useful missing-control check.
 * RU: Находит обязательный name в root либо прерывает проверку отсутствующего элемента. */
static GtkWidget *required(GtkWidget *root, const gchar *name)
{
    GtkWidget *widget = find(root, name);
    g_assert_nonnull(widget);
    return widget;
}

/* EN: Activate a named main-window menu action and reacquire the live command window.
 * RU: Активирует пункт name главного окна и получает действующее окно команды. */
static void activate(const gchar *name)
{
    gtk_menu_item_activate(GTK_MENU_ITEM(required(window, name)));
    tool = top("insight-custom-commands");
}

/* EN: Click a real tool button name.
 * RU: Нажимает настоящую кнопку name инструмента. */
static void click(const gchar *name)
{
    gtk_button_clicked(GTK_BUTTON(required(tool, name)));
}

/* EN: Set tool entry name to text using GTK signals.
 * RU: Устанавливает text в поле name через сигналы GTK. */
static void set(const gchar *name, const gchar *text)
{
    gtk_entry_set_text(GTK_ENTRY(required(tool, name)), text);
}

/* EN: Compare actual child launch log with expected, and verify source buffer/Undo are unchanged.
 * RU: Сравнивает фактический журнал запусков с expected и проверяет неизменность исходника/Undo. */
static void expect(const gchar *expected)
{
    gchar *text = NULL;
    g_assert_true(g_file_get_contents(calls, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, expected);
    g_free(text);
    text = sci_get_contents(doc->editor->sci, -1);
    g_assert_cmpstr(text, ==, "source\n");
    g_free(text);
    g_assert_false(doc->changed);
    g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
}

/* EN: Dispatch the configured Alpha accelerator through GTK's production accelerator groups.
 * RU: Выполняет клавишу Alpha через настоящие группы ускорителей GTK. */
static void accelerated(void)
{
    g_assert_true(gtk_accel_groups_activate(G_OBJECT(window), key, mods));
    tool = top("insight-custom-commands");
}

/* EN: Provide an inert competing accelerator for group/object/keyval/modifier; data is unused, return FALSE.
 * RU: Имитирует конкурирующий ускоритель group/object/keyval/modifier; data не используется, возвращает FALSE. */
static gboolean competing(GtkAccelGroup *group, GObject *object, guint keyval, GdkModifierType modifier, gpointer data)
{
    (void)group;
    (void)object;
    (void)keyval;
    (void)modifier;
    (void)data;
    return FALSE;
}

/* EN: Inspect the real Command Browser for Alpha and its displayed shortcut, then close without running it.
 * data is unused; FALSE/REMOVE after the expected row is found.
 * RU: Проверяет Alpha и показанное сочетание в настоящем Command Browser, закрывает без запуска.
 * data не используется; REMOVE после нахождения ожидаемой строки. */
static gboolean browser(gpointer data)
{
    (void)data;
    GtkWidget *dialog = top("insight-command-browser");
    if (!dialog)
        return G_SOURCE_CONTINUE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(required(dialog, "insight-command-view")));
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter), found = FALSE;
    gchar *shortcut = gtk_accelerator_get_label(key, mods);
    while (valid)
    {
        gchar *path = NULL, *shown = NULL;
        gtk_tree_model_get(model, &iter, 0, &path, 1, &shown, -1);
        if (strstr(path, "→ Fixture Alpha Ω"))
        {
            g_assert_cmpstr(shown, ==, shortcut);
            found = TRUE;
        }
        g_free(path);
        g_free(shown);
        valid = gtk_tree_model_iter_next(model, &iter);
    }
    g_assert_true(found);
    g_free(shortcut);
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    return G_SOURCE_REMOVE;
}

/* EN: Require failed shortcut edits to preserve exact saved configuration bytes.
 * RU: Требует сохранения точных байтов настроек после ошибочного сочетания shortcut. */
static void invalid_shortcut(const gchar *shortcut)
{
    gchar *before = NULL, *after = NULL;
    g_assert_true(g_file_get_contents(settings, &before, NULL, NULL));
    set("insight-custom-shortcut", shortcut);
    click("insight-custom-check-shortcut");
    click("insight-custom-command-edit-1");
    g_assert_true(g_file_get_contents(settings, &after, NULL, NULL));
    g_assert_cmpstr(before, ==, after);
    g_free(before);
    g_free(after);
}

/* EN: Advance real menu/key launches and safety scenarios; data is unused, ticks bound hangs.
 * RU: Продвигает настоящие запуски меню/клавишами и защитные сценарии; data не используется, ticks ограничивает ожидание. */
static gboolean step(gpointer data)
{
    (void)data;
    if (++ticks > 300)
        g_error("Quick command GUI timeout phase %u", phase);
    if (phase != 8 && tool && g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"))
        return G_SOURCE_CONTINUE;
    const gchar *four = "Alpha\nAlpha\nAlpha\nBeta\n";
    const gchar *five = "Alpha\nAlpha\nAlpha\nBeta\nAlpha\n";
    const gchar *seven = "Alpha\nAlpha\nAlpha\nBeta\nAlpha\nAlpha\nBeta\n";
    const gchar *eight = "Alpha\nAlpha\nAlpha\nBeta\nAlpha\nAlpha\nBeta\nBeta\n";
    if (phase == 0)
    {
        doc = document_get_current();
        if (!doc)
            return G_SOURCE_CONTINUE;
        window = gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
        g_assert_false(g_file_test(calls, G_FILE_TEST_EXISTS));
        GtkWidget *alpha = required(window, "insight-saved-command-0");
        g_assert_nonnull(g_object_get_data(G_OBJECT(alpha), "insight-command-shortcut"));
        activate("insight-saved-command-0");
    }
    else if (phase == 1)
    {
        expect("Alpha\n");
        accelerated();
    }
    else if (phase == 2)
    {
        expect("Alpha\nAlpha\n");
        activate("insight-repeat-saved-command");
    }
    else if (phase == 3)
    {
        expect("Alpha\nAlpha\nAlpha\n");
        set("insight-custom-command-name", "Unsaved draft");
        activate("insight-saved-command-1");
        g_assert_null(g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"));
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(required(tool, "insight-custom-command-name"))), ==, "Unsaved draft");
        click("insight-custom-discard-draft");
        activate("insight-saved-command-1");
    }
    else if (phase == 4)
    {
        expect(four);
        invalid_shortcut("<Control>s");
        invalid_shortcut(alpha_key);
        invalid_shortcut("x");
        set("insight-custom-shortcut", beta_key);
        click("insight-custom-command-edit-1");
        stale = g_object_ref(required(window, "insight-saved-command-0"));
        click("insight-custom-command-edit-3");
        gtk_menu_item_activate(GTK_MENU_ITEM(stale));
        expect(four);
        g_assert_null(g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"));
    }
    else if (phase == 5)
    {
        expect(four);
        g_assert_false(gtk_widget_get_sensitive(required(window, "insight-repeat-saved-command")));
        gtk_menu_item_activate(GTK_MENU_ITEM(stale));
        g_object_unref(stale);
        stale = NULL;
        accelerated();
    }
    else if (phase == 6)
    {
        expect(five);
        g_timeout_add(50, browser, NULL);
        activate("insight-command-browser-command");
        activate("insight-create-key-list");
        GeanyDocument *list = document_get_current();
        gchar *text = sci_get_contents(list->editor->sci, -1);
        g_assert_nonnull(strstr(text, "Fixture Alpha Ω"));
        g_assert_nonnull(strstr(text, "Fixture Beta"));
        gchar *shortcut = gtk_accelerator_get_label(key, mods);
        g_assert_nonnull(strstr(text, shortcut));
        g_free(shortcut);
        g_free(text);
        document_set_text_changed(list, FALSE);
        g_assert_true(document_close(list));
        GtkAccelGroup *reserved = gtk_accel_group_new();
        gtk_accel_group_connect(reserved, key, mods, 0, g_cclosure_new(G_CALLBACK(competing), NULL, NULL));
        gtk_window_add_accel_group(GTK_WINDOW(window), reserved);
        g_assert_false(gtk_accel_groups_activate(G_OBJECT(window), key, mods));
        expect(five);
        gtk_window_remove_accel_group(GTK_WINDOW(window), reserved);
        g_object_unref(reserved);
        accelerated();
    }
    else if (phase == 7)
    {
        expect("Alpha\nAlpha\nAlpha\nBeta\nAlpha\nAlpha\n");
        set("insight-custom-command-argument-1", "hold");
        activate("insight-saved-command-0");
        accelerated();
    }
    else if (phase == 8)
    {
        gchar *log = NULL;
        if (!g_file_get_contents(calls, &log, NULL, NULL))
            return G_SOURCE_CONTINUE;
        gboolean ready = !strcmp(log, seven);
        g_free(log);
        if (!ready)
            return G_SOURCE_CONTINUE;
        accelerated();
        expect(seven);
        click("insight-custom-command-stop");
    }
    else if (phase == 9)
    {
        expect(seven);
        gtk_widget_destroy(tool);
        tool = NULL;
        activate("insight-repeat-saved-command");
    }
    else if (phase == 10)
    {
        expect(eight);
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(required(tool, "insight-custom-shortcut"))), ==, beta_key);
        stale = g_object_ref(required(window, "insight-saved-command-0"));
        click("insight-custom-command-edit-2");
        gtk_menu_item_activate(GTK_MENU_ITEM(stale));
        expect(eight);
    }
    else if (phase == 11)
    {
        g_assert_false(gtk_widget_get_sensitive(required(window, "insight-repeat-saved-command")));
        gtk_menu_item_activate(GTK_MENU_ITEM(stale));
        g_object_unref(stale);
        stale = NULL;
        accelerated();
    }
    else if (phase == 12)
    {
        expect("Alpha\nAlpha\nAlpha\nBeta\nAlpha\nAlpha\nBeta\nBeta\nAlpha\n");
        g_assert_true(g_file_set_contents(settings, "broken settings", -1, NULL));
        activate("insight-saved-command-0");
        g_assert_null(g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"));
    }
    else if (phase == 13)
    {
        g_assert_false(gtk_accel_groups_activate(G_OBJECT(window), key, mods));
        expect("Alpha\nAlpha\nAlpha\nBeta\nAlpha\nAlpha\nBeta\nBeta\nAlpha\n");
        g_print("Quick commands GUI: startup no-run, menu/accelerator/repeat, dirty discard, conflict rollback, reorder/delete stale guards, browser/keylist, dynamic GTK conflict, busy refusal, reopen and malformed reload passed\n");
        gtk_widget_destroy(tool);
        tool = NULL;
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
        return G_SOURCE_REMOVE;
    }
    phase++;
    return G_SOURCE_CONTINUE;
}

/* EN: Seed two saved tools before startup and record actual child calls in a private fixture directory.
 * RU: Создаёт два профиля до старта и записывает фактические вызовы в частном каталоге проверки. */
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY"))
        return 77;
    directory = g_dir_make_tmp("quick-commands-XXXXXX", NULL);
    gchar *profile = g_build_filename(directory, "profile", NULL);
    gchar *folder = g_build_filename(profile, "insight", NULL);
    g_assert_cmpint(g_mkdir_with_parents(folder, 0700), ==, 0);
    gchar *config = g_build_filename(profile, "geany.conf", NULL);
    g_assert_true(g_file_set_contents(config, "[geany]\nconfirm_exit=false\n", -1, NULL));
    gchar *source = g_build_filename(directory, "source.v", NULL);
    g_assert_true(g_file_set_contents(source, "source\n", -1, NULL));
    calls = g_build_filename(directory, "calls", NULL);
    settings = g_build_filename(folder, "custom-commands.conf", NULL);
    GKeyFile *saved = g_key_file_new();
    g_key_file_set_integer(saved, "commands", "count", 2);
    for (guint i = 0; i < 2; i++)
    {
        gchar *group = g_strdup_printf("command-%u", i);
        gchar *script = g_strdup_printf("import pathlib,sys,time; p=pathlib.Path('calls'); p.write_text((p.read_text() if p.exists() else '')+'%s\\n'); time.sleep(30 if sys.argv[1]=='hold' else 0)", i ? "Beta" : "Alpha");
        gchar *quoted = g_shell_quote(script);
        gchar *command = g_strdup_printf("python3 -c %s %%1", quoted);
        g_key_file_set_string(saved, group, "name", i ? "Fixture Beta" : "Fixture Alpha Ω");
        g_key_file_set_string(saved, group, "command", command);
        g_key_file_set_string(saved, group, "directory", directory);
        g_key_file_set_string(saved, group, "shortcut", i ? "" : alpha_key);
        g_free(command);
        g_free(quoted);
        g_free(script);
        g_free(group);
    }
    gchar *text = g_key_file_to_data(saved, NULL, NULL);
    g_assert_true(g_file_set_contents(settings, text, -1, NULL));
    gtk_accelerator_parse(alpha_key, &key, &mods);
    gchar *argv[] = {"quick-command-native", "--new-instance", "--no-session", "--config", profile, source, NULL};
    g_timeout_add(300, step, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    return main_lib(6, argv);
}
