#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Check production lazy value trees against real GDB records and GTK models without ptrace.
# RU: Проверяет производственное дерево значений на ответах GDB и моделях GTK без ptrace.
import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import DECLARATIONS, function

EXTRA = r'''
typedef unsigned long guint64;
typedef void GtkWidget;
typedef void GtkTreeRowReference;
typedef struct {void *head, *tail; guint length;} GQueue;
typedef enum {NATIVE_DATA_INSPECT, NATIVE_DATA_WATCH, NATIVE_DATA_ASSIGN,
    NATIVE_DATA_WRITE, NATIVE_DATA_READ, NATIVE_DATA_ACCESS} NativeDataAction;
typedef enum {NATIVE_VALUE_NONE, NATIVE_VALUE_CREATE, NATIVE_VALUE_CHILDREN,
    NATIVE_VALUE_ASSIGN, NATIVE_VALUE_DELETE, NATIVE_VALUE_PATH} NativeValueRequest;
enum {NATIVE_VALUE_PLACEHOLDER = 1, NATIVE_VALUE_MORE = 2};
extern char *g_strescape(const char *, const char *);
extern size_t g_strlcpy(char *, const char *, size_t);
extern int g_utf8_validate(const char *, gssize, const char **);
extern int g_snprintf(char *, size_t, const char *, ...);
extern char *g_compute_checksum_for_string(int, const char *, gssize);
#define G_CHECKSUM_SHA256 2
#define TRUE 1
#define G_MAXUINT 4294967295u
#define G_TYPE_BOOLEAN 20UL
#define G_TYPE_UINT 28UL
#define GTK_LABEL(a) (a)
#define g_steal_pointer(p) __extension__ ({void *value=*(p); *(p)=NULL; value;})
extern int g_ascii_string_to_unsigned(const char *, unsigned, guint64, guint64, guint64 *, void *);
extern void g_queue_push_tail(GQueue *, void *);
extern void *g_queue_pop_head(GQueue *);
extern int gtk_tree_store_remove(GtkTreeStore *, GtkTreeIter *);
extern void gtk_tree_store_clear(GtkTreeStore *);
extern int gtk_tree_model_iter_next(GtkTreeModel *, GtkTreeIter *);
extern int gtk_tree_model_iter_nth_child(GtkTreeModel *, GtkTreeIter *, GtkTreeIter *, int);
extern int gtk_tree_model_get_iter(GtkTreeModel *, GtkTreeIter *, GtkTreePath *);
extern GtkTreePath *gtk_tree_row_reference_get_path(GtkTreeRowReference *);
extern void gtk_tree_row_reference_free(GtkTreeRowReference *);
static guint mutation_count;
/* EN: Ignore label rendering; tree storage is the real GTK implementation.
 * RU: Пропускает отрисовку label/text; хранилище дерева использует настоящую GTK. */
static void fixture_label(GtkWidget *label, const char *text)
{
    (void)label;
    (void)text;
}
#define gtk_label_set_text fixture_label
'''

STATE = '''typedef struct {
    GtkTreeStore *value_rows;
    GtkWidget *value_status;
    char *value_root, *value_pending_expression;
    guint value_nodes, value_offset, value_generation, value_pending_generation;
    gboolean stopped, value_reload;
    GQueue value_cleanup;
    GtkTreeRowReference *value_pending_row;
    NativeValueRequest value_request;
    NativeDataAction value_path_action;
    GtkTreeStore *all_stack_rows;'''

STUBS = r'''
/* EN: Count successful refresh requests for session; no live process is mutated in this fixture.
 * RU: Считает запросы обновления session; живой процесс в фикстуре не изменяется. */
static void native_data_refresh(NativeDebug *session)
{
    (void)session;
    mutation_count++;
}
/* EN: Consume the resolved expression/action for session without invoking editor controls.
 * RU: Принимает expression/action для session, не вызывая элементы редактора. */
static void native_data_use_expression(NativeDebug *session, const char *expression, NativeDataAction action)
{
    (void)session;
    (void)expression;
    (void)action;
}
'''

