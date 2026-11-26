/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise isolated stdout, exact source replacement reviews and stale/binary/failure guards in the native editor.
 * RU: Проверяет отдельный stdout, точные замены с предпросмотром и отказы при устаревании/двоичных данных/ошибках. */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window, *tool;
static GeanyDocument *doc;
static gchar *directory, *source, *prefix;
static guint phase, ticks, review_action, review_stage;
static const gchar *payload = "я\r\nnew();\n";

/* EN: Find borrowed named widget under root including menus; NULL when absent.
 * RU: Находит заимствованный name в root, включая меню; NULL при отсутствии. */
static GtkWidget *find(GtkWidget *root, const gchar *name)
{
    if (!g_strcmp0(gtk_widget_get_name(root), name))
        return root;
    if (GTK_IS_MENU_ITEM(root))
    {
        GtkWidget *menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));
        GtkWidget *result = menu ? find(menu, name) : NULL;
        if (result)
            return result;
    }
    if (!GTK_IS_CONTAINER(root))
        return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *result = NULL;
    for (GList *p = children; p && !result; p = p->next)
        result = find(p->data, name);
    g_list_free(children);
    return result;
}

/* EN: Find a live named top-level window, returning a borrowed widget or NULL.
 * RU: Находит действующее окно name, возвращая заимствованный виджет либо NULL. */
static GtkWidget *top(const gchar *name)
{
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *result = NULL;
    for (GList *p = windows; p; p = p->next)
        if (!g_strcmp0(gtk_widget_get_name(p->data), name))
            result = p->data;
    g_list_free(windows);
    return result;
}

/* EN: Return required tool widget name, aborting when production controls are missing.
 * RU: Возвращает обязательный элемент name, прерывая тест при отсутствии настоящих элементов. */
static GtkWidget *named(const gchar *name)
{
    GtkWidget *widget = find(tool, name);
    g_assert_nonnull(widget);
    return widget;
}

/* EN: Invoke a real named button.
 * RU: Нажимает настоящую кнопку name. */
static void click(const gchar *name)
{
    gtk_button_clicked(GTK_BUTTON(named(name)));
}

/* EN: Assert the source text and on-disk baseline; disk is never saved by output actions.
 * RU: Проверяет исходник text и исходные байты на диске; действия stdout никогда не сохраняют диск. */
static void expect(const gchar *text)
{
    gchar *actual = sci_get_contents(doc->editor->sci, -1);
    g_assert_cmpstr(actual, ==, text);
    g_free(actual);
    gchar *disk = NULL;
    g_assert_true(g_file_get_contents(source, &disk, NULL, NULL));
    g_assert_cmpstr(disk, ==, "ABCDE");
    g_free(disk);
}

/* EN: Reset source text, writable stream selection and empty Undo history.
 * RU: Сбрасывает исходник, доступное обычное выделение и пустой Undo. */
