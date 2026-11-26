/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise unified plain/intelligent paste policies through real editor menus and clipboard requests.
 * RU: Проверяет единые правила обычной/умной вставки настоящим меню редактора и запросами буфера обмена. */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static gchar *directory, *denied;
static GtkWidget *window, *smart, *always;
static GeanyDocument *doc;

/* EN: Find a borrowed named child below root, including menu subtrees.
 * RU: Находит заимствованный виджет name в root, включая подменю. */
static GtkWidget *named(GtkWidget *root, const gchar *name)
{
    if (!g_strcmp0(gtk_widget_get_name(root), name)) return root;
    if (GTK_IS_MENU_ITEM(root))
    {
        GtkWidget *menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));
        GtkWidget *found = menu ? named(menu, name) : NULL;
        if (found) return found;
    }
    if (!GTK_IS_CONTAINER(root)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *found = NULL;
    for (GList *it = children; it && !found; it = it->next)
        found = named(it->data, name);
    g_list_free(children);
    return found;
}

/* EN: Drain local GTK clipboard callbacks and editor notifications without external clipboard owners.
 * RU: Завершает локальные запросы GTK и уведомления редактора без внешних владельцев буфера. */
static void settle(void)
{
    for (guint i = 0; i < 200; i++)
    {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(1000);
    }
}

/* EN: Assert current doc bytes and exact anchor/caret.
 * RU: Проверяет байты текущего doc и точные якорь/курсор. */
static void expect(const gchar *text, gint anchor, gint caret)
{
    ScintillaObject *sci = doc->editor->sci;
    gchar *actual = sci_get_contents(sci, -1);
    g_assert_cmpstr(actual, ==, text);
    g_free(actual);
    g_assert_cmpint(SEND(sci, SCI_GETANCHOR, 0, 0), ==, anchor);
    g_assert_cmpint(sci_get_current_position(sci), ==, caret);
}

/* EN: Reset doc to text and anchor/caret with normal writable UTF-8 selection and no Undo history.
 * RU: Сбрасывает doc к text и anchor/caret с обычным доступным выделением UTF-8 и пустым Undo. */
static void reset(const gchar *text, gint anchor, gint caret)
{
    ScintillaObject *sci = doc->editor->sci;
    doc->readonly = FALSE;
    SEND(sci, SCI_SETREADONLY, 0, 0);
    SEND(sci, SCI_CLEARSELECTIONS, 0, 0);
    SEND(sci, SCI_SETSELECTIONMODE, SC_SEL_STREAM, 0);
    SEND(sci, SCI_SETSELECTIONNCARETVIRTUALSPACE, 0, 0);
    SEND(sci, SCI_SETSELECTIONNANCHORVIRTUALSPACE, 0, 0);
    sci_set_text(sci, text);
    SEND(sci, SCI_SETSEL, anchor, caret);
    SEND(sci, SCI_SETEOLMODE, SC_EOL_LF, 0);
    SEND(sci, SCI_SETPASTECONVERTENDINGS, TRUE, 0);
    SEND(sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    gtk_widget_grab_focus(GTK_WIDGET(sci));
}

/* EN: Select persistent post-paste mode value using the actual menu.
 * RU: Выбирает постоянный режим value настоящим меню после вставки. */
static void position(gint value)
{
    gchar *name = g_strdup_printf("insight-paste-position-%d", value);
    GtkWidget *item = named(window, name);
    g_free(name);
    g_assert_nonnull(item);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
}

/* EN: Start ordinary Paste with clipboard text; wait controls whether mutations can be injected before completion.
 * RU: Запускает обычную вставку text; wait задаёт ожидание либо возможность изменить назначение до завершения. */
static void paste(const gchar *text, gboolean wait)
{
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
    keybindings_send_command(GEANY_KEY_GROUP_CLIPBOARD, GEANY_KEYS_CLIPBOARD_PASTE);
    if (wait) settle();
}

/* EN: Switch to doc's real notebook page without private editor symbols.
 * RU: Переключает настоящую вкладку doc без закрытых символов редактора. */
static void reveal(GeanyDocument *target)
{
    GtkWidget *child = GTK_WIDGET(target->editor->sci);
    for (GtkWidget *parent = gtk_widget_get_parent(child); parent;
         child = parent, parent = gtk_widget_get_parent(parent))
        if (GTK_IS_NOTEBOOK(parent))
            gtk_notebook_set_current_page(GTK_NOTEBOOK(parent), gtk_notebook_page_num(GTK_NOTEBOOK(parent), child));
    gtk_widget_grab_focus(GTK_WIDGET(target->editor->sci));
}

/* EN: Exercise all position choices, override precedence, partial reversed replacement and EOL conversion.
 * RU: Проверяет позиции, приоритет флажка, обратную частичную замену и преобразование EOL. */
static void positions(void)
{
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(smart), FALSE);
    for (gint mode = 0; mode < 3; mode++)
    {
        position(mode);
        reset("abcdef", 4, 2);
        paste("я", TRUE);
        expect("abяef", mode == 0 ? 4 : 2, mode == 1 ? 2 : 4);
        SEND(doc->editor->sci, SCI_UNDO, 0, 0);
        gchar *text = sci_get_contents(doc->editor->sci, -1);
        g_assert_cmpstr(text, ==, "abcdef");
        g_free(text);
        g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
        SEND(doc->editor->sci, SCI_REDO, 0, 0);
        text = sci_get_contents(doc->editor->sci, -1);
        g_assert_cmpstr(text, ==, "abяef");
        g_free(text);
    }
    position(0);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(always), TRUE);
    reset("AB", 1, 1);
    paste("xy", TRUE);
    expect("AxyB", 1, 3);
    position(1);
    reset("AB", 1, 1);
    paste("xy", TRUE);
    expect("AxyB", 1, 3);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(always), FALSE);
    reset("AB", 1, 1);
    paste("xy", TRUE);
    expect("AxyB", 1, 1);
    position(2);
    reset("AB", 1, 1);
    SEND(doc->editor->sci, SCI_SETEOLMODE, SC_EOL_CRLF, 0);
    paste("я\nz\r", TRUE);
    expect("Aя\r\nz\r\nB", 1, 8);
    reset("AB", 1, 1);
    SEND(doc->editor->sci, SCI_SETPASTECONVERTENDINGS, FALSE, 0);
    paste("x\r\ny", TRUE);
    expect("Ax\r\nyB", 1, 5);
    reset("AB", 1, 1);
    paste("", TRUE);
    expect("AB", 1, 1);
}

