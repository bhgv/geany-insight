#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Verify production exception catchpoint commands, GTK rows and real C++ exception stops.
# RU: Проверяет производственные команды исключений, строки GTK и остановки на настоящих исключениях C++.
import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_controls_test import controls_source
from native_debugger_models_test import ROOT, function

EXTRA = r'''
extern int g_utf8_validate(const char *, gssize, const char **);
extern long g_utf8_strlen(const char *, gssize);
'''

MAIN = r'''
/* EN: Emit production MI commands or validate the real GDB breakpoint snapshot in argv[1].
 * RU: Выводит производственные команды MI либо проверяет настоящий снимок точек GDB из argv[1]. */
int main(int argc, char **argv)
{
    if (argc == 1)
    {
        assert(!native_catch_command(3, ""));
        assert(!native_catch_command(0, "bad\nfilter"));
        assert(!native_catch_command(0, "bad\rfilter"));
        assert(!native_catch_command(0, "\xff"));
        char oversized[1026];
        memset(oversized, 'a', sizeof oversized - 1);
        oversized[sizeof oversized - 1] = '\0';
        assert(!native_catch_command(0, oversized));
        const char *filters[] = {"std::runtime_error", "", "std::.*"};
        for (unsigned i = 0; i < 3; i++)
        {
            char *command = native_catch_command(i, filters[i]);
            assert(command);
            puts(command);
            g_free(command);
        }
        return 0;
    }
    assert(argc == 2);
    NativeDebug session = {0};
    session.point_rows = gtk_list_store_new(10, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    session.editor_points = g_hash_table_new(g_str_hash, g_str_equal);
    session.point_markers = g_ptr_array_new();
    native_points_snapshot(&session, argv[1]);
    assert(gtk_tree_model_iter_n_children(session.point_rows, NULL) == 3);
    GtkTreeIter row;
    unsigned index = 0;
    for (int valid = gtk_tree_model_get_iter_first(session.point_rows, &row); valid;
         valid = gtk_tree_model_iter_next(session.point_rows, &row), index++)
    {
        char *caption = NULL, *enabled = NULL, *condition = NULL, *ignore = NULL, *kind = NULL;
        gtk_tree_model_get(session.point_rows, &row, 0, &caption, 2, &enabled, 3, &condition,
                           5, &ignore, 8, &kind, -1);
        assert(!strcmp(kind, "catchpoint"));
        assert(!strcmp(enabled, index == 1 ? "n" : "y"));
        if (index == 0)
        {
            assert(!strcmp(caption, "C++ throw [std::runtime_error]"));
            assert(!strcmp(condition, "1") && !strcmp(ignore, "2"));
        }
        else
            assert(!strcmp(caption, index == 1 ? "C++ catch" : "C++ rethrow [std::.*]"));
        g_free(caption);
        g_free(enabled);
        g_free(condition);
        g_free(ignore);
        g_free(kind);
    }
    assert(marker_count == 0);
    char *detail = native_catch_stop_detail(&session, "*stopped,bkptno=\"1\"");
    assert(detail && !strcmp(detail, "C++ throw [std::runtime_error] (#1)"));
    g_free(detail);
    assert(!native_catch_stop_detail(&session, "*stopped,bkptno=\"999\""));
    g_object_unref(session.point_rows);
    g_hash_table_unref(session.editor_points);
    g_ptr_array_unref(session.point_markers);
    return 0;
}
'''

CPP = r'''
#include <stdexcept>
/* EN: Throw, catch, rethrow and catch again without terminating the process.
 * RU: Выбрасывает исключение, перехватывает, повторно выбрасывает и перехватывает без завершения процесса. */
int main()
{
    try {
        try { throw std::runtime_error("fixture"); }
        catch (...) { throw; }
    }
    catch (...) { return 0; }
}
'''


class NativeCatchpoints(unittest.TestCase):
    # EN: Validate the exact production commands and table against real GDB without launching a traced process.
    # RU: Проверяет настоящие производственные команды и таблицу с GDB без запуска трассируемого процесса.
    def test_commands_and_confirmed_rows(self):
        gdb = os.environ.get('GDB', shutil.which('gdb'))
        libraries = [ctypes.util.find_library(name) for name in ('gtk-3', 'gobject-2.0', 'glib-2.0')]
        if not gdb or not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs GDB, cc and GTK runtime libraries.')
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            root = Path(temporary)
            source = controls_source('') + EXTRA
            source += function('src/insight-debuggers/gdb-catchpoints-model.inc', 'native_catch_command')
            source += function('src/insight-debuggers/gdb-catchpoints-ui.inc', 'native_catch_stop_detail') + MAIN
            (root / 'catchpoints.c').write_text(source)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(root / 'catchpoints.c'), '-o', str(root / 'catchpoints'),
                            *['-l:' + name for name in libraries]], check=True)
            commands = subprocess.check_output([str(root / 'catchpoints')], text=True)
            commands += ('-break-condition 1 "1"\n-break-after 1 2\n-break-disable 2\n'
                         '-catch-throw -r "["\n-break-list\n-gdb-exit\n')
            result = subprocess.run([gdb, '--nx', '--quiet', '--interpreter=mi2'], input=commands,
                                    text=True, capture_output=True, check=True, timeout=15)
            self.assertEqual(result.stdout.count('^error'), 1, result.stdout)
            snapshot = next(line for line in result.stdout.splitlines() if line.startswith('^done,BreakpointTable='))
            subprocess.run([str(root / 'catchpoints'), snapshot], check=True)

    # EN: Check actual throw/catch/rethrow stops and a normal exit; skip only when the sandbox forbids ptrace.
    # RU: Проверяет остановки throw/catch/rethrow и нормальный выход; пропускает только при запрете ptrace средой.
    def test_real_exception_stops(self):
        gdb = os.environ.get('GDB', shutil.which('gdb'))
        if not gdb or not shutil.which('c++'):
            self.skipTest('Needs GDB and C++.')
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            root = Path(temporary)
            (root / 'exceptions.cpp').write_text(CPP)
            subprocess.run(['c++', '-g', '-O0', str(root / 'exceptions.cpp'), '-o', str(root / 'program')], check=True)
            commands = ['set startup-with-shell off', 'set pagination off', 'catch throw', 'catch catch',
                        'catch rethrow', 'run', 'bt', 'continue', 'bt', 'continue', 'bt',
                        'continue', 'bt', 'continue', 'quit']
            arguments = [gdb, '--nx', '--quiet', '--batch', str(root / 'program')]
            for command in commands:
                arguments.extend(['-ex', command])
            env = dict(os.environ, LC_ALL='C.UTF-8')
            result = subprocess.run(arguments, text=True, capture_output=True, timeout=25, env=env)
            output = result.stdout + result.stderr
            if 'ptrace: Operation not permitted' in output:
                self.skipTest('The local sandbox forbids ptrace.')
            self.assertEqual(result.returncode, 0, output)
            for text in ('exception thrown', 'exception caught', 'exception rethrown', 'exited normally'):
                self.assertIn(text, output)
            self.assertIn('main', output)


if __name__ == '__main__':
    unittest.main()