MAIN = r'''
/* EN: Validate command output or the five GDB tree records supplied in argv.
 * RU: Проверяет вывод команд либо пять записей дерева GDB, переданных через argv. */
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--commands"))
    {
        for (NativeDataAction action = NATIVE_DATA_WRITE; action <= NATIVE_DATA_ACCESS; action++)
        {
            char *command = native_data_watch_command("sample.count", action);
            puts(command);
            g_free(command);
        }
        assert(!native_data_watch_command("", NATIVE_DATA_WRITE));
        assert(!native_data_watch_command("sample", NATIVE_DATA_ASSIGN));
        return 0;
    }
    assert(argc == 6);
    char label[4];
    native_watch_text(label, sizeof label, "ЖЖ");
    assert(g_utf8_validate(label, -1, NULL) && !strcmp(label, "Ж"));
    char expression[1100], first_path[128], second_path[128];
    memset(expression, 'a', sizeof expression - 1);
    expression[sizeof expression - 1] = '\0';
    native_watch_path(first_path, sizeof first_path, "gdb", expression);
    expression[sizeof expression - 2] = 'b';
    native_watch_path(second_path, sizeof second_path, "gdb", expression);
    assert(strcmp(first_path, second_path));
    native_watch_path(first_path, sizeof first_path, "gdb", "marker");
    assert(!strcmp(first_path, "gdb:marker"));
    NativeDebug session = {0};
    session.value_rows = gtk_tree_store_new(8, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
    session.stopped = TRUE;
    session.value_generation = 2;
    session.value_pending_generation = 1;
    session.value_request = NATIVE_VALUE_CREATE;
    native_values_reply(&session, argv[1]);
    assert(gtk_tree_model_iter_n_children(session.value_rows, NULL) == 0);
    char *discarded = g_queue_pop_head(&session.value_cleanup);
    assert(discarded && !strcmp(discarded, "test"));
    g_free(discarded);
    session.value_pending_generation = 2;
    session.value_request = NATIVE_VALUE_CREATE;
    session.value_pending_expression = "sample";
    native_values_reply(&session, argv[1]);
    assert(session.value_root && !strcmp(session.value_root, "test"));
    GtkTreeIter root, array, first;
    assert(gtk_tree_model_iter_children(session.value_rows, &root, NULL));
    native_value_children(&session, &root, argv[2]);
    assert(gtk_tree_model_iter_n_children(session.value_rows, &root) == 2);
    assert(gtk_tree_model_iter_nth_child(session.value_rows, &array, &root, 1));
    session.value_offset = 0;
    native_value_children(&session, &array, argv[3]);
    assert(gtk_tree_model_iter_n_children(session.value_rows, &array) == 129);
    assert(gtk_tree_model_iter_children(session.value_rows, &first, &array));
    char *value = NULL;
    gtk_tree_model_get(session.value_rows, &first, 2, &value, -1);
    assert(!strcmp(value, "2"));
    g_free(value);
    session.value_offset = 128;
    native_value_children(&session, &array, argv[4]);
    assert(gtk_tree_model_iter_n_children(session.value_rows, &array) == 257);
    session.value_offset = 256;
    native_value_children(&session, &array, argv[5]);
    assert(gtk_tree_model_iter_n_children(session.value_rows, &array) == 260);
    assert(session.value_nodes == 263);
    session.value_request = NATIVE_VALUE_ASSIGN;
    native_values_reply(&session, "^error,msg=\"read only\"");
    assert(mutation_count == 0);
    session.value_request = NATIVE_VALUE_ASSIGN;
    native_values_reply(&session, "^done,value=\"13\"");
    assert(mutation_count == 1);
    char *reason = native_stop_detail("*stopped,reason=\"watchpoint-trigger\",wpt={exp=\"marker\"},value={old=\"41\",new=\"42\"}");
    assert(strstr(reason, "marker") && strstr(reason, "41") && strstr(reason, "42"));
    g_free(reason);
    session.value_nodes = 4096;
    native_value_row(&session, &array, "{name=\"test.extra\",exp=\"extra\"}", NULL);
    assert(gtk_tree_model_iter_n_children(session.value_rows, &array) == 260);
    native_values_reset(&session);
    assert(session.value_generation == 3 && session.value_nodes == 0);
    discarded = g_queue_pop_head(&session.value_cleanup);
    assert(discarded && !strcmp(discarded, "test"));
    g_free(discarded);
    g_object_unref(session.value_rows);
    return 0;
}
'''


class NativeValues(unittest.TestCase):
    # EN: Exercise variable-object lifetime, pagination and all watchpoint modes on real GDB.
    # RU: Проверяет срок жизни объектов, страницы и все режимы точек по данным на настоящем GDB.
    def test_lazy_values_and_watchpoints(self):
        gdb = os.environ.get('GDB', shutil.which('gdb'))
        libraries = [ctypes.util.find_library(n) for n in ('gtk-3', 'gobject-2.0', 'glib-2.0')]
        if not gdb or not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs GDB, cc and GTK runtime libraries.')
        declarations = DECLARATIONS.replace('typedef struct {\n    GtkTreeStore *all_stack_rows;', STATE)
        position = declarations.index('typedef struct {\n    GtkTreeStore *value_rows;')
        declarations = declarations[:position] + EXTRA + declarations[position:] + STUBS
        helpers = [function('src/insight-debuggers/gdb-context.inc', n) for n in
                   ('native_debug_field', 'native_next_tuple')]
        helpers += [function('src/insight-debuggers/gdb-watches.inc', n) for n in
                    ('native_watch_text', 'native_watch_path')]
        helpers += [function('src/insight-debuggers/gdb-data-actions.inc', n) for n in
                    ('native_data_watch_command', 'native_stop_detail')]
        helpers += [function('src/insight-debuggers/gdb-values-model.inc', n) for n in
                    ('native_values_reset', 'native_value_row', 'native_value_children', 'native_values_reply')]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'values.c').write_text(declarations + '\n'.join(helpers) + MAIN)
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(folder / 'values.c'), '-o', str(folder / 'values'),
                            *['-l:' + n for n in libraries]], check=True)
            commands = subprocess.check_output([str(folder / 'values'), '--commands'], text=True).splitlines()
            (folder / 'target.c').write_text(
                'struct Sample { int count; int values[260]; };\n'
                'struct Sample sample = {7, {2, 3, 5}};\n'
                'int main(void) { return sample.count; }\n')
            subprocess.run(['cc', '-g', str(folder / 'target.c'), '-o', str(folder / 'target')], check=True)
            setup = ['-var-create test * "sample"', '-var-list-children --all-values test 0 128',
                     '-var-list-children --all-values test.values 0 128',
                     '-var-list-children --all-values test.values 128 256',
                     '-var-list-children --all-values test.values 256 384']
            result = subprocess.run([gdb, '--nx', '--quiet', '--interpreter=mi2', str(folder / 'target')],
                                    input='\n'.join(setup + commands + ['-var-delete test', '-gdb-exit']) + '\n',
                                    capture_output=True, text=True, check=True, timeout=15)
            self.assertNotIn('^error', result.stdout)
            self.assertIn('hw-rwpt=', result.stdout)
            self.assertIn('hw-awpt=', result.stdout)
            records = [line for line in result.stdout.splitlines() if line.startswith('^done')][:5]
            subprocess.run([str(folder / 'values'), *records], check=True)


if __name__ == '__main__':
    unittest.main()
