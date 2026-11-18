/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise the installed File Compare window, real snapshot parsers and non-destructive merge-mode switching.
 * RU: Проверяет установленное File Compare, настоящие парсеры снимков и переключение сведения без потери результата. */
#include <gtk/gtk.h>
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "sciwrappers.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>

static GtkWidget *editor_window;
static gchar *left_path, *right_path, *verilog_left, *verilog_right, *retained;
static guint phase;
static gboolean passed;
static const gchar *left_code =
    "int global_value = 1;\n\nint alpha(int x)\n{\n    return x + 1;\n}\n\n\n"
    "class Box\n{\npublic:\n    int calc()\n    {\n        return 7;\n    }\n};\n";
static const gchar *right_code =
    "int global_value = 2;\n\nint renamed_alpha(int x)\n{\n    return x + 2;\n}\n\n\n"
    "class Box\n{\npublic:\n    int calc()\n    {\n        return 9;\n    }\n};\n";

/* EN: Find key by widget name or command caption below widget, including menu subtrees; return NULL when absent.
 * RU: Ищет key по имени виджета или подписи команды под widget, включая подменю; возвращает NULL при отсутствии. */
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

/* EN: Return the visible production comparison window, or NULL during startup.
 * RU: Возвращает видимое настоящее окно сравнения либо NULL при запуске. */
static GtkWidget *comparison_window(void)
{
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *found = NULL;
    for (GList *it = windows; it && !found; it = it->next)
        if (gtk_widget_get_visible(it->data) && !g_strcmp0(gtk_widget_get_name(it->data), "insight-diff-window"))
            found = it->data;
    g_list_free(windows);
    return found;
}

/* EN: Return owned text from view's GTK buffer.
 * RU: Возвращает собственный текст GTK-буфера view. */
static gchar *text(GtkWidget *view)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    return gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
}

/* EN: Check snapshot-context labels in window for fragment, matching exactly when exact is TRUE.
 * RU: Проверяет подписи контекста window на fragment, точно при exact=TRUE. */
static gboolean context(GtkWidget *window, const gchar *fragment, gboolean exact)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(find(window, "insight-diff-differences")));
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *label = NULL;
        gtk_tree_model_get(model, &row, 5, &label, -1);
        gboolean match = label && (exact ? !strcmp(label, fragment) : strstr(label, fragment) != NULL);
        g_free(label);
        if (match)
            return TRUE;
    }
    return FALSE;
}

/* EN: Click window's named enabled button; fail on absent or disabled controls.
 * RU: Нажимает доступную кнопку name в window; отсутствие либо недоступность завершает проверку. */
static void click(GtkWidget *window, const gchar *name)
{
    GtkWidget *button = find(window, name);
    if (!button)
        g_error("Missing File Compare control: %s (phase %u)", name, phase);
    g_assert_true(gtk_widget_is_sensitive(button));
    gtk_button_clicked(GTK_BUTTON(button));
}

/* EN: Save window's requested real production capture from variable after a separate paint cycle.
 * RU: Сохраняет запрошенный снимок настоящего window из variable после отдельного цикла отрисовки. */
static void capture(GtkWidget *window, const gchar *variable)
{
    const gchar *path = g_getenv(variable);
    if (!path)
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

/* EN: Capture the scoped change dialog after painting and verify paired Verilog snapshots before accepting.
 * RU: Снимает диалог изменений после отрисовки и проверяет два снимка Verilog перед подтверждением. */
static gboolean capture_scope(gpointer data)
{
    (void)data;
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *dialog = NULL;
    for (GList *it = windows; it; it = it->next)
        if (!g_strcmp0(gtk_widget_get_name(it->data), "insight-diff-scope-changes"))
            dialog = it->data;
    g_list_free(windows);
    g_assert_nonnull(dialog);
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(find(dialog, "insight-diff-scope-list")));
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 1);
    gchar *left = text(find(dialog, "insight-diff-scope-left"));
    gchar *right = text(find(dialog, "insight-diff-scope-right"));
    g_assert_nonnull(strstr(left, "value = x + 1;"));
    g_assert_nonnull(strstr(right, "value = x + 2;"));
    g_free(left);
    g_free(right);
    capture(dialog, "INSIGHT_FILE_SCOPE_CAPTURE");
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* EN: Capture the retained dialog in data after its filtered list and preview have painted, then accept it.
 * RU: Снимает сохранённый dialog из data после отрисовки фильтра и предпросмотра, затем подтверждает выбор. */
