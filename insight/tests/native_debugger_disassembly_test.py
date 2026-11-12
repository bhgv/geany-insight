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
    /* EN: A final oversized tuple invalidates the whole snapshot, even after a valid instruction.
     * RU: Слишком большой последний кортеж отменяет весь снимок, даже после корректной инструкции. */
    char huge_instruction[8193];
    memset(huge_instruction, 'x', sizeof huge_instruction - 1);
    huge_instruction[sizeof huge_instruction - 1] = '\0';
    const char *prefixes[] = {"", "{address=\"0x10\",inst=\"nop\"},"};
    for (unsigned i = 0; i < sizeof prefixes / sizeof *prefixes; i++)
    {
        char *record = g_strdup_printf("^done,asm_insns=[%s{address=\"0x11\",inst=\"%s\"}]",
            prefixes[i], huge_instruction);
        assert(!native_assembly_format(record));
        assert(!native_assembly_format_bytes(record));
        g_free(record);
    }
    huge_instruction[8192 - strlen("{address=\"0x11\",inst=\"\"}")] = '\0';
    char *boundary = g_strdup_printf("^done,asm_insns=[{address=\"0x11\",inst=\"%s\"}]", huge_instruction);
    text = native_assembly_format(boundary);
    assert(text && strstr(text, "0x11"));
    g_free(text);
    text = native_assembly_format_bytes(boundary);
    assert(text && strstr(text, "[??]"));
    g_free(text);
    g_free(boundary);

    /* EN: Keep valid row/size boundaries readable while rejecting a complete-looking over-limit snapshot.
     * RU: Сохраняет допустимые границы строк/размера и отклоняет кажущийся полным снимок сверх лимита. */
    for (unsigned rows = 256; rows <= 257; rows++)
    {
        GString *record = g_string_new("^done,asm_insns=[");
        for (unsigned row = 0; row < rows; row++)
            g_string_append_printf(record, "%s{address=\"0x%x\",inst=\"nop\"}", row ? "," : "", row);
        g_string_append(record, "]");
        text = native_assembly_format(record->str);
        assert((text != NULL) == (rows == 256));
        g_free(text);
        text = native_assembly_format_bytes(record->str);
        assert((text != NULL) == (rows == 256));
        g_free(text);
        g_string_free(record, TRUE);
    }
    huge_instruction[8000] = '\0';
    for (unsigned rows = 8; rows <= 9; rows++)
    {
        GString *record = g_string_new("^done,asm_insns=[");
        for (unsigned row = 0; row < rows; row++)
            g_string_append_printf(record, "%s{address=\"0x%x\",inst=\"%s\"}",
                row ? "," : "", row, huge_instruction);
        g_string_append(record, "]");
        text = native_assembly_format(record->str);
        assert((text != NULL) == (rows == 8));
        g_free(text);
        text = native_assembly_format_bytes(record->str);
        assert((text != NULL) == (rows == 8));
        g_free(text);
        g_string_free(record, TRUE);
    }
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
