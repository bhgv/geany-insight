/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify C/C++ and HDL .inc files coexist without global reassignment.
 * RU: Проверяет совместную работу .inc C/C++ и HDL без глобального назначения языка. */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window;
static gchar *root, *profile, *project, *cpp, *hdl, *cpp_inc, *c_inc, *hdl_inc, *shared;
static guint phase, profile_step;
static gboolean profile_done;
static const gchar *cpp_source = "#include \"cpp.inc\"\n#include \"shared.inc\"\n";
static const gchar *hdl_source = "`include \"hdl.inc\"\n`include \"shared.inc\"\nmodule top; endmodule\n";

/* EN: Find widget name below root, including menu subtrees; return NULL if absent.
 * RU: Находит виджет name в root, включая меню; возвращает NULL при отсутствии. */
static GtkWidget *named(GtkWidget *root, const gchar *name)
{
    if (!g_strcmp0(gtk_widget_get_name(root), name)) return root;
    if (GTK_IS_ENTRY(root) && !g_strcmp0(gtk_entry_get_placeholder_text(GTK_ENTRY(root)), name))
        return root;
    if (GTK_IS_MENU_ITEM(root))
    {
        GtkWidget *menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));
        GtkWidget *found = menu ? named(menu, name) : NULL;
        if (found) return found;
    }
    if (GTK_IS_CONTAINER(root))
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(root));
        GtkWidget *found = NULL;
        for (GList *p = children; p && !found; p = p->next)
            found = named(p->data, name);
        g_list_free(children);
        return found;
    }
    return NULL;
}

/* EN: Return the current model for named view; fail if the view is absent.
 * RU: Возвращает модель представления name; отсутствие виджета завершает тест. */
static GtkTreeModel *model(const gchar *name)
{
    GtkWidget *view = named(window, name);
    g_assert_nonnull(view);
    return gtk_tree_view_get_model(GTK_TREE_VIEW(view));
}

/* EN: Run project symbol query marker after activating file path.
 * RU: Запускает поиск символов marker после активации файла path. */
static void symbols(const gchar *path)
{
    document_open_file(path, FALSE, NULL, NULL);
    gtk_entry_set_text(GTK_ENTRY(named(window, "insight-symbol-query")), "marker");
}

/* EN: Check saved compatibility for language name; return FALSE until written.
 * RU: Проверяет сохранённую совместимость name; возвращает FALSE до записи файла. */
static gboolean saved_profile(const gchar *name)
{
    gchar *path = g_build_filename(profile, "languages", name, "language.conf", NULL);
    GKeyFile *key = g_key_file_new();
    gboolean loaded = g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL);
    gchar *compatible = loaded ? g_key_file_get_string(key, "language", "compatible-extensions", NULL) : NULL;
    gboolean ready = compatible && strstr(compatible, "inc");
    g_free(compatible);
    g_key_file_unref(key);
    g_free(path);
    return ready;
}

/* EN: Apply built-in C++/HDL options without losing effective .inc extensions;
 * data is unused. Use the actual modal dialog and capture it when requested.
 * RU: Сохраняет встроенные C++/HDL-настройки без потери действующих .inc;
 * data не используется. Работает с настоящим диалогом и при необходимости снимает его. */
