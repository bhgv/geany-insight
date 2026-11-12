#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Regress value-tree retries, stale replies and request limits with real GTK row references.
# RU: Проверяет повторы дерева значений, устаревшие ответы и пределы запросов с настоящими ссылками строк GTK.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import DECLARATIONS, function
from native_debugger_values_test import EXTRA, STATE, STUBS


REQUESTS = r'''
typedef void *gpointer;
typedef void GtkTreeViewColumn;
extern GtkTreePath *gtk_tree_model_get_path(GtkTreeModel *, GtkTreeIter *);
extern void gtk_tree_path_free(GtkTreePath *);
extern int gtk_tree_path_get_depth(GtkTreePath *);
extern int gtk_tree_model_iter_parent(GtkTreeModel *, GtkTreeIter *, GtkTreeIter *);
extern GtkTreeRowReference *gtk_tree_row_reference_new(GtkTreeModel *, GtkTreePath *);
static char *sent_command;
static guint sent_count;
/* EN: Record command for the model fixture; window is unused and no GDB process is started.
 * RU: Запоминает command фикстуры модели; window не используется, процесс GDB не запускается. */
static void native_debug_send(GtkWidget *window, const char *command)
{
    (void)window;
    g_free(sent_command);
    sent_command = g_strdup(command);
    sent_count++;
}
/* EN: Use view as the fixture model directly, without constructing a display-dependent widget.
 * RU: Использует view непосредственно как модель фикстуры без создания виджета, требующего дисплея. */
static GtkTreeModel *fixture_view_model(GtkTreeView *view)
{
    return (GtkTreeModel *)view;
}
#define gtk_tree_view_get_model fixture_view_model
/* EN: Reject unexpected scalar editing in this request fixture; all arguments describe the absent editor.
 * RU: Запрещает неожиданное редактирование скаляра в фикстуре запросов; все параметры описывают отсутствующий редактор. */
static void native_assign_dialog(NativeDebug *session, const char *expression,
    const char *value, const char *object)
{
    (void)session;
    (void)expression;
    (void)value;
    (void)object;
    assert(FALSE);
}
'''

