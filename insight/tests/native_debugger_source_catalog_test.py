#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Validate exact source-file resolution against complete, ambiguous and boundary-size GDB catalogs.
# RU: Проверяет точное разрешение исходников по полным, неоднозначным и граничным каталогам GDB.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_names_fixture import model_source

SESSION = '\ntypedef struct {NativeAssembly *assembly;} NativeDebug;\n'
MAIN = r'''
/* EN: Check argv[1]'s malformed/boundary scenario without GTK or an inferior; argc must be two.
 * RU: Проверяет сценарий обрезки/границ argv[1] без GTK и отлаживаемого процесса; argc равен двум. */
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "malformed"))
    {
        const gchar *invalid[] = {
            "^done,files=[{file=\"src/a.c\",fullname=\"/project/src/a.c\"}",
            "^done,files=[{file=\"src/a.c\",fullname=\"/project/src/a.c\"},",
            "^done,files=[{file=\"src/a.c\",fullname=\"/project/src/a.c\"},{file=\"src/a.c\",fullname=\"",
            "^done,files=[{file=\"src/a.c\",fullname=\"/one/a.c\"},{file=\"src/a.c\",fullname=\"/two/a.c\"}]",
            "^done,files=[{file=\"other/a.c\",fullname=\"/project/other/a.c\"}]",
            "^done,files=[]",
            "^error,msg=\"No symbols\""
        };
        for (guint i = 0; i < sizeof invalid / sizeof *invalid; i++)
            assert(!native_instruction_source_fullname(invalid[i], "src/a.c"));
        gchar *file = native_instruction_source_fullname(
            "^done,files=[{file=\"src/a.c\",fullname=\"/project/src/a.c\"},"
            "{file=\"src/a.c\",fullname=\"/project/src/a.c\"}]", "src/a.c");
        assert(file && !strcmp(file, "/project/src/a.c"));
        g_free(file);
        return 0;
    }
    gchar *file = NULL;
    for (guint rows = 4095; rows <= 4097; rows++)
    {
        GString *record = g_string_new("^done,files=[");
        for (guint row = 0; row < rows; row++)
            g_string_append_printf(record, "%s{file=\"src/f%u.c\",fullname=\"/project/src/f%u.c\"}",
                row ? "," : "", row, row);
        g_string_append(record, "]");
        file = native_instruction_source_fullname(record->str, "src/f0.c");
        assert((file != NULL) == (rows <= 4096));
        if (file)
            assert(!strcmp(file, "/project/src/f0.c"));
        g_free(file);
        gchar *last = g_strdup_printf("src/f%u.c", rows - 1);
        gchar *expected = g_strdup_printf("/project/%s", last);
        file = native_instruction_source_fullname(record->str, last);
        assert((file != NULL) == (rows <= 4096));
        if (file)
            assert(!strcmp(file, expected));
        g_free(file);
        g_free(expected);
        g_free(last);
        g_string_free(record, TRUE);
    }
    return 0;
}
'''


class NativeSourceCatalog(unittest.TestCase):
    # EN: Compile production source resolution and exercise truncated catalogs, ambiguity and exact count boundaries.
    # RU: Компилирует настоящее разрешение исходников и проверяет обрезку, неоднозначность и точные границы числа записей.
    def test_catalog_completeness(self):
        libraries = [ctypes.util.find_library(name) for name in ('gio-2.0', 'glib-2.0')]
        if not all(libraries) or not shutil.which('cc'):
            self.skipTest('Needs cc and GLib/GIO runtime libraries.')
        code = model_source(SESSION)
        code += Path('src/insight-debuggers/disassembly/source-model.inc').read_text()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source-catalog.c'
            source.write_text(code + MAIN)
            executable = root / 'source-catalog'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(executable), *['-l:' + name for name in libraries]], check=True)
            for scenario in ('malformed', 'boundary'):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(executable), scenario], check=True)


if __name__ == '__main__':
    unittest.main()
