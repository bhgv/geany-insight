/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise production search commands across a mixed-language project.
 * RU: Проверяет настоящие команды поиска в проекте с разными языками. */
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
static GeanyDocument *cpp;
static gchar *directory, *profile, *project, *cpp_path, *text_path;
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

/* EN: Start query in scope through the actual project entry's Enter handler.
 * RU: Запускает query в scope настоящим обработчиком Enter строки проекта. */
static void search(const gchar *query, gint scope)
{
    gtk_combo_box_set_active(GTK_COMBO_BOX(named(window, "insight-search-scope")), scope);
    GtkWidget *entry = named(window, "insight-project-query");
    gtk_entry_set_text(GTK_ENTRY(entry), query);
    g_signal_emit_by_name(entry, "activate");
}

/* EN: Wait for completed search with count rows; return FALSE while pending.
 * RU: Ожидает завершённого поиска с count строками; до этого возвращает FALSE. */
static gboolean complete(gint count)
{
    const gchar *status = gtk_label_get_text(GTK_LABEL(named(window, "insight-search-status")));
    return strstr(status, "files scanned") &&
        gtk_tree_model_iter_n_children(model("insight-search-results"), NULL) == count;
}

/* EN: Accept the standard native search dialog from its nested event loop.
 * RU: Подтверждает стандартный встроенный диалог из вложенного цикла событий. */
static gboolean accept_search(gpointer data)
{
    (void)data;
    GList *windows=gtk_window_list_toplevels();
    GtkWidget *dialog=NULL;
    for(GList *p=windows;p;p=p->next)
        if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-search-project-dialog")) dialog=p->data;
    g_list_free(windows);
    if(!dialog) return G_SOURCE_CONTINUE;
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(dialog,"insight-search-command-query"))),==,"needle");
    gtk_combo_box_set_active(GTK_COMBO_BOX(named(dialog,"insight-search-command-scope")),0);
    gtk_entry_set_text(GTK_ENTRY(named(dialog,"insight-search-command-mask")),"*.txt");
    gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* EN: Advance GUI scenarios; data is unused. Stop after all assertions pass.
 * RU: Продвигает сценарии интерфейса; data не используется. Завершает после проверок. */