static gboolean capture_symbols(gpointer data)
{
    GtkWidget *dialog = data;
    capture(dialog, "INSIGHT_FILE_SYMBOLS_CAPTURE");
    g_timeout_add(250, capture_scope, NULL);
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);
    return G_SOURCE_REMOVE;
}

/* EN: Inspect the real symbol browser after painting, capture it and choose the filtered Verilog function; data is unused.
 * RU: Проверяет настоящий список символов после отрисовки, снимает его и выбирает функцию Verilog; data не используется. */
static gboolean browse_symbols(gpointer data)
{
    (void)data;
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *dialog = NULL;
    for (GList *it = windows; it; it = it->next)
        if (!g_strcmp0(gtk_widget_get_name(it->data), "insight-diff-symbol-browser"))
            dialog = it->data;
    g_list_free(windows);
    g_assert_nonnull(dialog);
    GtkWidget *tree = find(dialog, "insight-diff-symbol-list");
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), >=, 2);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(find(dialog, "insight-diff-symbol-changed")), TRUE);
    gtk_entry_set_text(GTK_ENTRY(find(dialog, "insight-diff-symbol-filter")), "VALUE");
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 1);
    gchar *preview = text(find(dialog, "insight-diff-symbol-preview"));
    g_assert_nonnull(strstr(preview, "module Top;"));
    g_assert_null(strstr(preview, "stale_live_buffer"));
    g_free(preview);
    gtk_widget_queue_draw(dialog);
    g_timeout_add_full(G_PRIORITY_DEFAULT, 150, capture_symbols, g_object_ref(dialog), g_object_unref);
    return G_SOURCE_REMOVE;
}

/* EN: Advance the isolated fixture from unused data; test exact on-disk versions even with a different live buffer.
 * Return REMOVE after C++/Verilog, layout and retained-result assertions; failures abort the test.
 * RU: Продвигает фикстуру из неиспользуемого data; проверяет дисковые версии при отличающемся живом буфере.
 * Возвращает REMOVE после проверок C++/Verilog, компоновки и результата; ошибки завершают тест. */
