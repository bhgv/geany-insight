/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify HDL membership, indexing, region searches and included .inc symbols.
 * RU: Проверяет состав HDL-проекта, индекс, области поиска и символы из .inc. */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>

static GtkWidget *window;
static gchar *root, *profile, *project, *top, *include_path;
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

/* EN: Start query over project files in region using the actual Enter signal.
 * RU: Запускает query по проекту в region настоящим сигналом Enter. */
static void search(const gchar *query, gint region)
{
    gtk_combo_box_set_active(GTK_COMBO_BOX(named(window, "insight-search-scope")), 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(named(window, "insight-search-result-region")), region);
    GtkWidget *entry = named(window, "insight-project-query");
    gtk_entry_set_text(GTK_ENTRY(entry), query);
    g_signal_emit_by_name(entry, "activate");
}

/* EN: Return TRUE for completed project search with count rows and no skips.
 * RU: Возвращает TRUE для завершённого поиска с count строками без пропусков. */
static gboolean complete(gint count, gint files)
{
    const gchar *status = gtk_label_get_text(GTK_LABEL(named(window, "insight-search-status")));
    gchar expected[40];
    g_snprintf(expected, sizeof expected, "%d files scanned", files);
    return strstr(status, expected) && strstr(status, "0 skipped") &&
        gtk_tree_model_iter_n_children(model("insight-search-results"), NULL) == count;
}

/* EN: Run a lexical/semantic relation query by mode through the real entry.
 * RU: Запускает лексический/семантический запрос query режима mode через строку. */
static void relations(const gchar *query, gint mode)
{
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window, "insight-relations-follow")), FALSE);
    gtk_combo_box_set_active(GTK_COMBO_BOX(named(window, "insight-relation-mode")), mode);
    GtkWidget *entry = named(window, "insight-relation-query");
    gtk_entry_set_text(GTK_ENTRY(entry), query);
    g_signal_emit_by_name(entry, "activate");
}

/* EN: Advance production GUI scenarios; data is unused. Quit after success.
 * RU: Продвигает сценарии настоящего интерфейса; data не используется. Выходит после успеха. */
