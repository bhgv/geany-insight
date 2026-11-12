#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Regress nested dynamic GDB pretty-printers using production GTK models without ptrace.
# RU: Проверяет вложенные динамические pretty-printer GDB на производственных моделях GTK без ptrace.
import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import DECLARATIONS, function
from native_debugger_values_test import EXTRA, STATE, STUBS


HARNESS = r'''
/* EN: Check expansion hints or seven real GDB replies in argv; argc selects the scenario.
 * RU: Проверяет признаки раскрытия либо семь ответов GDB в argv; argc выбирает сценарий. */
int main(int argc, char **argv)
{
    NativeDebug session = {0};
    session.value_rows = gtk_tree_store_new(8, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_UINT,
        G_TYPE_UINT, G_TYPE_UINT);
    if (argc == 2 && !strcmp(argv[1], "--hints"))
    {
        const char *records[] = {
            "{name=\"scalar\",numchild=\"0\"}",
            "{name=\"nested\",numchild=\"0\",dynamic=\"1\"}",
            "{name=\"empty\",numchild=\"0\",dynamic=\"1\",has_more=\"0\"}",
            "{name=\"root\",numchild=\"0\",dynamic=\"1\",has_more=\"1\"}",
            "{name=\"static\",numchild=\"2\"}",
            "{name=\"plain\",numchild=\"0\",dynamic=\"0\"}"
        };
        const int expected[] = {0, 1, 0, 1, 1, 0};
        for (guint i = 0; i < G_N_ELEMENTS(records); i++)
        {
            native_value_row(&session, NULL, records[i], NULL);
            GtkTreeIter row;
            assert(gtk_tree_model_iter_nth_child(session.value_rows, &row, NULL, i));
            assert(gtk_tree_model_iter_n_children(session.value_rows, &row) == expected[i]);
        }
    }
    else
    {
        assert(argc == 8);
        native_value_row(&session, NULL, argv[1], "sample");
        GtkTreeIter root, nested, scalar, more;
        assert(gtk_tree_model_iter_children(session.value_rows, &root, NULL));
        assert(gtk_tree_model_iter_n_children(session.value_rows, &root) == 1);
        native_value_children(&session, &root, argv[2]);
        assert(gtk_tree_model_iter_n_children(session.value_rows, &root) == 129);
        assert(gtk_tree_model_iter_children(session.value_rows, &nested, &root));
        assert(gtk_tree_model_iter_n_children(session.value_rows, &nested) == 1);
        native_value_children(&session, &nested, argv[5]);
        assert(gtk_tree_model_iter_children(session.value_rows, &scalar, &nested));
        char *value = NULL;
        gtk_tree_model_get(session.value_rows, &scalar, 2, &value, -1);
        assert(!strcmp(value, "7"));
        g_free(value);
        GtkTreeIter nested_empty;
        assert(gtk_tree_model_iter_nth_child(session.value_rows, &nested_empty, &root, 1));
        assert(gtk_tree_model_iter_n_children(session.value_rows, &nested_empty) == 1);
        native_value_children(&session, &nested_empty, argv[7]);
        assert(gtk_tree_model_iter_n_children(session.value_rows, &nested_empty) == 0);
        assert(gtk_tree_model_iter_nth_child(session.value_rows, &more, &root, 128));
        guint mode = 0, offset = 0;
        gtk_tree_model_get(session.value_rows, &more, 6, &offset, 7, &mode, -1);
        assert(mode == NATIVE_VALUE_MORE && offset == 128);
        session.value_offset = offset;
        native_value_children(&session, &root, argv[3]);
        assert(gtk_tree_model_iter_n_children(session.value_rows, &root) == 257);
        session.value_offset = 256;
        native_value_children(&session, &root, argv[4]);
        assert(gtk_tree_model_iter_n_children(session.value_rows, &root) == 260);
        native_value_row(&session, NULL, argv[6], "empty");
        GtkTreeIter empty;
        assert(gtk_tree_model_iter_nth_child(session.value_rows, &empty, NULL, 1));
        assert(gtk_tree_model_iter_n_children(session.value_rows, &empty) == 0);
    }
    g_object_unref(session.value_rows);
    return 0;
}
'''