static gboolean drive(gpointer data)
{
    (void)data;
    if (!editor_window)
    {
        GeanyDocument *document = document_get_current();
        if (!DOC_VALID(document))
            return G_SOURCE_CONTINUE;
        editor_window = gtk_widget_get_toplevel(GTK_WIDGET(document->editor->sci));
        sci_set_text(document->editor->sci, "int stale_live_buffer() { return 0; }\n");
        document_set_text_changed(document, FALSE);
    }
    if (!phase)
    {
        GtkWidget *menu = find(editor_window, "Diff / Merge…");
        if (!menu)
            return G_SOURCE_CONTINUE;
        gtk_menu_item_activate(GTK_MENU_ITEM(menu));
        phase = 1;
        return G_SOURCE_CONTINUE;
    }
    GtkWidget *window = comparison_window();
    if (!window)
        return G_SOURCE_CONTINUE;
    GtkWidget *mode = find(window, "insight-diff-merge-mode");
    GtkWidget *result_panel = find(window, "insight-diff-result-panel");
    GtkWidget *result_view = find(window, "insight-diff-result");
    GtkWidget *compare = find(window, "insight-diff-compare");
    if (phase == 1)
    {
        g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mode)));
        g_assert_false(gtk_widget_get_visible(result_panel));
        gtk_entry_set_text(GTK_ENTRY(find(window, "insight-diff-path-left")), left_path);
        gtk_entry_set_text(GTK_ENTRY(find(window, "insight-diff-path-right")), right_path);
        click(window, "Compare");
        phase = 2;
    }
    else if (phase == 2 && gtk_widget_is_sensitive(compare))
    {
        g_assert_true(context(window, "alpha → renamed_alpha", TRUE));
        g_assert_true(context(window, "Box::calc", TRUE));
        g_assert_true(context(window, "No symbol context", TRUE));
        g_assert_false(context(window, "stale_live_buffer", FALSE));
        gchar *left = text(find(window, "insight-diff-left"));
        g_assert_cmpstr(left, ==, left_code);
        g_free(left);
        GtkWidget *split = find(window, "insight-diff-browser-split");
        g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(split)), ==, GTK_ORIENTATION_HORIZONTAL);
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(find(window, "insight-diff-differences")));
        GtkTreePath *path = gtk_tree_path_new_from_indices(1, -1);
        gtk_tree_selection_select_path(selection, path);
        gtk_tree_path_free(path);
        gtk_window_present(GTK_WINDOW(window));
        gdk_window_raise(gtk_widget_get_window(window));
        gtk_widget_queue_draw(window);
        phase = 3;
    }
    else if (phase == 3)
    {
        GtkWidget *list = find(window, "insight-diff-differences");
        GtkWidget *left = find(window, "insight-diff-left");
        GtkWidget *right = find(window, "insight-diff-right");
        gint list_x, list_y, left_x, left_y, right_x, right_y;
        g_assert_true(gtk_widget_translate_coordinates(list, window, 0, 0, &list_x, &list_y));
        g_assert_true(gtk_widget_translate_coordinates(left, window, 0, 0, &left_x, &left_y));
        g_assert_true(gtk_widget_translate_coordinates(right, window, 0, 0, &right_x, &right_y));
        g_assert_cmpint(list_x, <, left_x);
        g_assert_cmpint(left_x, <, right_x);
        g_assert_cmpint(ABS(left_y - right_y), <=, 3);
        capture(window, "INSIGHT_FILE_COMPARE_CAPTURE");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mode), TRUE);
        g_assert_true(gtk_widget_get_visible(result_panel));
        gtk_notebook_set_current_page(GTK_NOTEBOOK(find(window, "insight-diff-toolbar-tabs")), 1);
        click(window, "Take Right");
        retained = text(result_view);
        g_assert_cmpstr(retained, !=, left_code);
        phase = 4;
    }
    else if (phase == 4)
    {
        capture(window, "INSIGHT_FILE_MERGE_CAPTURE");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mode), FALSE);
        g_assert_false(gtk_widget_get_visible(result_panel));
        gchar *value = text(result_view);
        g_assert_cmpstr(value, ==, retained);
        g_free(value);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mode), TRUE);
        click(window, "insight-diff-undo");
        value = text(result_view);
        g_assert_cmpstr(value, ==, left_code);
        g_free(value);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mode), FALSE);
        gtk_entry_set_text(GTK_ENTRY(find(window, "insight-diff-path-left")), verilog_left);
        gtk_entry_set_text(GTK_ENTRY(find(window, "insight-diff-path-right")), verilog_right);
        click(window, "Compare");
        phase = 5;
    }
    else if (phase == 5 && gtk_widget_is_sensitive(compare))
    {
        g_assert_true(context(window, "value", FALSE));
        g_assert_true(context(window, "Top", TRUE));
        g_assert_false(context(window, "alpha", FALSE));
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(find(window, "insight-diff-left")));
        gchar *snapshot = text(find(window, "insight-diff-left"));
        const gchar *call = strstr(snapshot, "value =");
        g_assert_nonnull(call);
        gint original = g_utf8_pointer_to_offset(snapshot, call);
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_offset(buffer, &at, original);
        gtk_text_buffer_place_cursor(buffer, &at);
        click(window, "insight-diff-symbol-0-0");
        gtk_text_buffer_get_iter_at_mark(buffer, &at, gtk_text_buffer_get_insert(buffer));
        g_assert_cmpint(gtk_text_iter_get_line(&at), ==, 4);
        click(window, "insight-diff-symbol-0-2");
        gtk_text_buffer_get_iter_at_mark(buffer, &at, gtk_text_buffer_get_insert(buffer));
        g_assert_cmpint(gtk_text_iter_get_offset(&at), ==, original);
        phase = 6;
        g_timeout_add(200, browse_symbols, NULL);
        click(window, "insight-diff-symbol-0-3");
        gtk_text_buffer_get_iter_at_mark(buffer, &at, gtk_text_buffer_get_insert(buffer));
        g_assert_cmpint(gtk_text_iter_get_line(&at), ==, 7);
        click(window, "insight-diff-symbol-0-2");
        gtk_text_buffer_get_iter_at_mark(buffer, &at, gtk_text_buffer_get_insert(buffer));
        g_assert_cmpint(gtk_text_iter_get_offset(&at), ==, original);
        g_free(snapshot);
        g_print("File Compare GTK: side list, two-source default, C++/Verilog snapshot symbols, different live buffer, explicit merge and retained Undo passed.\n");
        passed = TRUE;
        gtk_widget_destroy(window);
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(editor_window, "menu_quit1")));
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Fail a stalled scenario with phase and currently visible context labels; data is unused.
 * RU: Завершает зависший сценарий с phase и видимыми подписями контекста; data не используется. */
