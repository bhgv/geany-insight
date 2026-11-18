/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise production Lua/Python frame selection, Stop, Restart and terminal cleanup in GTK.
 * RU: Проверяет выбор кадров, Stop, Restart и очистку завершённых Lua/Python-сеансов в настоящем GTK. */
#include <gtk/gtk.h>
#include "geany.h"
#include "main.h"
#include "document.h"
#include "editor.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>

static gboolean python, failing, console_mode, passed;
static guint phase;
static GtkWidget *editor_window;

/* EN: Find widget by key name, button/menu caption, or recursively in children and submenus; return NULL if absent.
 * RU: Ищет widget по имени key, надписи кнопки/меню, детям и подменю; возвращает NULL при отсутствии. */
static GtkWidget *find(GtkWidget *widget, const gchar *key)
{
    if (!g_strcmp0(gtk_widget_get_name(widget), key))
        return widget;
    if (GTK_IS_BUTTON(widget) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), key))
        return widget;
    if (GTK_IS_MENU_ITEM(widget))
    {
        if (!g_strcmp0(gtk_menu_item_get_label(GTK_MENU_ITEM(widget)), key))
            return widget;
        GtkWidget *menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget));
        GtkWidget *found = menu ? find(menu, key) : NULL;
        if (found)
            return found;
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

/* EN: Return the visible source debugger window for the chosen language, or NULL while startup is pending.
 * RU: Возвращает видимое окно отладчика выбранного языка либо NULL до завершения запуска. */
static GtkWidget *debug_window(void)
{
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *found = NULL;
    for (GList *it = windows; it && !found; it = it->next)
        if (gtk_widget_get_visible(it->data) && !g_strcmp0(gtk_window_get_title(GTK_WINDOW(it->data)),
            python ? "Python Source Debugger" : "Lua Source Debugger"))
            found = it->data;
    g_list_free(windows);
    return found;
}

/* EN: Return owned plain text from view's GTK buffer; the caller releases it.
 * RU: Возвращает собственный текст буфера GTK view; вызывающий освобождает его. */
static gchar *view_text(GtkWidget *view)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    return gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
}

/* EN: Click window's enabled button key; return FALSE while unavailable and fail if the control is absent.
 * RU: Нажимает доступную кнопку key окна window; возвращает FALSE при недоступности, отсутствие — ошибка. */
static gboolean click(GtkWidget *window, const gchar *key)
{
    GtkWidget *button = find(window, key);
    g_assert_nonnull(button);
    if (!gtk_widget_is_sensitive(button))
        return FALSE;
    gtk_button_clicked(GTK_BUTTON(button));
    return TRUE;
}

/* EN: Assert window has no stale selectable frame rows or execution/source actions after Stop or completion.
 * RU: Проверяет отсутствие старых кадров и команд исполнения/перехода в window после Stop либо завершения. */
static void assert_cleared(GtkWidget *window)
{
    GtkWidget *frames = find(window, "insight-script-frames");
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(frames));
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 0);
    g_assert_null(gtk_combo_box_get_active_id(GTK_COMBO_BOX(frames)));
    g_assert_false(gtk_widget_is_sensitive(frames));
    const gchar *inactive[] = {"Step Into", "Continue", "Stop", "Go to Source"};
    for (guint i = 0; i < G_N_ELEMENTS(inactive); i++)
        g_assert_false(gtk_widget_is_sensitive(find(window, inactive[i])));
}

/* EN: Save window to the path in environment variable after raising and painting; errors abort the fixture.
 * RU: Сохраняет window в путь переменной окружения variable после поднятия и отрисовки; ошибка завершает фикстуру. */
static void capture(GtkWidget *window, const gchar *variable)
{
    const gchar *path = g_getenv(variable);
    if (!path || !python || failing)
        return;
    GdkWindow *surface = gtk_widget_get_window(window);
    GdkPixbuf *pixels = gdk_pixbuf_get_from_window(surface, 0, 0,
        gdk_window_get_width(surface), gdk_window_get_height(surface));
    g_assert_nonnull(pixels);
    GError *error = NULL;
    g_assert_true(gdk_pixbuf_save(pixels, path, "jpeg", &error, "quality", "88", NULL));
    g_assert_no_error(error);
    g_object_unref(pixels);
}

/* EN: Drive window through Python input, pending Pause, EOF, Stop and Restart; late old replies must not replace Stop.
 * Return REMOVE after successful shutdown, CONTINUE while asynchronous requests drain; assertions fail the fixture.
 * RU: Проверяет window с вводом Python, ожидающей Pause, EOF, Stop и Restart; поздние ответы не должны заменять Stop.
 * Возвращает REMOVE после успешного выхода, CONTINUE при ожидании; неудачная проверка завершает фикстуру. */