/* EN: Ensure asynchronous paste refuses edited/Undo-restored destinations, moved carets, changed preferences and tabs.
 * RU: Проверяет отказ отложенной вставки после правки/Undo, перемещения курсора, смены настроек и вкладки. */
static void guards(void)
{
    position(2);
    reset("AB", 1, 1);
    paste("xy", FALSE);
    SEND(doc->editor->sci, SCI_INSERTTEXT, 0, (sptr_t)" ");
    SEND(doc->editor->sci, SCI_UNDO, 0, 0);
    SEND(doc->editor->sci, SCI_SETSEL, 1, 1);
    settle();
    gchar *text = sci_get_contents(doc->editor->sci, -1);
    g_assert_cmpstr(text, ==, "AB");
    g_free(text);
    reset("AB", 1, 1);
    paste("xy", FALSE);
    SEND(doc->editor->sci, SCI_SETSEL, 0, 0);
    settle();
    expect("AB", 0, 0);
    reset("AB", 1, 1);
    paste("xy", FALSE);
    position(1);
    position(2);
    settle();
    expect("AB", 1, 1);
    reset("AB", 1, 1);
    paste("xy", FALSE);
    doc->readonly = TRUE;
    SEND(doc->editor->sci, SCI_SETREADONLY, TRUE, 0);
    settle();
    expect("AB", 1, 1);
    reset("AB", 1, 1);
    paste("xy", FALSE);
    GeanyDocument *other = document_new_file("paste-other.txt", NULL, "other");
    settle();
    text = sci_get_contents(doc->editor->sci, -1);
    g_assert_cmpstr(text, ==, "AB");
    g_free(text);
    text = sci_get_contents(other->editor->sci, -1);
    g_assert_cmpstr(text, ==, "other");
    g_free(text);
    document_set_text_changed(other, FALSE);
    g_assert_true(document_close(other));
    reveal(doc);
}

/* EN: Verify a language forbidding intelligent reindent still gets ordinary paste position policies.
 * RU: Проверяет обычные правила вставки для языка, запрещающего умные переотступы. */
static void language_guard(void)
{
    GeanyDocument *original = doc;
    doc = document_open_file(denied, FALSE, NULL, NULL);
    g_assert_nonnull(doc);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(smart), TRUE);
    position(2);
    reset("  AB", 3, 3);
    paste("    x();\n", TRUE);
    expect("  A    x();\nB", 3, 12);
    document_set_text_changed(doc, FALSE);
    g_assert_true(document_close(doc));
    doc = original;
    reveal(doc);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(smart), FALSE);
}

/* EN: Paste a plain CRLF clip through the real Clips panel, preserving its bytes and applying the same selection policy.
 * RU: Вставляет обычный CRLF-клип настоящей панелью Clips, сохраняя байты и применяя общее выделение. */
static void clips(void)
{
    position(2);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(always), FALSE);
    reset("я\r\nv", 0, 5);
    GtkWidget *query = named(window, "insight-clips-query");
    GtkWidget *copy = named(window, "insight-clip-copy");
    g_assert_nonnull(query);
    g_assert_nonnull(copy);
    gtk_entry_set_text(GTK_ENTRY(query), "PlainPastePolicyFixture");
    gtk_button_clicked(GTK_BUTTON(copy));
    reset("AB", 1, 1);
    GtkWidget *view = named(window, "insight-clips");
    GtkTreePath *path = gtk_tree_path_new_from_indices(0, -1);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), path);
    gtk_tree_path_free(path);
    GtkWidget *button = named(window, "insight-clip-paste");
    g_assert_nonnull(button);
    gtk_button_clicked(GTK_BUTTON(button));
    expect("Aя\r\nvB", 1, 6);
    SEND(doc->editor->sci, SCI_UNDO, 0, 0);
    g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
}