HARNESS = r'''
/* EN: Assert loaded state and child count for parent in session; abort on an unexpected model change.
 * RU: Проверяет загрузку и число членов parent в session; при неожиданном изменении модели прерывает тест. */
static void check_parent(NativeDebug *session, GtkTreeIter *parent, gboolean loaded, guint count)
{
    gboolean actual = FALSE;
    gtk_tree_model_get(session->value_rows, parent, 4, &actual, -1);
    assert(actual == loaded);
    assert(gtk_tree_model_iter_n_children(session->value_rows, parent) == (int)count);
}

/* EN: Activate parent's last child in session through the production row callback; assert that it exists.
 * RU: Активирует последний член parent в session производственным обработчиком строки; проверяет его наличие. */
static void activate_last(NativeDebug *session, GtkTreeIter *parent)
{
    GtkTreeIter child;
    int count = gtk_tree_model_iter_n_children(session->value_rows, parent);
    assert(count > 0);
    assert(gtk_tree_model_iter_nth_child(session->value_rows, &child, parent, count - 1));
    GtkTreePath *path = gtk_tree_model_get_path(session->value_rows, &child);
    native_value_activated((GtkTreeView *)session->value_rows, path, NULL, session);
    gtk_tree_path_free(path);
}

/* EN: Run the retry, stale, bounds, busy or escaping scenario from argv[1]; argc must be two.
 * RU: Запускает сценарий retry, stale, bounds, busy либо escaping из argv[1]; argc должен равняться двум. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeDebug session = {0};
    session.value_rows = gtk_tree_store_new(8, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_UINT,
        G_TYPE_UINT, G_TYPE_UINT);
    session.stopped = session.ready = TRUE;
    native_value_row(&session, NULL, "{name=\"root\",numchild=\"5\"}", NULL);
    GtkTreeIter root;
    assert(gtk_tree_model_iter_children(session.value_rows, &root, NULL));
    const char *first = "^done,numchild=\"2\",children=["
        "child={name=\"root.0\",exp=\"0\",numchild=\"0\",value=\"10\"},"
        "child={name=\"root.1\",exp=\"1\",numchild=\"0\",value=\"11\"}],has_more=\"1\"";
    if (!strcmp(argv[1], "retry"))
    {
        native_value_expand(&session, &root, 0);
        assert(session.value_pending_row && sent_count == 1);
        native_values_reply(&session, "^error,msg=\"temporarily unavailable\"");
        check_parent(&session, &root, FALSE, 1);
        assert(!session.value_pending_row);
        activate_last(&session, &root);
        assert(sent_count == 2);
        native_values_reply(&session, first);
        check_parent(&session, &root, TRUE, 3);
        for (guint attempt = 0; attempt < 3; attempt++)
        {
            activate_last(&session, &root);
            native_values_reply(&session, "^error,msg=\"page read failed\"");
            guint before = sent_count;
            native_value_expanded((GtkTreeView *)session.value_rows, &root, NULL, &session);
            if (sent_count != before)
                native_values_reply(&session, first);
            check_parent(&session, &root, TRUE, 3);
            assert(sent_count == before);
        }
        activate_last(&session, &root);
        assert(strstr(sent_command, " 2 130"));
        native_values_reply(&session, "^done,children=["
            "child={name=\"root.2\",exp=\"2\"},child={name=\"root.3\",exp=\"3\"}],has_more=\"1\"");
        check_parent(&session, &root, TRUE, 5);
        native_value_expand(&session, &root, 4);
        native_values_reply(&session,
            "^done,children=[child={name=\"root.4\",exp=\"4\"}],has_more=\"0\"");
        check_parent(&session, &root, TRUE, 5);
        for (guint index = 0; index < 5; index++)
        {
            GtkTreeIter child;
            char *name = NULL;
            assert(gtk_tree_model_iter_nth_child(session.value_rows, &child, &root, index));
            gtk_tree_model_get(session.value_rows, &child, 3, &name, -1);
            char *expected = g_strdup_printf("root.%u", index);
            assert(!strcmp(name, expected));
            g_free(expected);
            g_free(name);
        }
    }
    else if (!strcmp(argv[1], "stale"))
    {
        native_value_expand(&session, &root, 0);
        gtk_tree_store_remove(session.value_rows, &root);
        native_values_reply(&session, first);
        assert(!session.value_pending_row);
        assert(gtk_tree_model_iter_n_children(session.value_rows, NULL) == 0);
        native_value_row(&session, NULL, "{name=\"replacement\",numchild=\"5\"}", NULL);
        assert(gtk_tree_model_iter_children(session.value_rows, &root, NULL));
        native_value_expand(&session, &root, 0);
        session.stopped = FALSE;
        native_values_reply(&session, first);
        check_parent(&session, &root, FALSE, 1);
        session.stopped = TRUE;
        native_value_expand(&session, &root, 0);
        native_values_reset(&session);
        assert(!session.value_pending_row);
        native_values_reply(&session, first);
        assert(gtk_tree_model_iter_n_children(session.value_rows, NULL) == 0);
        assert(session.value_request == NATIVE_VALUE_NONE);
        session.value_request = NATIVE_VALUE_CREATE;
        session.value_pending_generation = session.value_generation;
        native_values_reset(&session);
        native_values_reply(&session, "^done,name=\"obsolete\",numchild=\"0\"");
        char *discarded = g_queue_pop_head(&session.value_cleanup);
        assert(discarded && !strcmp(discarded, "obsolete"));
        g_free(discarded);
        assert(gtk_tree_model_iter_n_children(session.value_rows, NULL) == 0);
    }
    else if (!strcmp(argv[1], "bounds"))
    {
        session.value_nodes = 4096;
        native_value_expand(&session, &root, 0);
        assert(!sent_count && !session.value_pending_row);
        session.value_nodes = 1;
        GtkTreeIter parent = root;
        for (guint depth = 2; depth <= 65; depth++)
        {
            GtkTreeIter child;
            gtk_tree_store_insert_with_values(session.value_rows, &child, &parent, -1,
                3, "deep", 5, 1u, -1);
            parent = child;
            if (depth == 64)
            {
                native_value_expand(&session, &parent, 0);
                assert(sent_count == 1 && session.value_pending_row);
                native_values_reply(&session, "^error,msg=\"not loaded\"");
            }
        }
        native_value_expand(&session, &parent, 0);
        assert(sent_count == 1 && !session.value_pending_row);
    }
    else if (!strcmp(argv[1], "escaping"))
    {
        gtk_tree_store_set(session.value_rows, &root, 3, "root.\"quoted\"", -1);
        native_value_expand(&session, &root, 128);
        assert(!strcmp(sent_command,
            "-var-list-children --all-values \"root.\\\"quoted\\\"\" 128 256"));
        native_values_reply(&session, "^error,msg=\"fixture complete\"");
    }
    else if (!strcmp(argv[1], "busy"))
    {
        gboolean *flags[] = {&session.stopped, &session.ready, &session.pending, &session.writing};
        for (guint index = 0; index < G_N_ELEMENTS(flags); index++)
        {
            gboolean original = *flags[index];
            *flags[index] = !original;
            native_value_expand(&session, &root, 0);
            assert(!sent_count && !session.value_pending_row);
            assert(session.value_request == NATIVE_VALUE_NONE);
            *flags[index] = original;
        }
        native_value_expand(&session, &root, 0);
        assert(sent_count == 1 && session.value_pending_row);
        native_values_reply(&session, "^error,msg=\"fixture complete\"");
    }
    else
        assert(FALSE);
    g_free(session.value_pending_name);
    g_free(sent_command);
    g_object_unref(session.value_rows);
    return 0;
}
'''