PRINTER = '''# EN: Provide lazy fixture printers without reading inferior memory.
# RU: Предоставляет отложенные printer фикстуры без чтения памяти процесса.
import gdb

# EN: Print value with lazy children; value is a Page, Leaf or Empty fixture object.
# RU: Показывает value с отложенными членами; value — объект фикстуры Page, Leaf либо Empty.
class Printer:
    # EN: Retain value for subsequent GDB callbacks; returns no value.
    # RU: Сохраняет value для последующих вызовов GDB; ничего не возвращает.
    def __init__(self, value):
        self.value = value

    # EN: Return a stable summary without reading inferior memory.
    # RU: Возвращает постоянную подпись без чтения памяти процесса.
    def to_string(self):
        return 'lazy fixture'

    # EN: Yield named children of self.value without starting an inferior.
    # RU: Выдаёт именованные члены self.value без запуска отлаживаемого процесса.
    def children(self):
        if str(self.value.type) == 'Page':
            for index in range(260):
                yield str(index), self.value['empty' if index == 1 else 'leaf']
        elif str(self.value.type) == 'Leaf':
            yield 'value', gdb.Value(7)

# EN: Return a printer for value's fixture type, otherwise None; register no other types.
# RU: Возвращает printer для типа фикстуры value, иначе None; другие типы не затрагивает.
def lookup(value):
    if str(value.type) in ('Page', 'Leaf', 'Empty'):
        return Printer(value)
    return None

gdb.pretty_printers.append(lookup)
'''

TARGET = '''// EN: Expose nested C++ types and symbols for GDB without running a process.
// RU: Предоставляет вложенные типы C++ и символы GDB без запуска процесса.
struct Leaf { int value; };
struct Empty {};
struct Page { Leaf leaf; Empty empty; };
Page sample;
Empty empty;

// EN: Return success without accessing the fixture globals; there are no parameters.
// RU: Возвращает успех без обращения к глобальным данным фикстуры; параметров нет.
int main()
{
    return 0;
}
'''


class NativePrettyValues(unittest.TestCase):
    # EN: Compile shared production functions for self in folder; return the model executable or skip missing tools.
    # RU: Компилирует производственные функции для self в folder; возвращает программу либо пропускает при отсутствии средств.
    def compile_model(self, folder):
        libraries = [ctypes.util.find_library(name) for name in ('gtk-3', 'gobject-2.0', 'glib-2.0')]
        if not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs cc and GTK runtime libraries.')
        declarations = DECLARATIONS.replace('typedef struct {\n    GtkTreeStore *all_stack_rows;', STATE)
        position = declarations.index('typedef struct {\n    GtkTreeStore *value_rows;')
        declarations = declarations[:position] + EXTRA + declarations[position:] + STUBS
        helpers = [function('src/insight-debuggers/gdb-context.inc', name)
                   for name in ('native_debug_field', 'native_next_tuple')]
        helpers += [function('src/insight-debuggers/gdb-values-model.inc', name)
                    for name in ('native_value_row', 'native_value_children')]
        source = folder / 'pretty-model.c'
        executable = folder / 'pretty-model'
        source.write_text(declarations + '\n'.join(helpers) + HARNESS)
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        str(source), '-o', str(executable),
                        *['-l:' + name for name in libraries]], check=True)
        return executable

    # EN: Check self's static, dynamic, unknown and explicitly empty expansion hints without GDB.
    # RU: Проверяет статические, динамические, неизвестные и явно пустые признаки раскрытия self без GDB.
    def test_expansion_hints(self):
        with tempfile.TemporaryDirectory() as directory:
            model = self.compile_model(Path(directory))
            subprocess.run([str(model), '--hints'], check=True)

    # EN: Replay real GDB Python-printer replies through self's GTK model, including nested expansion and three pages.
    # RU: Передаёт ответы настоящих Python-printer GDB в модель GTK self, включая вложенное раскрытие и три страницы.
    def test_real_nested_printers(self):
        gdb = os.environ.get('GDB') or shutil.which('gdb')
        if not gdb or not shutil.which('c++'):
            self.skipTest('Needs GDB with Python and a C++ compiler.')
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            model = self.compile_model(folder)
            source = folder / 'pretty.cpp'
            source.write_text(TARGET)
            target = folder / 'pretty'
            subprocess.run(['c++', '-g', str(source), '-o', str(target)], check=True)
            printer = folder / 'printer.py'
            printer.write_text(PRINTER)
            commands = ['-enable-pretty-printing',
                        f'-interpreter-exec console "source {printer.as_posix()}"',
                        '-var-create page * sample',
                        '-var-list-children --all-values page 0 128',
                        '-var-list-children --all-values page 128 256',
                        '-var-list-children --all-values page 256 384',
                        '-var-list-children --all-values page.0 0 128',
                        '-var-create empty_value * empty',
                        '-var-list-children --all-values page.1 0 128', '-gdb-exit']
            result = subprocess.run([gdb, '--nx', '--quiet', '--interpreter=mi2', str(target)],
                                    input='\n'.join(commands) + '\n', capture_output=True,
                                    text=True, check=True, timeout=15)
            if 'Python scripting is not supported' in result.stdout:
                self.skipTest('GDB was built without Python scripting.')
            self.assertNotIn('^error', result.stdout)
            records = [line for line in result.stdout.splitlines() if line.startswith('^done')][2:]
            self.assertEqual(len(records), 7, result.stdout)
            self.assertIn('dynamic="1"', records[1])
            subprocess.run([str(model), *records], check=True)


if __name__ == '__main__':
    unittest.main()