static gboolean tick(gpointer data)
{
    (void)data;
    if (!document_get_current()) return G_SOURCE_CONTINUE;
    if (!window) window = gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));
    if (!phase)
    {
        document_open_file(top, FALSE, NULL, NULL);
        gtk_entry_set_text(GTK_ENTRY(named(window, "insight-file-query")), "*.inc");
        gtk_entry_set_text(GTK_ENTRY(named(window, "insight-symbol-query")), "unit_");
        phase++;
    }
    else if (phase == 1)
    {
        if (gtk_tree_model_iter_n_children(model("insight-project-files"), NULL) != 1 ||
            gtk_tree_model_iter_n_children(model("insight-project-symbols"), NULL) != 5)
            return G_SOURCE_CONTINUE;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window, "insight-search-keywords")), FALSE);
        search("needle", 0);
        phase++;
    }
    else if (phase >= 2 && phase <= 5)
    {
        const gint counts[] = {25, 15, 10, 10};
        if (!complete(counts[phase - 2], 5)) return G_SOURCE_CONTINUE;
        if (phase < 5) search("needle", phase - 1);
        else
        {
            gtk_entry_set_text(GTK_ENTRY(named(window, "File masks: *.c;*.h;*.py")), "*.inc");
            search("needle", 0);
        }
        phase++;
    }
    else if (phase == 6)
    {
        if (!complete(5, 1)) return G_SOURCE_CONTINUE;
        GtkWidget *view = named(window, "insight-search-results");
        GtkTreePath *first = gtk_tree_path_new_from_indices(0, -1);
        gtk_tree_view_row_activated(GTK_TREE_VIEW(view), first, gtk_tree_view_get_column(GTK_TREE_VIEW(view), 0));
        gtk_tree_path_free(first);
        g_assert_cmpstr(document_get_current()->file_name, ==, include_path);
        g_assert_true(document_get_current()->file_type == filetypes_lookup_by_name("SystemVerilog"));
        relations("needle", 0);
        phase++;
    }
    else if (phase == 7)
    {
        const gchar *status = gtk_label_get_text(GTK_LABEL(named(window, "insight-relation-status")));
        if (!strstr(status, "files scanned") ||
            gtk_tree_model_iter_n_children(model("insight-relations"), NULL) != 25)
            return G_SOURCE_CONTINUE;
        gtk_combo_box_set_active(GTK_COMBO_BOX(named(window, "insight-references-comments")), 1);
        phase++;
    }
    else if (phase == 8)
    {
        const gchar *status = gtk_label_get_text(GTK_LABEL(named(window, "insight-relation-status")));
        if (!strstr(status, "files scanned") ||
            gtk_tree_model_iter_n_children(model("insight-relations"), NULL) != 15)
            return G_SOURCE_CONTINUE;
        if (g_getenv("INSIGHT_TEST_HDL_SEMANTIC"))
        {
            relations("marker_inc", 11);
            phase++;
            return G_SOURCE_CONTINUE;
        }
        phase = 10;
    }
    else if (phase == 9)
    {
        const gchar *status = gtk_label_get_text(GTK_LABEL(named(window, "insight-relation-status")));
        if (!g_str_has_prefix(status, "slang:")) return G_SOURCE_CONTINUE;
        GtkTreeModel *rows = model("insight-relations");
        GtkTreeIter iter;
        gboolean in_header = FALSE, in_top = FALSE;
        for (gboolean ok = gtk_tree_model_get_iter_first(rows, &iter); ok;
             ok = gtk_tree_model_iter_next(rows, &iter))
        {
            gchar *path = NULL;
            gtk_tree_model_get(rows, &iter, 1, &path, -1);
            in_header |= !g_strcmp0(path, include_path);
            in_top |= !g_strcmp0(path, top);
            g_free(path);
        }
        g_assert_true(in_header);
        g_assert_true(in_top);
        phase++;
    }
    else
    {
        g_print("HDL search passed: .v/.sv/.vh/.svh/.inc membership and module index, all/skip/comments/code regions, apostrophes, masks, include navigation, cross-HDL lexical references%s.\n",
            g_getenv("INSIGHT_TEST_HDL_SEMANTIC") ? " and slang references inside included .inc" : "");
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Fail a stalled scenario; data is unused.
 * RU: Завершает зависший сценарий; data не используется. */
static gboolean timeout(gpointer data)
{
    (void)data;
    if (window)
    {
        const gchar *views[] = {"insight-project-files", "insight-project-symbols", "insight-search-results"};
        for (guint n = 0; n < G_N_ELEMENTS(views); n++)
        {
            GtkTreeModel *rows = model(views[n]);
            g_printerr("%s: %d rows\n", views[n], gtk_tree_model_iter_n_children(rows, NULL));
            GtkTreeIter iter;
            for (gboolean ok = gtk_tree_model_get_iter_first(rows, &iter); ok;
                 ok = gtk_tree_model_iter_next(rows, &iter))
            {
                gchar *name = NULL;
                gtk_tree_model_get(rows, &iter, 0, &name, -1);
                g_printerr("  %s\n", name ? name : "(null)");
                g_free(name);
            }
        }
    }
    g_error("HDL search timeout at phase %u", phase);
    return G_SOURCE_REMOVE;
}

/* EN: Create five HDL source kinds and run the real editor. Return 77 without
 * a GUI environment, otherwise the editor exit status.
 * RU: Создаёт пять видов HDL-исходников и запускает редактор. Возвращает 77 без
 * графической среды, иначе код завершения редактора. */
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
    root = g_dir_make_tmp("hdl-search-XXXXXX", NULL);
    profile = g_build_filename(root, "profile", NULL);
    g_assert_cmpint(g_mkdir_with_parents(profile, 0700), ==, 0);
    gchar *config = g_build_filename(profile, "geany.conf", NULL);
    g_assert_true(g_file_set_contents(config, "[geany]\nconfirm_exit=false\n", -1, NULL));
    g_free(config);
    gchar *sources = g_build_filename(root, "sources", NULL);
    g_assert_cmpint(g_mkdir_with_parents(sources, 0700), ==, 0);
    const gchar *extensions[] = {"v", "sv", "vh", "svh", "inc"};
    for (guint n = 0; n < G_N_ELEMENTS(extensions); n++)
    {
        gchar *name = g_strdup_printf("unit.%s", extensions[n]);
        gchar *path = g_build_filename(sources, name, NULL);
        gchar *text = g_strdup_printf(
            "%smodule unit_%u;\nwire needle;\ninitial $display(\"needle\"); // needle\n"
            "/* needle */\nassign needle = 1'b0;\n%sendmodule\n",
            n == 0 ? "`include \"unit.inc\"\n" : "", n,
            n == 0 ? "unit_4 u(); initial $display(u.marker_inc);\n" :
            n == 4 ? "wire marker_inc; assign marker_inc = 1'b0;\n" : "");
        g_assert_true(g_file_set_contents(path, text, -1, NULL));
        if (!n) top = g_strdup(path);
        if (n == 4) include_path = g_strdup(path);
        g_free(text);
        g_free(path);
        g_free(name);
    }
    g_free(sources);
    project = g_build_filename(root, "hdl.geany", NULL);
    g_assert_true(g_file_set_contents(project, "[project]\nname=HDL search\nbase_path=sources\n", -1, NULL));
    gchar *argv[] = {"hdl-search", "--new-instance", "--no-session", "--config", profile, project, include_path, NULL};
    g_timeout_add(200, tick, NULL);
    g_timeout_add_seconds(45, timeout, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    return main_lib(7, argv);
}
