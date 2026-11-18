#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Check production source availability and diagnostics with real GLib filesystem checks, without GTK headers.
# RU: Проверяет доступность исходников и пояснения настоящего кода с файловыми проверками GLib без заголовков GTK.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import DECLARATIONS, function


STATE = r'''
#define GTK_LABEL(value) (value)
#define G_FILE_TEST_IS_REGULAR 1
extern gboolean g_file_test(const char *, int);
extern gboolean g_path_is_absolute(const char *);
typedef struct {
    gboolean stopped;
    char *stop_file;
    guint stop_line;
    void *source, *source_status;
} NativeDebug;
static gboolean enabled;
static char *caption, *tooltip;

/* EN: Convert path for the fixture, modeling an encoding failure for the special unconvertible token.
 * RU: Преобразует path для фикстуры, моделируя ошибку кодировки для специальной строки unconvertible. */
static char *utils_get_locale_from_utf8(const char *path)
{
    return !strcmp(path, "unconvertible") ? NULL : g_strdup(path);
}

/* EN: Capture enabled state for the source button widget without constructing GTK.
 * RU: Запоминает доступность value для кнопки widget без создания GTK. */
static void gtk_widget_set_sensitive(void *widget, gboolean value)
{
    (void)widget;
    enabled = value;
}

/* EN: Capture label's new text with production ownership semantics.
 * RU: Запоминает новый text метки label с производственными правилами владения. */
static void gtk_label_set_text(void *label, const char *text)
{
    (void)label;
    g_free(caption);
    caption = g_strdup(text);
}

/* EN: Capture widget's optional full text without GTK rendering.
 * RU: Запоминает необязательный полный text виджета widget без отрисовки GTK. */
static void gtk_widget_set_tooltip_text(void *widget, const char *text)
{
    (void)widget;
    g_free(tooltip);
    tooltip = g_strdup(text);
}
'''

MAIN = r'''
/* EN: Check existing file argv[1], missing/relative/unconvertible paths and cleared/running contexts; argc is two.
 * RU: Проверяет существующий argv[1], отсутствующие/относительные/непреобразуемые пути и сброшенные контексты; argc равен двум. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    NativeDebug session = {0};
    session.stopped = 1;
    session.stop_line = 1;
    session.stop_file = argv[1];
    native_source_update(&session);
    assert(enabled && !*caption && !tooltip);
    const char *unavailable[] = {"missing-file.c", "unconvertible", "/nonexistent/insight-source.c"};
    for (guint i = 0; i < G_N_ELEMENTS(unavailable); i++)
    {
        session.stop_file = (char *)unavailable[i];
        native_source_update(&session);
        assert(!enabled && strstr(caption, unavailable[i]) && !strcmp(caption, tooltip));
    }
    session.stop_file = argv[1];
    session.stop_line = 0;
    native_source_update(&session);
    assert(!enabled && strstr(caption, "no source coordinates"));
    session.stop_line = 1;
    session.stop_file = NULL;
    native_source_update(&session);
    assert(!enabled && strstr(caption, "no source coordinates"));
    session.stopped = 0;
    native_source_update(&session);
    assert(!enabled && !*caption && !tooltip);
    session.stopped = 1;
    session.stop_file = argv[1];
    native_source_update(&session);
    assert(enabled && !*caption && !tooltip);
    g_free(caption);
    g_free(tooltip);
    return 0;
}
'''


class NativeSourceStatus(unittest.TestCase):
    # EN: Compile real source helpers for self; validate filesystem and UI-state transitions against a temporary source.
    # RU: Компилирует настоящие обработчики для self; проверяет файловые и интерфейсные переходы на временном исходнике.
    def test_source_diagnostics_and_recovery(self):
        library = ctypes.util.find_library('glib-2.0')
        if not shutil.which('cc') or not library:
            self.skipTest('Needs cc and the GLib runtime.')
        declarations = DECLARATIONS[:DECLARATIONS.index('typedef struct {\n    GtkTreeStore *all_stack_rows;')]
        helpers = '\n'.join(function('src/insight-debuggers/gdb-source-markers.inc', name)
                            for name in ('native_source_available', 'native_source_update'))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'source-status.c'
            source.write_text(declarations + STATE + helpers + MAIN)
            executable = folder / 'source-status'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(executable), '-l:' + library], check=True)
            subprocess.run([str(executable), str(source)], check=True)


if __name__ == '__main__':
    unittest.main()
