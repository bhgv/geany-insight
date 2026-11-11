#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Check all native debugger fragments at the supported GTK/GLib API ceilings.
# RU: Проверяет все части нативного отладчика с заявленными ограничениями API GTK/GLib.
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeCompile(unittest.TestCase):
    # EN: Compile production fragments with real headers; no application stubs are executed.
    # RU: Компилирует производственные части с настоящими заголовками; заглушки приложения не выполняются.
    def test_supported_apis(self):
        if not shutil.which('cc'):
            self.skipTest('Needs a C compiler.')
        flags = os.environ.get('INSIGHT_GTK_CFLAGS')
        if flags is None:
            if not shutil.which('pkg-config'):
                self.skipTest('Needs GTK development headers and pkg-config.')
            result = subprocess.run(['pkg-config', '--cflags', 'gtk+-3.0', 'gio-2.0'],
                                    capture_output=True, text=True)
            if result.returncode:
                self.skipTest('Needs GTK development headers: ' + result.stderr.splitlines()[0])
            flags = result.stdout
        command = ['cc', '-fsyntax-only', '-DGTK', '-DGEANY_PRIVATE',
                   '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56',
                   '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_56',
                   '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_22',
                   '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_3_22',
                   '-Werror=implicit-function-declaration', '-Werror=incompatible-pointer-types',
                   '-Werror=deprecated-declarations', *shlex.split(flags)]
        command += ['-I' + str(ROOT / name) for name in
                    ('', 'src', 'src/tagmanager', 'ctags/main', 'scintilla/include')]
        command += [str(ROOT / 'insight/tests/native_debugger_compile_fixture.c')]
        subprocess.run(command, check=True)


if __name__ == '__main__':
    unittest.main()