static gboolean profile_options(gpointer data)
{
    (void)data;
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *dialog = NULL;
    for (GList *p = windows; p; p = p->next)
        if (!g_strcmp0(gtk_widget_get_name(p->data), "insight-language-dialog")) dialog = p->data;
    g_list_free(windows);
    if (!dialog) return G_SOURCE_CONTINUE;
    GtkWidget *choice = named(dialog, "insight-language-profile");
    if (profile_step == 0)
    {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(choice), "C++");
        g_assert_nonnull(strstr(gtk_entry_get_text(GTK_ENTRY(named(dialog,
            "insight-language-compatible"))), "inc"));
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);
        profile_step++;
    }
    else if (profile_step == 1)
    {
        if (!saved_profile("C++")) return G_SOURCE_CONTINUE;
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(choice), "SystemVerilog");
        const gchar *compatible = gtk_entry_get_text(GTK_ENTRY(named(dialog, "insight-language-compatible")));
        g_assert_nonnull(strstr(compatible, "svh"));
        g_assert_nonnull(strstr(compatible, "inc"));
        profile_step++;
        /* Let GTK paint the chosen profile before the next timer capture. */
    }
    else if (profile_step == 2)
    {
        g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(choice)), ==, "SystemVerilog");
        const gchar *capture = g_getenv("INSIGHT_INCLUDE_PROFILE_CAPTURE");
        if (capture)
        {
            GdkWindow *native = gtk_widget_get_window(dialog);
            GdkPixbuf *image = gdk_pixbuf_get_from_window(native, 0, 0,
                gdk_window_get_width(native), gdk_window_get_height(native));
            g_assert_nonnull(image);
            g_assert_true(gdk_pixbuf_save(image, capture, "jpeg", NULL, "quality", "88", NULL));
            g_object_unref(image);
        }
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);
        profile_step++;
    }
    else
    {
        if (!saved_profile("SystemVerilog")) return G_SOURCE_CONTINUE;
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(choice), "C++");
        g_assert_nonnull(strstr(gtk_entry_get_text(GTK_ENTRY(named(dialog,
            "insight-language-compatible"))), "inc"));
        profile_done = TRUE;
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Advance mixed-project checks; data is unused. Stop after all assertions.
 * RU: Продвигает проверки смешанного проекта; data не используется. Завершает после проверок. */