static gboolean drive_console(GtkWidget *window)
{
    static guint settle_ticks;
    GtkWidget *input = find(window, "insight-python-input");
    GtkWidget *resume = find(window, "Continue");
    const gchar *status = gtk_label_get_text(GTK_LABEL(find(window, "insight-script-status")));
    gboolean waiting = gtk_widget_is_sensitive(input);
    if (phase == 1 && gtk_widget_is_sensitive(resume))
    {
        g_assert_true(click(window, "Continue"));
        phase = 2;
    }
    else if (phase == 2 && waiting)
    {
        gtk_entry_set_text(GTK_ENTRY(input), "old draft");
        g_assert_true(click(window, "Stop"));
        settle_ticks = 0;
        phase = 3;
    }
    else if (phase == 3 || phase == 14)
    {
        assert_cleared(window);
        g_assert_false(waiting);
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(input)), ==, "");
        g_assert_cmpstr(status, ==, "Debugging stopped");
        if (++settle_ticks < 5)
            return G_SOURCE_CONTINUE;
        if (phase == 3)
        {
            g_assert_true(click(window, "Restart"));
            phase = 4;
        }
        else
        {
            g_print("Python GTK: input/EOF, deferred source Pause, Stop while waiting, Restart while waiting and ignored late replies passed.\n");
            passed = TRUE;
            gtk_widget_destroy(window);
            gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(editor_window, "menu_quit1")));
            return G_SOURCE_REMOVE;
        }
    }
    else if ((phase == 4 || phase == 10 || phase == 12) && gtk_widget_is_sensitive(resume))
    {
        g_assert_false(waiting);
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(input)), ==, "");
        GtkWidget *frames = find(window, "insight-script-frames");
        g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(frames)), ==, "0");
        gchar *output = view_text(find(window, "insight-python-console"));
        g_assert_cmpstr(output, ==, "");
        g_free(output);
        g_assert_true(click(window, "Continue"));
        phase++;
    }
    else if (phase == 5 && waiting)
    {
        g_assert_true(click(window, "Pause"));
        status = gtk_label_get_text(GTK_LABEL(find(window, "insight-script-status")));
        g_assert_nonnull(strstr(status, "provide input or EOF"));
        g_assert_false(gtk_widget_is_sensitive(find(window, "Pause")));
        gtk_window_present(GTK_WINDOW(window));
        gdk_window_raise(gtk_widget_get_window(window));
        gtk_widget_queue_draw(window);
        phase = 6;
    }
    else if (phase == 6)
    {
        capture(window, "INSIGHT_SCRIPT_INPUT_CAPTURE");
        gtk_entry_set_text(GTK_ENTRY(input), "alpha");
        g_signal_emit_by_name(input, "activate");
        phase = 7;
    }
    else if (phase == 7 && gtk_widget_is_sensitive(resume))
    {
        g_assert_false(waiting);
        g_assert_nonnull(strstr(status, "Paused"));
        g_assert_true(click(window, "Continue"));
        phase = 8;
    }
    else if (phase == 8 && waiting)
    {
        g_assert_nonnull(strstr(status, "Python is waiting for input"));
        g_assert_true(click(window, "Send EOF"));
        phase = 9;
    }
    else if (phase == 9 && strstr(status, "script finished"))
    {
        assert_cleared(window);
        g_assert_false(waiting);
        gchar *output = view_text(find(window, "insight-python-console"));
        g_assert_nonnull(strstr(output, "first=alpha"));
        g_assert_nonnull(strstr(output, "eof"));
        g_assert_nonnull(strstr(output, "done"));
        g_free(output);
        g_assert_true(click(window, "Restart"));
        phase = 10;
    }
    else if (phase == 11 && waiting)
    {
        gtk_entry_set_text(GTK_ENTRY(input), "draft to discard");
        g_assert_true(click(window, "Restart"));
        phase = 12;
    }
    else if (phase == 13 && waiting)
    {
        g_assert_true(click(window, "Stop"));
        settle_ticks = 0;
        phase = 14;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Drive a small nested-call fixture from data's unused timer; return REMOVE after all lifecycle assertions pass.
 * RU: Управляет небольшой фикстурой вложенных вызовов из таймера с неиспользуемым data; возвращает REMOVE после всех проверок. */
static gboolean drive(gpointer data)
{
    (void)data;
    if (!editor_window)
    {
        GeanyDocument *document = document_get_current();
        if (!DOC_VALID(document))
            return G_SOURCE_CONTINUE;
        editor_window = gtk_widget_get_toplevel(GTK_WIDGET(document->editor->sci));
    }
    if (!phase)
    {
        GtkWidget *menu = find(editor_window,
            python ? "Debug Current Python Buffer…" : "Debug Current Lua Buffer…");
        if (!menu)
            return G_SOURCE_CONTINUE;
        phase = 1;
        gtk_menu_item_activate(GTK_MENU_ITEM(menu));
        return G_SOURCE_CONTINUE;
    }
    GtkWidget *window = debug_window();
    if (!window)
        return G_SOURCE_CONTINUE;
    if (console_mode)
        return drive_console(window);
    GtkWidget *frames = find(window, "insight-script-frames");
    const gchar *status = gtk_label_get_text(GTK_LABEL(find(window, "insight-script-status")));
    GtkWidget *resume = find(window, "Continue");
    if (phase == 1 && gtk_widget_is_sensitive(resume))
    {
        gchar *stack = view_text(find(window, "insight-script-stack"));
        gint count = gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(frames)), NULL);
        gboolean nested = count >= 2 && strstr(stack, "value") && strstr(stack, "41");
        g_free(stack);
        if (nested)
        {
            phase = 2;
            g_assert_true(gtk_combo_box_set_active_id(GTK_COMBO_BOX(frames), "1"));
        }
        else
            g_assert_true(click(window, "Step Into"));
    }
    else if (phase == 2 && gtk_widget_is_sensitive(resume) && strstr(status, "inspecting frame #1"))
    {
        g_assert_true(click(window, "Stop"));
        phase = 3;
    }
    else if (phase == 3)
    {
        assert_cleared(window);
        gchar *stack = view_text(find(window, "insight-script-stack"));
        g_assert_cmpstr(stack, ==, "");
        g_free(stack);
        g_assert_nonnull(strstr(status, "Debugging stopped"));
        gtk_window_present(GTK_WINDOW(window));
        gdk_window_raise(gtk_widget_get_window(window));
        gtk_widget_queue_draw(window);
        phase = 4;
    }
    else if (phase == 4)
    {
        capture(window, "INSIGHT_SCRIPT_STOP_CAPTURE");
        g_assert_true(click(window, "Restart"));
        phase = 5;
    }
    else if (phase == 5 && gtk_widget_is_sensitive(resume))
    {
        g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(frames)), ==, "0");
        g_assert_null(strstr(status, "inspecting frame #1"));
        g_assert_true(click(window, "Continue"));
        phase = 6;
    }
    else if (phase == 6 && (strstr(status, "script finished") || strstr(status, "fixture failure")))
    {
        assert_cleared(window);
        g_assert_nonnull(strstr(status, failing ? "fixture failure" : "script finished"));
        g_assert_true(click(window, "Restart"));
        phase = 7;
    }
    else if (phase == 7 && gtk_widget_is_sensitive(resume))
    {
        g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(frames)), ==, "0");
        g_assert_true(click(window, "Stop"));
        phase = 8;
    }
    else if (phase == 8)
    {
        assert_cleared(window);
        g_print("%s GTK: caller-frame inspection, Stop cleanup, Restart, %s and fresh frame context passed.\n",
            python ? "Python" : "Lua", failing ? "runtime error" : "normal completion");
        passed = TRUE;
        gtk_widget_destroy(window);
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(editor_window, "menu_quit1")));
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Fail a stalled scenario with its last phase and status; data is unused.
 * RU: Завершает зависший сценарий с номером этапа и статусом; data не используется. */
