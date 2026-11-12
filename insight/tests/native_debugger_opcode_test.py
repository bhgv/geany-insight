#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Validate production opcode lookup against complete and rejected instruction snapshots without GTK widgets.
# RU: Проверяет поиск байтов инструкций в полных и отклонённых снимках без виджетов GTK.
import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_disassembly_test import EXTRA
from native_debugger_memory_test import DECLARATIONS
from native_debugger_models_test import function

STATE = r'''
#include <ctype.h>
#define g_ascii_isdigit(c) isdigit((unsigned char)(c))
#define g_ascii_isxdigit(c) isxdigit((unsigned char)(c))
typedef struct {const gchar *record;} NativeAssembly;
'''
MAIN = r'''
/* EN: Check bytes and rejected snapshots; optional argv[1] supplies a real GDB record when argc is two.
 * RU: Проверяет байты и отклонённые снимки; необязательный argv[1] задаёт настоящий ответ GDB при argc равном двум. */
int main(int argc, char **argv)
{
    if (argc == 2)
    {
        NativeAssembly actual = {.record = argv[1]};
        gchar *address = native_debug_field(actual.record, "address");
        gchar *raw = native_debug_field(actual.record, "opcodes");
        gchar *expected = native_assembly_bytes(raw);
        gchar *copied = native_opcode_at(&actual, address);
        assert(expected && copied && !strcmp(expected, copied));
        g_free(copied);
        g_free(expected);
        g_free(raw);
        g_free(address);
        return 0;
    }
    NativeAssembly assembly = {.record = "^done,asm_insns=[{address=\"0x10\",opcodes=\"55 48\",inst=\"push rbp\"},"
        "{address=\"0x11\",opcodes=\"c3\",inst=\"ret\"}]"};
    gchar *bytes = native_opcode_at(&assembly, "0x10");
    assert(bytes && !strcmp(bytes, "55 48"));
    g_free(bytes);
    bytes = native_opcode_at(&assembly, "17");
    assert(bytes && !strcmp(bytes, "c3"));
    g_free(bytes);
    assert(!native_opcode_at(&assembly, "0x12"));
    assert(!native_opcode_at(&assembly, "not an address"));
    assert(!native_opcode_at(&assembly, "0x10000000000000000"));
    gchar *owned_record = g_strdup("^done,asm_insns=[{address=\"0x10\",opcodes=\"55 48\",inst=\"push rbp\"},"
        "{address=\"0x11\",inst=\"ret\"}]");
    assembly.record = owned_record;
    bytes = native_opcode_at(&assembly, "0x10");
    assert(!native_opcode_at(&assembly, "0x11"));
    g_free(owned_record);
    assert(bytes && !strcmp(bytes, "55 48"));
    g_free(bytes);
    const gchar *invalid[] = {
        "^done,asm_insns=[{address=\"0x10\",opcodes=\"c3\",inst=\"ret\"}",
        "^done,asm_insns=[{address=\"0x10\",opcodes=\"c3\",inst=\"ret\"},{",
        "^error,asm_insns=[{address=\"0x10\",opcodes=\"c3\",inst=\"ret\"}]",
        "^done,asm_insns=[{address=\"0x10\",opcodes=\"c3\"}]",
        "^done,asm_insns=[{address=\"0x10\",opcodes=\"z3\",inst=\"ret\"}]",
        "^done,asm_insns=[{address=\"0x10\",inst=\"ret\"}]"
    };
    for (guint i = 0; i < sizeof invalid / sizeof *invalid; i++)
    {
        assembly.record = invalid[i];
        assert(!native_opcode_at(&assembly, "0x10"));
    }
    char huge[8193];
    memset(huge, 'x', sizeof huge - 1);
    huge[sizeof huge - 1] = '\0';
    gchar *oversized = g_strdup_printf("^done,asm_insns=[{address=\"0x10\",opcodes=\"c3\",inst=\"ret\"},"
        "{address=\"0x11\",inst=\"%s\"}]", huge);
    assembly.record = oversized;
    assert(!native_opcode_at(&assembly, "0x10"));
    g_free(oversized);
    GString *many = g_string_new("^done,asm_insns=[");
    for (guint row = 0; row < 257; row++)
        g_string_append_printf(many, "%s{address=\"0x%x\",opcodes=\"90\",inst=\"nop\"}",
            row ? "," : "", row + 16);
    g_string_append(many, "]");
    assembly.record = many->str;
    assert(!native_opcode_at(&assembly, "0x10"));
    g_string_free(many, TRUE);
    assembly.record = NULL;
    assert(!native_opcode_at(&assembly, "0x10"));
    return 0;
}
'''


class NativeOpcode(unittest.TestCase):
    # EN: Compile actual formatting/address/lookup functions and check byte ownership plus whole-snapshot validity.
    # RU: Компилирует настоящие функции форматирования/адресов/поиска и проверяет владение байтами и целостность снимка.
    def test_snapshot_bytes(self):
        library = ctypes.util.find_library('glib-2.0')
        if not library or not shutil.which('cc'):
            self.skipTest('Needs cc and GLib runtime.')
        helpers = [function('src/insight-debuggers/gdb-context.inc', name)
                   for name in ('native_debug_field', 'native_next_tuple')]
        code = DECLARATIONS + EXTRA + STATE + '\n'.join(helpers)
        code += Path('src/insight-debuggers/gdb-disassembly-model.inc').read_text()
        code += function('src/insight-debuggers/disassembly/resolve.inc', 'native_assembly_number')
        code += function('src/insight-debuggers/disassembly/opcode-ui.inc', 'native_opcode_at')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'opcode.c'
            source.write_text(code + MAIN)
            executable = root / 'opcode'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(source), '-o', str(executable), '-l:' + library], check=True)
            subprocess.run([str(executable)], check=True)
            gdb = os.environ.get('GDB') or shutil.which('gdb')
            if gdb:
                reply = subprocess.run([gdb, '-q', '-nx', '--interpreter=mi2', str(executable)],
                                       input='-data-disassemble -s main -e main+16 -- 2\n-gdb-exit\n',
                                       capture_output=True, text=True, check=True)
                record = next(line for line in reply.stdout.splitlines() if line.startswith('^done,asm_insns='))
                subprocess.run([str(executable), record], check=True)


if __name__ == '__main__':
    unittest.main()
