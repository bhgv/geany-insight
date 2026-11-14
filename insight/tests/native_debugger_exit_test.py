#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Validate production exit classification and human status against reordered MI and real GDB exits.
# RU: Проверяет определение завершения и понятный статус на переставленных полях MI и настоящем GDB.
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import ROOT, function

MAIN = r'''
/* EN: Classify argv[1]'s MI record; print its owned detail, or "active" for a non-exit. argc must be two.
 * RU: Определяет завершение по MI-записи argv[1]; печатает описание либо "active". argc равен двум. */
int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 2);
    if (!native_exit_record(argv[1]))
        puts("active");
    else
    {
        gchar *detail = native_exit_detail(argv[1]);
        puts(detail ? detail : "no detail");
        g_free(detail);
    }
    return 0;
}
'''


class NativeExit(unittest.TestCase):
    # EN: Compile the production parser and formatter once per test; skip missing compiler/GLib dependencies.
    # RU: Компилирует настоящий разборщик и форматтер для теста; пропускает при отсутствии компилятора/GLib.
    def setUp(self):
        if not shutil.which('cc') or not shutil.which('pkg-config'):
            self.skipTest('Needs cc and GLib development files.')
        flags = subprocess.run(['pkg-config', '--cflags', '--libs', 'glib-2.0'],
                               text=True, capture_output=True)
        if flags.returncode:
            self.skipTest('Needs GLib development files.')
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.model = Path(self.folder.name) / 'exit-model'
        source = '#include <glib.h>\n#include <stdio.h>\n#include <string.h>\n#define insight_tr(s) (s)\n'
        source += function('src/insight-debuggers/gdb-context.inc', 'native_debug_field')
        source += (ROOT / 'src/insight-debuggers/gdb-exit-model.inc').read_text() + MAIN
        path = self.model.with_suffix('.c')
        path.write_text(source)
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(self.model),
                        *flags.stdout.split()], check=True)

    # EN: Execute the production model with record, returning its printable status and enforcing a deadline.
    # RU: Запускает настоящую модель с record, возвращая текст статуса и ограничивая время выполнения.
    def status(self, record):
        return subprocess.check_output([str(self.model), record], text=True, timeout=5).strip()

    # EN: Cover reordered exit reasons, octal/hex/invalid codes, signals and harmless active MI records.
    # RU: Проверяет переставленные причины, восьмеричные/hex/неверные коды, сигналы и записи активной цели.
    def test_exit_records(self):
        cases = {
            '*stopped,thread-id="1",reason="exited-normally"': 'Program exited with code 0.',
            '*stopped,exit-code="037",reason="exited"': 'Program exited with code 31.',
            '=thread-group-exited,id="i1",exit-code="010"': 'Program exited with code 8.',
            '=thread-group-exited,id="i1",exit-code="0xc0000005"': 'Program exited with code 3221225477.',
            '=thread-group-exited,id="i1"': 'no detail',
            '*stopped,reason="exited",exit-code="invalid"': 'Program exited.',
            '*stopped,reason="exited",exit-code="4294967296"': 'Program exited.',
            '*stopped,reason="exited",exit-code="-1"': 'Program exited.',
            '*stopped,reason="exited",exit-code="09"': 'Program exited.',
            '*stopped,signal-name="SIGTERM",signal-meaning="Terminated",reason="exited-signalled"':
                'Program terminated by signal: SIGTERM — Terminated',
            '*stopped,reason="exited-signalled"': 'Program terminated by signal: ?',
            '*stopped,reason="signal-received",signal-name="SIGSEGV"': 'active',
            '*stopped,frame={func="exited"},reason="breakpoint-hit"': 'active',
            '~"reason=\\"exited\\""': 'active',
            '*running,thread-id="all"': 'active',
        }
        for record, expected in cases.items():
            with self.subTest(record=record):
                self.assertEqual(self.status(record), expected)

    # EN: Check actual GDB normal/nonzero/signal exits; skip only when ptrace is blocked or GDB is missing.
    # RU: Проверяет настоящие выходы GDB: нормальный, ненулевой, по сигналу; пропускает без GDB или ptrace.
    def test_real_gdb_exits(self):
        gdb = os.environ.get('GDB', shutil.which('gdb'))
        if not gdb:
            self.skipTest('Needs GDB.')
        folder = Path(self.folder.name)
        target = folder / 'target'
        path = target.with_suffix('.c')
        path.write_text('/* EN/RU: Exit-code and signal fixture. / Фикстура кода выхода и сигнала. */\n'
                        '#include <signal.h>\n#include <stdlib.h>\n'
                        '/* EN/RU: argc/argv select normal exit or SIGTERM. / argc/argv выбирают код или SIGTERM. */\n'
                        'int main(int argc, char **argv)\n{\n'
                        '    if (argc > 1 && argv[1][0] == \'s\')\n        raise(SIGTERM);\n'
                        '    return argc > 1 ? atoi(argv[1]) : 0;\n}\n')
        subprocess.run(['cc', '-g', str(path), '-o', str(target)], check=True)
        for argument, expected in [('0', 'Program exited with code 0.'),
                                   ('31', 'Program exited with code 31.'),
                                   ('signal', 'Program terminated by signal: SIGTERM — Terminated')]:
            commands = ['-gdb-set startup-with-shell off',
                        '-interpreter-exec console "handle SIGTERM nostop noprint pass"',
                        '-exec-arguments ' + argument, '-exec-run', '-gdb-exit']
            result = subprocess.run([gdb, '--nx', '--quiet', '--interpreter=mi2', str(target)],
                                    input='\n'.join(commands) + '\n', text=True,
                                    capture_output=True, timeout=20, check=True)
            if 'ptrace: Operation not permitted' in result.stdout + result.stderr:
                self.skipTest('Sandbox blocks ptrace; real exits run in Linux CI.')
            record = next((line for line in result.stdout.splitlines()
                           if line.startswith('*stopped,')), None)
            self.assertIsNotNone(record, result.stdout)
            self.assertEqual(self.status(record), expected)


if __name__ == '__main__':
    unittest.main()