class NativeValueRequests(unittest.TestCase):
    # EN: Build production request/reply code for self and run scenario; skip only missing compiler/runtime libraries.
    # RU: Собирает производственный код запросов/ответов для self и запускает scenario; пропускает лишь при отсутствии средств.
    def exercise(self, scenario):
        libraries = [ctypes.util.find_library(name) for name in ('gtk-3', 'gobject-2.0', 'glib-2.0')]
        if not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs cc and GTK runtime libraries.')
        start = DECLARATIONS.index('/* EN/RU: No view')
        end = DECLARATIONS.index('typedef struct {\n    GtkTreeStore *all_stack_rows;', start)
        declarations = DECLARATIONS[:start] + DECLARATIONS[end:]
        state = STATE.replace('    GtkTreeStore *all_stack_rows;',
                              '    char *value_pending_name;\n'
                              '    GtkWidget *window;\n'
                              '    gboolean ready, pending, writing;\n'
                              '    GtkTreeStore *all_stack_rows;')
        declarations = declarations.replace('typedef struct {\n    GtkTreeStore *all_stack_rows;', state)
        position = declarations.index('typedef struct {\n    GtkTreeStore *value_rows;')
        declarations = declarations[:position] + EXTRA + declarations[position:] + STUBS + REQUESTS
        helpers = [function('src/insight-debuggers/gdb-context.inc', name)
                   for name in ('native_debug_field', 'native_next_tuple')]
        helpers += [function('src/insight-debuggers/gdb-values-model.inc', name)
                    for name in ('native_values_reset', 'native_value_row',
                                 'native_value_children', 'native_values_reply')]
        helpers += [function('src/insight-debuggers/gdb-values-ui.inc', name)
                    for name in ('native_value_expand', 'native_value_expanded', 'native_value_activated')]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'requests.c'
            source.write_text(declarations + '\n'.join(helpers) + HARNESS)
            executable = folder / 'requests'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(executable),
                            *['-l:' + name for name in libraries]], check=True)
            subprocess.run([str(executable), scenario], check=True)

    # EN: Verify self's failed initial/next-page retries never duplicate already loaded members.
    # RU: Проверяет, что повторные ошибки начальной/следующих страниц self не дублируют загруженные члены.
    def test_page_retry(self):
        self.exercise('retry')

    # EN: Verify self's frame reset discards late child replies and queues obsolete roots for cleanup.
    # RU: Проверяет, что сброс кадра self отбрасывает поздние ответы членов и ставит устаревшие корни на удаление.
    def test_stale_replies(self):
        self.exercise('stale')

    # EN: Verify self's request path enforces the node cap and the depth boundary at 64.
    # RU: Проверяет предел числа узлов и границу глубины 64 в пути запросов self.
    def test_request_bounds(self):
        self.exercise('bounds')

    # EN: Verify self's generated page command quotes GDB object names and preserves its offset.
    # RU: Проверяет экранирование имён объектов GDB и сохранение смещения в команде страницы self.
    def test_object_name_escaping(self):
        self.exercise('escaping')

    # EN: Verify self's expansions wait for a paused, connected session with no pending read/write.
    # RU: Проверяет, что раскрытие self требует паузы, соединения и отсутствия ожидающих чтения/записи.
    def test_busy_session(self):
        self.exercise('busy')


if __name__ == '__main__':
    unittest.main()
