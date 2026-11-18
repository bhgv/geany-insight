#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise production fresh-GDB recovery guards and failure ownership without a display or GDB.
# RU: Проверяет ограничения открытия нового GDB и владение окном при ошибке без дисплея и GDB.
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function


HARNESS = r'''
#include <assert.h>
#include <string.h>
typedef int gboolean;
typedef char gchar;
typedef void GtkWidget;
typedef void GtkButton;
typedef void *gpointer;
typedef struct {
    gboolean closed, backend_failed;
    gchar *gdb_executable, *target_executable;
    GtkWidget *window;
} NativeDebug;
static NativeDebug *expected;
static gboolean spawn_ok;
static unsigned spawn_count, destroy_count;

/* EN: Model spawning gdb/target with previous; verify original identity and return spawn_ok.
 * RU: Моделирует запуск gdb/target с previous; проверяет исходные параметры и возвращает spawn_ok. */
static gboolean native_debug_show(const gchar *gdb, const gchar *target, NativeDebug *previous)
{
    assert(previous == expected);
    assert(!strcmp(gdb, "/tools/gdb") && !strcmp(target, "/project/program"));
    spawn_count++;
    return spawn_ok;
}

/* EN: Count destruction of the expected old window; the new window is owned independently.
 * RU: Считает закрытия ожидаемого старого window; новое окно имеет независимое владение. */
static void gtk_widget_destroy(GtkWidget *window)
{
    assert(window == expected->window);
    destroy_count++;
}
'''

MAIN = r'''
/* EN: Exercise failed, closed and live recovery requests; no arguments or external resources are needed.
 * RU: Проверяет запросы для аварийного, закрытого и живого сеансов; аргументы и внешние ресурсы не нужны. */
int main(void)
{
    NativeDebug session = {0};
    expected = &session;
    session.window = &session;
    session.gdb_executable = "/tools/gdb";
    session.target_executable = "/project/program";
    native_debug_reopen(NULL, &session);
    assert(!spawn_count && !destroy_count);
    session.backend_failed = 1;
    session.closed = 1;
    native_debug_reopen(NULL, &session);
    assert(!spawn_count && !destroy_count);
    session.closed = 0;
    native_debug_reopen(NULL, &session);
    assert(spawn_count == 1 && !destroy_count);
    spawn_ok = 1;
    native_debug_reopen(NULL, &session);
    assert(spawn_count == 2 && destroy_count == 1);
    return 0;
}
'''


class NativeReopen(unittest.TestCase):
    # EN: Compile the production callback for self and verify guard, failed-spawn and successful ownership paths.
    # RU: Компилирует производственный обработчик для self и проверяет ограничения, отказ запуска и успешную передачу.
    def test_reopen_guards_and_spawn_failure(self):
        if not shutil.which('cc'):
            self.skipTest('Needs a C compiler.')
        callback = function('src/insight-debuggers/gdb-window.inc', 'native_debug_reopen')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'reopen.c'
            source.write_text(HARNESS + callback + MAIN)
            executable = Path(directory) / 'reopen'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(source),
                            '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