static void reset(void)
{
    SEND(doc->editor->sci, SCI_SETREADONLY, FALSE, 0);
    doc->readonly = FALSE;
    sci_set_text(doc->editor->sci, "ABCDE");
    SEND(doc->editor->sci, SCI_SETSEL, 4, 1);
    SEND(doc->editor->sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    document_set_text_changed(doc, TRUE);
}

/* EN: Restore doc's real notebook page without editing it.
 * RU: Восстанавливает настоящую вкладку doc без правок. */
static void reveal(void)
{
    GtkWidget *child = GTK_WIDGET(doc->editor->sci);
    for (GtkWidget *parent = gtk_widget_get_parent(child); parent;
         child = parent, parent = gtk_widget_get_parent(parent))
        if (GTK_IS_NOTEBOOK(parent))
            gtk_notebook_set_current_page(GTK_NOTEBOOK(parent), gtk_notebook_page_num(GTK_NOTEBOOK(parent), child));
}

/* EN: Launch the real Python fixture with mode; stdout and stderr are written independently.
 * RU: Запускает настоящий Python-сценарий с mode; stdout и stderr пишутся независимо. */
static void run(const gchar *mode)
{
    gchar *template = g_strconcat(prefix, mode, NULL);
    gtk_entry_set_text(GTK_ENTRY(named("insight-custom-command-template")), template);
    g_free(template);
    click("insight-custom-command-run");
}

/* EN: Check replacement previews, apply/cancel, or edit+Undo/move/read-only/close the destination while the modal runs.
 * data is unused; repeated callback verifies rejection after an injected change.
 * RU: Проверяет предпросмотр, применение/отмену либо правку+Undo/курсор/read-only/закрытие при открытом окне.
 * data не используется; повторный вызов проверяет отказ после изменения назначения. */
static gboolean review_step(gpointer data)
{
    (void)data;
    GtkWidget *dialog = top("insight-custom-output-review");
    if (!dialog)
        return G_SOURCE_CONTINUE;
    if (review_stage)
    {
        GtkWidget *apply = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);
        g_assert_false(gtk_widget_get_sensitive(apply));
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
        return G_SOURCE_REMOVE;
    }
    GtkWidget *before = find(dialog, "insight-custom-output-before");
    GtkWidget *after = find(dialog, "insight-custom-output-after");
    g_assert_nonnull(before);
    g_assert_nonnull(after);
    gchar *text = sci_get_contents(SCINTILLA(before), -1);
    g_assert_cmpstr(text, ==, "ABCDE");
    g_free(text);
    text = sci_get_contents(SCINTILLA(after), -1);
    g_assert_cmpstr(text, ==, "Aя\r\nnew();\nE");
    g_assert_null(strstr(text, "STDERR_ONLY"));
    g_free(text);
    g_assert_true(SEND(SCINTILLA(before), SCI_GETREADONLY, 0, 0));
    g_assert_true(SEND(SCINTILLA(after), SCI_GETREADONLY, 0, 0));
    if (review_action == 0 || review_action == 1)
    {
        gtk_dialog_response(GTK_DIALOG(dialog), review_action ? GTK_RESPONSE_APPLY : GTK_RESPONSE_CANCEL);
        return G_SOURCE_REMOVE;
    }
    if (review_action == 2)
    {
        SEND(doc->editor->sci, SCI_INSERTTEXT, 0, (sptr_t)" ");
        SEND(doc->editor->sci, SCI_UNDO, 0, 0);
        SEND(doc->editor->sci, SCI_SETSEL, 4, 1);
    }
    else if (review_action == 3)
        SEND(doc->editor->sci, SCI_SETSEL, 0, 0);
    else if (review_action == 4)
        SEND(doc->editor->sci, SCI_SETREADONLY, TRUE, 0);
    else if (review_action == 5)
    {
        document_set_text_changed(doc, FALSE);
        g_assert_true(document_close(doc));
        doc = NULL;
    }
    review_stage++;
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);
    return G_SOURCE_CONTINUE;
}

/* EN: Drive the real modal review with action, then wait synchronously for its scheduled GTK response.
 * RU: Открывает настоящий модальный просмотр с action и ждёт запланированный ответ GTK. */
static void review(guint action)
{
    review_action = action;
    review_stage = 0;
    g_timeout_add(50, review_step, NULL);
    click("insight-custom-stdout-review");
}

/* EN: Assert completed non-usable output disables both editor actions, preserving the source and Undo.
 * RU: Проверяет недоступность обоих действий при непригодном завершённом выводе, сохраняя исходник/Undo. */
static void refused(void)
{
    g_assert_false(gtk_widget_get_sensitive(named("insight-custom-stdout-open")));
    g_assert_false(gtk_widget_get_sensitive(named("insight-custom-stdout-review")));
    expect("ABCDE");
    g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
}

/* EN: Advance real command/review scenarios; data is unused, bounded ticks reject hangs.
 * RU: Продвигает настоящие команды/предпросмотры; data не используется, ограничение ticks выявляет зависание. */
