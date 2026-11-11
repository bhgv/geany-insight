#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Execute the production memory formatter on valid, fragmented and malformed GDB records.
# RU: Выполняет производственное форматирование памяти на верных, составных и повреждённых ответах GDB.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function

DECLARATIONS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef char gchar;
typedef unsigned char guchar;
typedef unsigned int guint;
typedef int gint;
typedef int gboolean;
typedef size_t gsize;
typedef long gssize;
typedef uint64_t guint64;
typedef struct {char *str; size_t len; size_t allocated_len;} GString;
#define FALSE 0
#define TRUE 1
#define G_MAXUINT64 UINT64_MAX
#define G_GINT64_MODIFIER "l"
extern void g_free(void *);
extern char *g_strndup(const char *, size_t);
extern char *g_strcompress(const char *);
extern int g_str_has_prefix(const char *, const char *);
extern int g_ascii_string_to_unsigned(const char *, unsigned, guint64, guint64, guint64 *, void *);
extern int g_ascii_xdigit_value(char);
extern GString *g_string_new(const char *);
extern GString *g_string_append(GString *, const char *);
extern GString *g_string_append_c(GString *, char);
extern void g_string_append_printf(GString *, const char *, ...);
extern char *g_string_free(GString *, int);
'''

MAIN = r'''
/* EN: Check complete byte coverage, ASCII, high addresses, pagination and rejected corrupt inputs.
 * RU: Проверяет покрытие байтов, ASCII, старшие адреса, страницы и отклонение повреждённых данных. */
int main(void)
{
    const char *valid = "^done,memory=[{begin=\"0x1000\",offset=\"0x0\",contents=\"0041ff7f20\"}]";
    char *text = native_memory_format(valid, 5);
    assert(text && strstr(text, "0x0000000000001000") && strstr(text, "00 41 ff 7f 20"));
    assert(strstr(text, " | .A.. \n"));
    g_free(text);
    text = native_memory_format("^done,memory=[{begin=\"0x1002\",offset=\"0x2\",contents=\"43\"},"
        "{begin=\"0x1000\",offset=\"0x0\",contents=\"4142\"}]", 3);
    assert(text && strstr(text, " | ABC\n"));
    g_free(text);
    text = native_memory_format("^done,memory=[{begin=\"0xffffffffffffffff\",offset=\"0x0\",contents=\"ff\"}]", 1);
    assert(text && strstr(text, "0xffffffffffffffff"));
    g_free(text);
    const char *invalid[] = {
        "^error,msg=\"Cannot access memory\"",
        "^done,memory=[]",
        "^done,memory=[{begin=\"0x1000\",offset=\"0x0\",contents=\"0\"}]",
        "^done,memory=[{begin=\"0x1000\",offset=\"0x0\",contents=\"gg\"}]",
        "^done,memory=[{begin=\"0x1000\",offset=\"0x0\",contents=\"00\"}]",
        "^done,memory=[{begin=\"0xffffffffffffffff\",offset=\"0x0\",contents=\"ffff\"}]",
        "^done,memory=[{begin=\"0x1000\",offset=\"0x2\",contents=\"00\"}]",
        "^done,memory=[{begin=\"0x0\",offset=\"0x1\",contents=\"00\"}]",
        "^done,memory=[{begin=\"0x1000\",offset=\"0x0\",contents=\"00\"},"
            "{begin=\"0x1000\",offset=\"0x0\",contents=\"00\"}]",
        "^done,memory=[{begin=\"0x1000\",offset=\"0x0\",contents=\"00\"},"
            "{begin=\"0x2001\",offset=\"0x1\",contents=\"00\"}]"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++)
        assert(!native_memory_format(invalid[i], 2));
    assert(!native_memory_format(valid, 0));
    assert(!native_memory_format(valid, 4097));
    return 0;
}
'''


class NativeMemory(unittest.TestCase):
    # EN: Compile actual parser functions against GLib and execute byte-layout/error cases without a display.
    # RU: Компилирует настоящие функции разбора с GLib и проверяет байты/ошибки без дисплея.
    def test_memory_records(self):
        library = ctypes.util.find_library('glib-2.0')
        if not library or not shutil.which('cc'):
            self.skipTest('Needs cc and the GLib runtime library.')
        helpers = [function('src/insight-debuggers/gdb-context.inc', name)
                   for name in ('native_debug_field', 'native_next_tuple')]
        helpers.extend(function('src/insight-debuggers/gdb-memory-model.inc', name)
                       for name in ('native_memory_address', 'native_memory_format'))
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as temporary:
            root = Path(temporary)
            source = root / 'memory.c'
            source.write_text(DECLARATIONS + '\n'.join(helpers) + MAIN)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(source),
                            '-o', str(root / 'memory'), '-l:' + library], check=True)
            subprocess.run([str(root / 'memory')], check=True)


if __name__ == '__main__':
    unittest.main()