static gboolean tick(gpointer data)
{
    (void)data;
    if (!document_get_current()) return G_SOURCE_CONTINUE;
    if (!window) window = gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));
    if (!phase)
    {
        symbols(cpp);
        gtk_entry_set_text(GTK_ENTRY(named(window, "insight-file-query")), "*.inc");
        phase++;
    }
    else if (phase == 1)
    {
        if (gtk_tree_model_iter_n_children(model("insight-project-files"), NULL) != 4 ||
            gtk_tree_model_iter_n_children(model("insight-project-symbols"), NULL) != 2)
            return G_SOURCE_CONTINUE;
        g_assert_true(document_find_by_filename(cpp_inc)->file_type == filetypes_lookup_by_name("C++"));
        g_assert_true(document_find_by_filename(c_inc)->file_type == filetypes_lookup_by_name("C"));
        g_assert_true(document_find_by_filename(hdl_inc)->file_type == filetypes_lookup_by_name("SystemVerilog"));
        symbols(hdl);
        phase++;
    }
    else if (phase == 2)
    {
        if (gtk_tree_model_iter_n_children(model("insight-project-symbols"), NULL) != 1)
            return G_SOURCE_CONTINUE;
        GeanyDocument *ambiguous = document_open_file(shared, FALSE, NULL, NULL);
        g_assert_true(ambiguous->file_type == filetypes_lookup_by_name("Pascal"));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window, "insight-search-keywords")), FALSE);
        GtkWidget *query = named(window, "insight-project-query");
        gtk_entry_set_text(GTK_ENTRY(query), "_marker");
        g_signal_emit_by_name(query, "activate");
        phase++;
    }
    else if (phase == 3)
    {
        const gchar *status = gtk_label_get_text(GTK_LABEL(named(window, "insight-search-status")));
        if (!strstr(status, "7 files scanned") ||
            gtk_tree_model_iter_n_children(model("insight-search-results"), NULL) != 3)
            return G_SOURCE_CONTINUE;
        sci_set_text(document_find_by_filename(cpp)->editor->sci, "#include \"shared.inc\"\n");
        gchar *changed = g_strconcat("`include \"cpp.inc\"\n", hdl_source, NULL);
        sci_set_text(document_find_by_filename(hdl)->editor->sci, changed);
        g_free(changed);
        phase++;
    }
    else if (phase == 4)
    {
        if (document_find_by_filename(cpp_inc)->file_type != filetypes_lookup_by_name("SystemVerilog"))
            return G_SOURCE_CONTINUE;
        gchar *disk = NULL;
        g_assert_true(g_file_get_contents(cpp, &disk, NULL, NULL));
        g_assert_cmpstr(disk, ==, cpp_source);
        g_free(disk);
        g_assert_true(g_file_get_contents(hdl, &disk, NULL, NULL));
        g_assert_cmpstr(disk, ==, hdl_source);
        g_free(disk);
        sci_set_text(document_find_by_filename(cpp)->editor->sci, cpp_source);
        sci_set_text(document_find_by_filename(hdl)->editor->sci, hdl_source);
        phase++;
    }
    else if (phase == 5)
    {
        if (document_find_by_filename(cpp_inc)->file_type != filetypes_lookup_by_name("C++"))
            return G_SOURCE_CONTINUE;
        phase++;
        g_timeout_add(150, profile_options, NULL);
        gtk_menu_item_activate(GTK_MENU_ITEM(named(window, "insight-language-options")));
    }
    else if (phase == 6)
    {
        if (!profile_done) return G_SOURCE_CONTINUE;
        symbols(cpp);
        phase++;
    }
    else if (phase == 7)
    {
        if (gtk_tree_model_iter_n_children(model("insight-project-symbols"), NULL) != 2)
            return G_SOURCE_CONTINUE;
        symbols(hdl);
        phase++;
    }
    else
    {
        if (gtk_tree_model_iter_n_children(model("insight-project-symbols"), NULL) != 1)
            return G_SOURCE_CONTINUE;
        document_set_text_changed(document_find_by_filename(cpp), FALSE);
        document_set_text_changed(document_find_by_filename(hdl), FALSE);
        g_print("Mixed .inc GUI passed: C/C++/HDL session types, separate symbol indexes, all-file text search, shared ambiguity, unsaved include ownership roundtrip and saved/reopened built-in compatibility\n");
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Fail stalled phase and print model counts; data is unused.
 * RU: Завершает зависший этап и выводит размеры моделей; data не используется. */
static gboolean timeout(gpointer data)
{
    (void)data;
    if (window)
        g_printerr("Mixed includes: files=%d symbols=%d results=%d\n",
            gtk_tree_model_iter_n_children(model("insight-project-files"), NULL),
            gtk_tree_model_iter_n_children(model("insight-project-symbols"), NULL),
            gtk_tree_model_iter_n_children(model("insight-search-results"), NULL));
    g_error("Mixed include GUI timeout at phase %u", phase);
    return G_SOURCE_REMOVE;
}

/* EN: Write fixture name/text below sources and return its owned path.
 * RU: Записывает файл name/text в sources и возвращает принадлежащий вызывающему путь. */
static gchar *file(const gchar *sources, const gchar *name, const gchar *text)
{
    gchar *path = g_build_filename(sources, name, NULL);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
    return path;
}

/* EN: Open all ambiguous session buffers before enumeration; run real editor.
 * Return 77 without a GUI environment, otherwise the editor exit status.
 * RU: Открывает неоднозначные буферы до обхода и запускает редактор.
 * Возвращает 77 без графической среды, иначе код завершения редактора. */
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
    root = g_dir_make_tmp("mixed-includes-XXXXXX", NULL);
    profile = g_build_filename(root, "profile", NULL);
    gchar *sources = g_build_filename(root, "sources", NULL);
    g_assert_cmpint(g_mkdir_with_parents(profile, 0700), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(sources, 0700), ==, 0);
    gchar *config = file(profile, "geany.conf", "[geany]\nconfirm_exit=false\n");
    g_free(config);
    cpp = file(sources, "main.cpp", cpp_source);
    config = file(sources, "main.c", "#include \"c.inc\"\n");
    g_free(config);
    hdl = file(sources, "top.sv", hdl_source);
    cpp_inc = file(sources, "cpp.inc", "int cpp_marker;\n");
    c_inc = file(sources, "c.inc", "int c_marker;\n");
    hdl_inc = file(sources, "hdl.inc", "module leaf; wire hdl_marker; endmodule\n");
    shared = file(sources, "shared.inc", "// shared by two language families\n");
    project = file(root, "mixed.geany", "[project]\nname=Mixed includes\nbase_path=sources\n");
    g_free(sources);
    gchar *argv[] = {"mixed-includes", "--new-instance", "--no-session", "--config", profile,
        project, cpp_inc, c_inc, hdl_inc, NULL};
    g_timeout_add(200, tick, NULL);
    g_timeout_add_seconds(45, timeout, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    return main_lib(9, argv);
}