static gboolean tick(gpointer data)
{
    (void)data;
    if (!document_get_current()) return G_SOURCE_CONTINUE;
    if (!window) window = gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));
    if (!phase)
    {
        cpp = document_open_file(cpp_path, FALSE, NULL, NULL);
        g_assert_nonnull(cpp);
        g_assert_true(cpp->file_type == filetypes_lookup_by_name("C++"));
        gtk_entry_set_text(GTK_ENTRY(named(window, "insight-file-query")), "notes.txt");
        phase++;
        return G_SOURCE_CONTINUE;
    }
    if (phase == 1)
    {
        if (gtk_tree_model_iter_n_children(model("insight-project-files"), NULL) != 1)
            return G_SOURCE_CONTINUE;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window, "insight-search-keywords")), FALSE);
        search("needle", 0);
        phase++;
    }
    else if (phase == 2)
    {
        if (!complete(3)) return G_SOURCE_CONTINUE;
        gtk_entry_set_text(GTK_ENTRY(named(window, "File masks: *.c;*.h;*.py")), "*.txt");
        search("needle", 0);
        phase++;
    }
    else if (phase == 3)
    {
        if (!complete(1)) return G_SOURCE_CONTINUE;
        GtkWidget *view = named(window, "insight-search-results");
        GtkTreePath *first = gtk_tree_path_new_from_indices(0, -1);
        gtk_tree_view_row_activated(GTK_TREE_VIEW(view), first, gtk_tree_view_get_column(GTK_TREE_VIEW(view), 0));
        gtk_tree_path_free(first);
        g_assert_cmpstr(document_get_current()->file_name, ==, text_path);
        gtk_entry_set_text(GTK_ENTRY(named(window, "File masks: *.c;*.h;*.py")), "");
        search("needle", 1);
        phase++;
    }
    else if (phase == 4)
    {
        if (!complete(2)) return G_SOURCE_CONTINUE;
        search("needle", 2);
        phase++;
    }
    else if (phase == 5)
    {
        if (!complete(1)) return G_SOURCE_CONTINUE;
        keybindings_send_command(GEANY_KEY_GROUP_SEARCH, GEANY_KEYS_SEARCH_FIND);
        GtkWidget *query = named(window, "insight-file-search-query");
        gtk_entry_set_text(GTK_ENTRY(query), "needle");
        g_signal_emit_by_name(query, "activate");
        g_assert_cmpint(sci_get_selection_start(document_get_current()->editor->sci), ==, 0);
        g_assert_cmpint(sci_get_selection_end(document_get_current()->editor->sci), ==, 6);
        document_open_file(cpp_path, FALSE, NULL, NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window, "insight-relations-follow")), FALSE);
        gtk_combo_box_set_active(GTK_COMBO_BOX(named(window, "insight-relation-mode")), 0);
        query = named(window, "insight-relation-query");
        gtk_entry_set_text(GTK_ENTRY(query), "needle");
        g_signal_emit_by_name(query, "activate");
        phase++;
    }
    else if (phase == 6)
    {
        const gchar *status = gtk_label_get_text(GTK_LABEL(named(window, "insight-relation-status")));
        if (!strstr(status, "files scanned") ||
            gtk_tree_model_iter_n_children(model("insight-relations"), NULL) != 1)
            return G_SOURCE_CONTINUE;
        sci_set_current_position(cpp->editor->sci, 5, FALSE);
        keybindings_send_command(GEANY_KEY_GROUP_SEARCH, GEANY_KEYS_SEARCH_FINDUSAGE);
        phase++;
    }
    else if(phase==7)
    {
        if(!complete(3)) return G_SOURCE_CONTINUE;
        keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_SEARCH_FINDDOCUMENTUSAGE);
        phase++;
    }
    else if(phase==8)
    {
        if(!complete(1)) return G_SOURCE_CONTINUE;
        scintilla_send_message(cpp->editor->sci,SCI_SETSEL,4,10);
        phase++;
        g_timeout_add(50,accept_search,NULL);
        keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_SEARCH_FINDINFILES);
    }
    else if(phase==9)
    {
        if(!complete(1)) return G_SOURCE_CONTINUE;
        gchar *hdl=g_build_filename(directory,"sources","defines.v",NULL);
        GeanyDocument *doc=document_open_file(hdl,FALSE,NULL,NULL);
        g_assert_nonnull(doc);
        g_free(hdl);
        scintilla_send_message(doc->editor->sci,SCI_SETSEL,8,19);
        keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_SEARCH_FINDUSAGE);
        phase++;
    }
    else
    {
        if(!complete(4)) return G_SOURCE_CONTINUE;
        const gchar *capture = g_getenv("INSIGHT_SEARCH_COMMANDS_CAPTURE");
        if (capture)
        {
            gtk_widget_show(named(window, "insight-project-search-panel"));
            gtk_window_resize(GTK_WINDOW(window), 1280, 900);
            GdkWindow *native = gtk_widget_get_window(window);
            GdkPixbuf *image = gdk_pixbuf_get_from_window(native, 0, 0,
                gdk_window_get_width(native), gdk_window_get_height(native));
            g_assert_nonnull(image);
            g_assert_true(gdk_pixbuf_save(image, capture, "jpeg", NULL, "quality", "88", NULL));
            g_object_unref(image);
        }
        g_assert_false(cpp->changed);
        g_print("Search commands passed: mixed-language filenames/project text, masks, disk/buffer results, open/current scopes, Ctrl+F, Enter, lexical usages, standard Find Usages/current-document commands, native Find in Files with masks and closed Verilog/macro references and result navigation.\n");
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Fail a stalled scenario after the deadline; data is unused.
 * RU: Завершает зависший сценарий по таймауту; data не используется. */
static gboolean timeout(gpointer data)
{
    (void)data;
    g_error("Search commands timeout in phase %u", phase);
    return G_SOURCE_REMOVE;
}

/* EN: Create an isolated project/profile and run the production editor; return 77
 * without a GUI environment, otherwise the editor exit status.
 * RU: Создаёт отдельный проект/профиль и запускает редактор; возвращает 77 без
 * графической среды, иначе код завершения редактора. */
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
    directory = g_dir_make_tmp("search-commands-XXXXXX", NULL);
    profile = g_build_filename(directory, "profile", NULL);
    gchar *language = g_build_filename(profile, "languages", "Search C++", NULL);
    g_assert_cmpint(g_mkdir_with_parents(language, 0700), ==, 0);
    gchar *config = g_build_filename(language, "language.conf", NULL);
    g_assert_true(g_file_set_contents(config,
        "[language]\nname=Search C++\nparser=C++\nextensions=cpp\n", -1, NULL));
    g_free(config);
    g_free(language);
    config = g_build_filename(profile, "geany.conf", NULL);
    g_assert_true(g_file_set_contents(config, "[geany]\nconfirm_exit=false\n[tools]\ngrep_cmd=/nonexistent/forbidden-search-command\n", -1, NULL));
    g_free(config);
    gchar *sources = g_build_filename(directory, "sources", NULL);
    g_assert_cmpint(g_mkdir_with_parents(sources, 0700), ==, 0);
    cpp_path = g_build_filename(sources, "main.cpp", NULL);
    text_path = g_build_filename(sources, "notes.txt", NULL);
    gchar *python = g_build_filename(sources, "worker.py", NULL);
    g_assert_true(g_file_set_contents(cpp_path, "int needle;\n", -1, NULL));
    g_assert_true(g_file_set_contents(text_path, "needle", -1, NULL));
    g_assert_true(g_file_set_contents(python, "needle = 1\n", -1, NULL));
    gchar *hdl=g_build_filename(sources,"defines.v",NULL);
    g_assert_true(g_file_set_contents(hdl,"`define CPU_R_START 8'hb9\n",-1,NULL));
    g_free(hdl);
    gchar *nested=g_build_filename(sources,"nested",NULL);
    g_assert_cmpint(g_mkdir_with_parents(nested,0700),==,0);
    const gchar *extensions[]={"v","sv","inc"};
    for(guint i=0;i<G_N_ELEMENTS(extensions);i++) {
        gchar *name=g_strdup_printf("use.%s",extensions[i]);
        hdl=g_build_filename(nested,name,NULL);
        g_assert_true(g_file_set_contents(hdl,"assign result = `CPU_R_START;\n",-1,NULL));
        g_free(hdl);g_free(name);
    }
    g_free(nested);
    g_free(python);
    g_free(sources);
    project = g_build_filename(directory, "search.geany", NULL);
    g_assert_true(g_file_set_contents(project,
        "[project]\nname=Search regression\nbase_path=sources\n", -1, NULL));
    gchar *argv[] = {"search-commands", "--new-instance", "--no-session", "--config", profile, project, NULL};
    g_timeout_add(200, tick, NULL);
    g_timeout_add_seconds(35, timeout, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    return main_lib(6, argv);
}
