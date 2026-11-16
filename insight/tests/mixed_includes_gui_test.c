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
static guint phase;

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
    else
    {
        const gchar *status = gtk_label_get_text(GTK_LABEL(named(window, "insight-search-status")));
        if (!strstr(status, "7 files scanned") ||
            gtk_tree_model_iter_n_children(model("insight-search-results"), NULL) != 3)
            return G_SOURCE_CONTINUE;
        g_print("Mixed .inc GUI passed: C/C++/HDL session types, separate symbol indexes, all-file text search, shared-header ambiguity and four filename matches\n");
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
    cpp = file(sources, "main.cpp", "#include \"cpp.inc\"\n#include \"shared.inc\"\n");
    config = file(sources, "main.c", "#include \"c.inc\"\n");
    g_free(config);
    hdl = file(sources, "top.sv", "`include \"hdl.inc\"\n`include \"shared.inc\"\nmodule top; endmodule\n");
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
