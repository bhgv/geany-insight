#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise production instruction requests and formatting with malformed tuples and real GDB.
# RU: Проверяет производственные запросы и форматирование инструкций на повреждённых данных и настоящем GDB.
import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_memory_test import DECLARATIONS
from native_debugger_models_test import function

EXTRA = r'''
extern char *g_strdup(const char *);
extern char *g_strdup_printf(const char *, ...);
extern char *g_strescape(const char *, const char *);
extern int g_utf8_validate(const char *, long, void *);
extern long g_utf8_strlen(const char *, long);
'''
MAIN = r'''
/* EN: Validate bounded commands and formatting, optionally processing one real MI record from argv.
 * RU: Проверяет ограниченные команды и форматирование, при наличии argv читает настоящий ответ MI. */
int main(int argc, char **argv)
{
    if (argc == 2)
    {
        char *text = native_assembly_format(argv[1]);
        assert(text && strstr(text, "main") && strstr(text, "0x"));
        g_free(text);
        text = native_assembly_format_bytes(argv[1]);
        assert(text && strstr(text, "[") && !strstr(text, "[??]"));
        g_free(text);
        return 0;
    }
    char *command = native_assembly_command("$pc");
    assert(command && strstr(command, "-s \"$pc\" -e \"($pc)+128\" -- 2"));
    g_free(command);
    assert(!native_assembly_command("a\nb"));
    assert(!native_assembly_command(""));
    assert(!native_assembly_command(NULL));
    command = native_assembly_command("a\"b");
    assert(command && strstr(command, "a\\\"b"));
    g_free(command);
    char *text = native_assembly_format("^done,asm_insns=[{address=\"0x10\",func-name=\"main\",offset=\"0\",inst=\"ret\"}]");
    assert(text && strstr(text, "0x10  <main + 0>  ret"));
    g_free(text);
    const char *raw[] = {"55 48 89 e5", "e92d4800", "05 45", "13050500"};
    for (unsigned i = 0; i < sizeof raw / sizeof *raw; i++)
    {
        char *bytes = native_assembly_bytes(raw[i]);
        assert(bytes && !strcmp(bytes, raw[i]));
        g_free(bytes);
    }
    assert(!native_assembly_bytes(NULL));
    assert(!native_assembly_bytes("9"));
    assert(!native_assembly_bytes("zz"));
    assert(!native_assembly_bytes("90\nret"));
    char oversized[514];
    memset(oversized, 'a', sizeof oversized - 1);
    oversized[sizeof oversized - 1] = '\0';
    assert(!native_assembly_bytes(oversized));
    text = native_assembly_format_bytes("^done,asm_insns=[{address=\"0x10\",opcodes=\"c3\",inst=\"ret\"}]");
    assert(text && strstr(text, "[c3]  ret"));
    g_free(text);
    text = native_assembly_format_bytes("^done,asm_insns=[{address=\"0x10\",opcodes=\"bad!\",inst=\"ret\"}]");
    assert(text && strstr(text, "[??]  ret"));
    g_free(text);
    assert(!native_assembly_format("^error,msg=\"bad address\""));
    assert(!native_assembly_format("^done,asm_insns=[]"));
    assert(!native_assembly_format("^done,asm_insns=[{address=\"0x10\"}]"));
    assert(!native_assembly_format("^done,asm_insns=[{address=\"0x10\",inst=\"ret\"}"));
    return 0;
}
'''


class NativeDisassembly(unittest.TestCase):
    # EN: Compile actual model functions, test malformed records and disassemble an executable without running it.
    # RU: Компилирует настоящие функции модели, проверяет ошибки и дизассемблирует программу без её запуска.
    def test_instructions(self):
        library = ctypes.util.find_library('glib-2.0')
        if not library or not shutil.which('cc'):
            self.skipTest('Needs cc and GLib runtime.')
        helpers = [function('src/insight-debuggers/gdb-context.inc', name)
                   for name in ('native_debug_field', 'native_next_tuple')]
        model = Path('src/insight-debuggers/gdb-disassembly-model.inc').read_text()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'model.c'
            source.write_text(DECLARATIONS + EXTRA + '\n'.join(helpers) + model + MAIN)
            executable = root / 'model'
            subprocess.run(['cc', '-g', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(executable), '-l:' + library], check=True)
            subprocess.run([str(executable)], check=True)
            gdb = os.environ.get('GDB') or shutil.which('gdb')
            if gdb:
                reply = subprocess.run([gdb, '-q', '-nx', '--interpreter=mi2', str(executable)],
                                       input='-data-disassemble -s main -e main+128 -- 2\n-gdb-exit\n',
                                       capture_output=True, text=True, check=True)
                record = next(line for line in reply.stdout.splitlines() if line.startswith('^done,asm_insns='))
                subprocess.run([str(executable), record], check=True)


if __name__ == '__main__':
    unittest.main()