static gboolean step(gpointer data)
{
    (void)data;
    if (++ticks > 250)
        g_error("Stdout review GUI timeout phase %u", phase);
    if (phase != 12 && phase != 14 && tool && g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"))
        return G_SOURCE_CONTINUE;
    if (phase == 0)
    {
        doc = document_get_current();
        if (!doc)
            return G_SOURCE_CONTINUE;
        window = gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
        reset();
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "stdout clipboard fixture", -1);
        GtkWidget *menu = find(window, "insight-custom-commands-menu");
        g_assert_nonnull(menu);
        gtk_menu_item_activate(GTK_MENU_ITEM(menu));
        tool = top("insight-custom-commands");
        g_assert_nonnull(tool);
        gtk_entry_set_text(GTK_ENTRY(named("insight-custom-command-directory")), directory);
        run("ok");
    }
    else if (phase == 1)
    {
        g_assert_true(gtk_widget_get_sensitive(named("insight-custom-stdout-open")));
        GtkTextBuffer *log = gtk_text_view_get_buffer(GTK_TEXT_VIEW(named("insight-custom-command-output")));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(log, &start, &end);
        gchar *logged = gtk_text_buffer_get_text(log, &start, &end, FALSE);
        g_assert_nonnull(strstr(logged, "\nSTDERR_ONLY\n"));
        g_free(logged);
        review(0);
        expect("ABCDE");
        g_assert_cmpint(SEND(doc->editor->sci, SCI_GETANCHOR, 0, 0), ==, 4);
        g_assert_cmpint(sci_get_current_position(doc->editor->sci), ==, 1);
        g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
        review(1);
        expect("Aя\r\nnew();\nE");
        g_assert_cmpint(SEND(doc->editor->sci, SCI_GETANCHOR, 0, 0), ==, 1);
        g_assert_cmpint(sci_get_current_position(doc->editor->sci), ==, 1 + strlen(payload));
        SEND(doc->editor->sci, SCI_UNDO, 0, 0);
        expect("ABCDE");
        g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
        SEND(doc->editor->sci, SCI_REDO, 0, 0);
        expect("Aя\r\nnew();\nE");
        click("insight-custom-stdout-open");
        GeanyDocument *output = document_get_current();
        g_assert_true(output != doc);
        g_assert_null(output->file_name);
        gchar *text = sci_get_contents(output->editor->sci, -1);
        g_assert_cmpstr(text, ==, payload);
        g_free(text);
        g_assert_true(output->changed);
        document_set_text_changed(output, FALSE);
        g_assert_true(document_close(output));
        reveal();
        reset();
        run("ok");
    }
    else if (phase >= 2 && phase <= 5)
    {
        review(phase);
        if (doc)
            expect("ABCDE");
        if (phase == 5)
        {
            doc = document_open_file(source, FALSE, NULL, NULL);
            g_assert_nonnull(doc);
        }
        reset();
        run(phase == 5 ? "exit7" : "ok");
    }
    else if (phase >= 6 && phase <= 10)
    {
        refused();
        const gchar *modes[] = {"nul", "bad", "empty", "large", "ok"};
        reset();
        run(modes[phase - 6]);
    }
    else if (phase == 11)
    {
        SEND(doc->editor->sci, SCI_INSERTTEXT, 0, (sptr_t)" ");
        SEND(doc->editor->sci, SCI_UNDO, 0, 0);
        SEND(doc->editor->sci, SCI_SETSEL, 4, 1);
        click("insight-custom-stdout-review");
        g_assert_null(top("insight-custom-output-review"));
        expect("ABCDE");
        click("insight-custom-command-output-0");
        refused();
        reset();
        run("hold");
    }
    else if (phase == 12)
    {
        g_assert_nonnull(g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"));
        click("insight-custom-command-stop");
    }
    else if (phase == 13)
    {
        refused();
        reset();
        run("wait");
    }
    else if (phase == 14)
    {
        g_assert_nonnull(g_object_get_data(G_OBJECT(tool), "insight-custom-command-busy"));
        click("insight-custom-command-output-0");
    }
    else if (phase == 15)
    {
        refused();
        gchar *clipboard = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
        g_assert_cmpstr(clipboard, ==, "stdout clipboard fixture");
        g_free(clipboard);
        document_set_text_changed(doc, FALSE);
        g_print("Stdout GUI: stderr isolation, exact UTF-8/CRLF, cancelled review, replacement selection and one Undo/Redo, new unnamed buffer, in-review edit+Undo/caret/readonly/close guards, stale Run target, failed/binary/empty/oversized/stopped output and Clear invalidation passed\n");
        gtk_widget_destroy(tool);
        tool = NULL;
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
        return G_SOURCE_REMOVE;
    }
    phase++;
    return G_SOURCE_CONTINUE;
}

/* EN: Start a native editor and a Python fixture producing independent stdout/stderr and error/binary/empty variants.
 * RU: Запускает редактор и Python-сценарий с отдельными stdout/stderr и ошибочным/двоичным/пустым выводом. */
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY"))
        return 77;
    directory = g_dir_make_tmp("stdout-review-XXXXXX", NULL);
    gchar *profile = g_build_filename(directory, "profile", NULL);
    g_assert_cmpint(g_mkdir(profile, 0700), ==, 0);
    gchar *config = g_build_filename(profile, "geany.conf", NULL);
    g_assert_true(g_file_set_contents(config, "[geany]\nconfirm_exit=false\n", -1, NULL));
    source = g_build_filename(directory, "source.cpp", NULL);
    g_assert_true(g_file_set_contents(source, "ABCDE", -1, NULL));
    const gchar *script = "import sys,time; m=sys.argv[1]; b=bytes.fromhex('d18f0d0a6e657728293b0a') if m in ('ok','exit7','hold','wait') else (b'a\\x00b' if m=='nul' else (b'\\xff' if m=='bad' else ((b'x'*4096+b'\\n')*257 if m=='large' else b''))); sys.stdout.buffer.write(b); sys.stdout.buffer.flush(); sys.stderr.write('STDERR_ONLY\\n'); sys.stderr.flush(); time.sleep(30 if m=='hold' else (1 if m=='wait' else 0)); sys.exit(7 if m=='exit7' else 0)";
    gchar *quoted = g_shell_quote(script);
    prefix = g_strdup_printf("python3 -c %s ", quoted);
    gchar *argv[] = {"stdout-review-native", "--new-instance", "--no-session", "--config", profile, source, NULL};
    g_timeout_add(200, step, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    return main_lib(6, argv);
}