/* EN: Close the captured destination while clipboard reading is pending and ensure the surviving tab stays untouched.
 * RU: Закрывает зафиксированное назначение до получения буфера и проверяет неизменность оставшейся вкладки. */
static void closed_destination(void)
{
    reset("AB", 1, 1);
    paste("xy", FALSE);
    GeanyDocument *original = doc;
    doc = document_new_file("paste-survivor.txt", NULL, "survivor");
    SEND(doc->editor->sci, SCI_SETSEL, 0, 0);
    document_set_text_changed(original, FALSE);
    g_assert_true(document_close(original));
    settle();
    expect("survivor", 0, 0);
}

/* EN: Assert preference persistence and rollback after a denied config write, without changing a project file.
 * RU: Проверяет сохранение настроек и откат при запрете записи конфигурации без изменения проекта. */
static void persistence(void)
{
    position(1);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(always), TRUE);
    gchar *path = g_build_filename(directory, "insight-paste.conf", NULL);
    GKeyFile *key = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(key, path, 0, NULL));
    g_assert_cmpint(g_key_file_get_integer(key, "typing", "paste-position", NULL), ==, 1);
    g_assert_true(g_key_file_get_boolean(key, "typing", "always-select-pasted-text", NULL));
    gchar *saved = g_key_file_to_data(key, NULL, NULL);
    g_assert_cmpint(g_remove(path), ==, 0);
    g_assert_cmpint(g_mkdir(path, 0700), ==, 0);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(always), FALSE);
    g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(always)));
    reset("AB", 1, 1);
    paste("xy", TRUE);
    expect("AxyB", 1, 3);
    g_assert_cmpint(g_rmdir(path), ==, 0);
    g_assert_true(g_file_set_contents(path, saved, -1, NULL));
    g_free(saved);
    g_key_file_unref(key);
    g_free(path);
}

/* EN: Run all scenarios once the real editor is initialized; data is unused.
 * RU: Запускает сценарии после инициализации настоящего редактора; data не используется. */
static gboolean run(gpointer data)
{
    (void)data;
    doc = document_get_current();
    if (!doc) return G_SOURCE_CONTINUE;
    window = gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
    smart = named(window, "insight-intelligent-paste-option");
    always = named(window, "insight-paste-always-select");
    g_assert_nonnull(smart);
    g_assert_nonnull(always);
    positions();
    guards();
    language_guard();
    clips();
    persistence();
    closed_destination();
    g_print("Paste policy GUI: ordinary/partial UTF-8, all positions/override, CRLF/native EOLs, Undo/Redo, edit+Undo/preferences/tab/readonly guards, denied intelligent profile and save rollback passed\n");
    document_set_text_changed(doc, FALSE);
    gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
    return G_SOURCE_REMOVE;
}

/* EN: Fail a stalled test; data is unused.
 * RU: Завершает зависшую проверку ошибкой; data не используется. */
static gboolean timeout(gpointer data)
{
    (void)data;
    g_error("Paste policy GUI timeout");
    return G_SOURCE_REMOVE;
}

/* EN: Start an isolated editor and a language profile with intelligent paste denied; no user settings are used.
 * RU: Запускает изолированный редактор с языком, запрещающим умную вставку; настройки пользователя не используются. */
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
    directory = g_dir_make_tmp("paste-policy-XXXXXX", NULL);
    denied = g_build_filename(directory, "denied.cpp", NULL);
    gchar *source = g_build_filename(directory, "source.cpp", NULL);
    gchar *folder = g_build_filename(directory, "languages", "plain-only", NULL);
    g_assert_cmpint(g_mkdir_with_parents(folder, 0700), ==, 0);
    gchar *language = g_build_filename(folder, "language.conf", NULL);
    gchar *profile = g_strdup_printf("[language]\nname=Plain Only\nparser=C++\nfiles=%s;\n[editing]\nversion=1\nenabled=true\nallow-intelligent-paste=false\n", denied);
    g_assert_true(g_file_set_contents(language, profile, -1, NULL));
    gchar *config = g_build_filename(directory, "geany.conf", NULL);
    g_assert_true(g_file_set_contents(config, "[geany]\nconfirm_exit=false\n", -1, NULL));
    g_assert_true(g_file_set_contents(source, "AB", -1, NULL));
    g_assert_true(g_file_set_contents(denied, "AB", -1, NULL));
    gchar *argv[] = {"paste-policy", "--new-instance", "--no-session", "--config", directory, source, NULL};
    g_timeout_add(400, run, NULL);
    g_timeout_add_seconds(45, timeout, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    return main_lib(6, argv);
}