static gboolean timeout(gpointer data)
{
    (void)data;
    GtkWidget *window = debug_window();
    GtkWidget *label = window ? find(window, "insight-script-status") : NULL;
    g_error("%s lifecycle timeout at phase %u: %s", python ? "Python" : "Lua", phase,
        label ? gtk_label_get_text(GTK_LABEL(label)) : "no debugger window");
    return G_SOURCE_REMOVE;
}

/* EN: Run language argv[1] and normal/error/console argv[2] in a temporary editor profile; return nonzero on failure.
 * RU: Проверяет язык argv[1] и normal/error/console argv[2] во временном профиле редактора; возвращает ненулевой код при ошибке. */
int main(int argc, char **argv)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY"))
        return 77;
    g_assert_cmpint(argc, ==, 3);
    python = !strcmp(argv[1], "python");
    failing = !strcmp(argv[2], "error");
    console_mode = python && !strcmp(argv[2], "console");
    gchar *directory = g_dir_make_tmp("script-lifecycle-XXXXXX", NULL);
    g_assert_nonnull(directory);
    gchar *profile = g_build_filename(directory, "profile", NULL);
    gchar *settings = g_build_filename(profile, "geany.conf", NULL);
    gchar *source = g_build_filename(directory, python ? "fixture.py" : "fixture.lua", NULL);
    g_assert_cmpint(g_mkdir_with_parents(profile, 0700), ==, 0);
    g_assert_true(g_file_set_contents(settings, "[geany]\nconfirm_exit=false\n", -1, NULL));
    const gchar *body = python ?
        "def inner():\n    value = 41\n    value += 1\n    return value\nanswer = inner()\n" :
        "local function inner()\n    local value = 41\n    value = value + 1\n    return value\nend\nlocal answer = inner()\n";
    if (console_mode)
        body = "first = input('first: ')\nprint('first=' + first)\ntry:\n    second = input('second: ')\nexcept EOFError:\n    print('eof')\nprint('done')\n";
    gchar *code = g_strconcat(body, failing ? (python ? "raise RuntimeError('fixture failure')\n" :
        "error('fixture failure')\n") : "", NULL);
    g_assert_true(g_file_set_contents(source, code, -1, NULL));
    gchar *args[] = {argv[0], "--new-instance", "--no-session", "--no-plugins", "--config", profile, source, NULL};
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_timeout_add(100, drive, NULL);
    g_timeout_add_seconds(45, timeout, NULL);
    gint result = main_lib(G_N_ELEMENTS(args) - 1, args);
    g_free(code);
    g_free(source);
    g_free(settings);
    g_free(profile);
    g_free(directory);
    return passed ? result : 1;
}
