#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Check literal launch argv through production quoting, a real POSIX shell and GDB.
# RU: Проверяет буквальный argv через настоящее экранирование, оболочку POSIX и GDB.
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import ROOT


class NativeArguments(unittest.TestCase):
    # EN: Compile production quoting and a hex-printing argv fixture; skip absent compiler/GLib tools.
    # RU: Компилирует настоящее экранирование и фикстуру hex-вывода argv; пропускает без компилятора/GLib.
    def setUp(self):
        if not shutil.which('cc') or not shutil.which('pkg-config'):
            self.skipTest('Needs cc and GLib development files.')
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.model = self.root / 'arguments'
        source = '#include <glib.h>\n#include <stdio.h>\n#include <string.h>\n#define insight_tr(s) (s)\n'
        source += (ROOT / 'src/insight-debuggers/gdb-arguments-model.inc').read_text()
        source += r'''
/* EN: Print argv[1]'s launch command for argv[2]'s target; argc must be three. Reject invalid input.
 * RU: Печатает команду запуска argv[1] для цели argv[2]; argc равен трём. Отклоняет неверный ввод. */
int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 3);
    if (!strcmp(argv[2], "entry") || !strcmp(argv[2], "run"))
    {
        puts(native_launch_run_command(!strcmp(argv[2], "entry")));
        return 0;
    }
    GError *error = NULL;
    gchar *command = native_launch_arguments(argv[1], !strcmp(argv[2], "windows"), &error);
    if (!command)
    {
        g_assert_nonnull(error);
        g_error_free(error);
        return 2;
    }
    puts(command);
    g_free(command);
    return 0;
}
'''
        path = self.model.with_suffix('.c')
        path.write_text(source)
        flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'glib-2.0'], text=True)
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(self.model),
                        *shlex.split(flags)], check=True)
        self.target = self.root / 'target'
        path = self.target.with_suffix('.c')
        path.write_text(r'''/* EN: Emit argv bytes for launch tests. RU: Выводит байты argv для проверок запуска. */
#include <stdio.h>
/* EN: Print argc/argv arguments as hex to preserve empty, quoted and non-ASCII values.
 * RU: Печатает аргументы argc/argv в hex, сохраняя пустые значения, кавычки и не-ASCII. */
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        printf("ARG:");
        for (unsigned char *p = (unsigned char *)argv[i]; *p; p++)
            printf("%02x", *p);
        puts("");
    }
    return 0;
}
''')
        subprocess.run(['cc', '-g', str(path), '-o', str(self.target)], check=True)

    # EN: Return the command for text and target using the production model; fail on rejected input.
    # RU: Возвращает команду для text и target через настоящую модель; отклонённый ввод вызывает ошибку.
    def command(self, text, target='posix'):
        return subprocess.check_output([str(self.model), text, target], text=True, timeout=5).rstrip('\n')

    # EN: Build a demanding literal argv and parse its actual printed bytes from output.
    # RU: Создаёт сложный буквальный argv и извлекает фактически напечатанные байты из output.
    def arguments(self, output=None):
        if output is None:
            return ['31', 'two words', '', 'a\\b', 'trailing\\', 'quote"here', "a'b", 'ключ',
                    '*', '$HOME', '$(touch should-not-exist)', '`touch should-not-exist`', '; quit']
        return [bytes.fromhex(line[4:]).decode() for line in output.splitlines() if line.startswith('ARG:')]

    # EN: Verify actual POSIX argv including empty strings and literal shell syntax, with no expansion or side effects.
    # RU: Проверяет настоящий argv POSIX, включая пустые строки и синтаксис оболочки, без подстановок и действий.
    def test_literal_shell_argv(self):
        expected = self.arguments()
        command = self.command(shlex.join(expected))
        raw = command.removeprefix('-exec-arguments')
        output = subprocess.check_output(['/bin/sh', '-c', 'exec ' + shlex.quote(str(self.target)) + raw],
                                         cwd=self.root, text=True, timeout=5)
        self.assertEqual(self.arguments(output), expected)
        self.assertFalse((self.root / 'should-not-exist').exists())
        self.assertEqual(self.command(''), '-exec-arguments')
        for text in ["'unterminated", 'one\ntwo', "'one\rtwo'"]:
            self.assertEqual(subprocess.run([str(self.model), text, 'posix'], timeout=5).returncode, 2)

    # EN: Verify Windows CRT quoting for spaces, quotes and trailing backslashes independently of the host platform.
    # RU: Проверяет экранирование CRT Windows для пробелов, кавычек и конечных слешей независимо от ОС проверки.
    def test_windows_encoding(self):
        self.assertEqual(self.command("31 'two words' '' 'C:\\two words\\'", 'windows'),
                         '-exec-arguments "31" "two words" "" "C:\\two words\\\\"')
        self.assertEqual(self.command("'a\"b' 'a\\\"b'", 'windows'),
                         '-exec-arguments "a\\\"b" "a\\\\\\\"b"')

    # EN: Check GDB's actual inferior argv; skip when ptrace is denied or GDB is missing.
    # RU: Проверяет настоящий argv программы через GDB; пропускает при запрете ptrace или отсутствии GDB.
    def test_real_gdb_argv(self):
        gdb = os.environ.get('GDB', shutil.which('gdb'))
        if not gdb:
            self.skipTest('Needs GDB.')
        expected = self.arguments()
        commands = ['-gdb-set startup-with-shell on', self.command(shlex.join(expected)),
                    self.command('', 'run'), '-gdb-exit']
        result = subprocess.run([gdb, '--nx', '--quiet', '--interpreter=mi2', str(self.target)],
                                input='\n'.join(commands) + '\n', cwd=self.root,
                                text=True, capture_output=True, timeout=20, check=True)
        if 'ptrace: Operation not permitted' in result.stdout + result.stderr:
            self.skipTest('Sandbox blocks ptrace; actual argv runs in Linux CI.')
        self.assertEqual(self.arguments(result.stdout), expected, result.stdout)
        self.assertFalse((self.root / 'should-not-exist').exists())

    # EN: Verify the production entry command stops in main without leaving a permanent breakpoint.
    # RU: Проверяет остановку настоящей команды входа в main без оставшейся постоянной точки.
    def test_real_entry_stop(self):
        gdb = os.environ.get('GDB', shutil.which('gdb'))
        if not gdb:
            self.skipTest('Needs GDB.')
        commands = [self.command('', 'entry'), '-stack-info-frame', '-break-list',
                    '-exec-continue', '-gdb-exit']
        result = subprocess.run([gdb, '--nx', '--quiet', '--interpreter=mi2', str(self.target)],
                                input='\n'.join(commands) + '\n', text=True,
                                capture_output=True, timeout=20, check=True)
        if 'ptrace: Operation not permitted' in result.stdout + result.stderr:
            self.skipTest('Sandbox blocks ptrace; entry stop runs in Linux CI.')
        self.assertIn('func="main"', result.stdout)
        self.assertIn('reason="breakpoint-hit"', result.stdout)
        table = next(line for line in result.stdout.splitlines() if 'BreakpointTable=' in line)
        self.assertIn('body=[]', table)
        self.assertNotIn('^error', result.stdout)


if __name__ == '__main__':
    unittest.main()