static gboolean timeout(gpointer data)
{
    (void)data;
    g_error("File Compare GUI timed out at phase %u", phase);
    return G_SOURCE_REMOVE;
}

/* EN: Run an isolated editor profile using argc/argv, captured C++ files and Verilog fixtures; return nonzero on failure.
 * RU: Запускает отдельный профиль argc/argv с файлами C++ и Verilog; возвращает ненулевой код при ошибке. */
int main(int argc, char **argv)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY"))
        return 77;
    (void)argc;
    gchar *directory = g_dir_make_tmp("file-compare-native-XXXXXX", NULL);
    g_assert_nonnull(directory);
    gchar *profile = g_build_filename(directory, "profile", NULL);
    gchar *settings = g_build_filename(profile, "geany.conf", NULL);
    g_assert_cmpint(g_mkdir_with_parents(profile, 0700), ==, 0);
    g_assert_true(g_file_set_contents(settings, "[geany]\nconfirm_exit=false\n", -1, NULL));
    left_path = g_build_filename(directory, "left.cpp", NULL);
    right_path = g_build_filename(directory, "right.cpp", NULL);
    verilog_left = g_build_filename(directory, "left.v", NULL);
    verilog_right = g_build_filename(directory, "right.v", NULL);
    g_assert_true(g_file_set_contents(left_path, left_code, -1, NULL));
    g_assert_true(g_file_set_contents(right_path, right_code, -1, NULL));
    const gchar *verilog = "module Top;\nparameter WIDTH = %u;\n\n\nfunction integer value;\ninput integer x;\nbegin\nvalue = x + %u;\nend\nendfunction\n\ninitial $display(\"endmodule\");\nendmodule\n";
    gchar *first = g_strdup_printf(verilog, 8, 1);
    gchar *second = g_strdup_printf(verilog, 9, 2);
    g_assert_true(g_file_set_contents(verilog_left, first, -1, NULL));
    g_assert_true(g_file_set_contents(verilog_right, second, -1, NULL));
    gchar *args[] = {argv[0], "--new-instance", "--no-session", "--no-plugins", "--config", profile, left_path, NULL};
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_timeout_add(100, drive, NULL);
    g_timeout_add_seconds(60, timeout, NULL);
    gint result = main_lib(G_N_ELEMENTS(args) - 1, args);
    g_free(first);
    g_free(second);
    g_free(retained);
    g_free(left_path);
    g_free(right_path);
    g_free(verilog_left);
    g_free(verilog_right);
    g_free(settings);
    g_free(profile);
    g_free(directory);
    return passed ? result : 1;
}
