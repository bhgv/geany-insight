#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Check incremental font installation, staging and preservation when replacement fails.
# RU: Проверяет повторную установку шрифтов, промежуточный каталог и сохранность при ошибке замены.
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('install_fonts', 'insight/tools/install_fonts.py')
FONTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FONTS)


class FontInstall(unittest.TestCase):
    # EN: Install/update nested files, skip byte-identical destinations without replacement, and retain old bytes on a simulated Windows lock.
    # RU: Устанавливает/обновляет вложенные файлы, пропускает одинаковые без замены и сохраняет прежние байты при имитации блокировки Windows.
    def test_incremental_and_locked(self):
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()):
            root = Path(temporary)
            source = root / 'source'
            original = source / 'NotoSans' / 'font.ttf'
            original.parent.mkdir(parents=True)
            original.write_bytes(b'font-one')
            destination = root / 'installed'
            target = destination / 'NotoSans' / 'font.ttf'
            FONTS.install(source, destination)
            self.assertEqual(target.read_bytes(), b'font-one')
            with patch.object(FONTS.os, 'replace', side_effect=AssertionError('Unchanged font must not be replaced')):
                FONTS.install(source, destination)
            original.write_bytes(b'font-two')
            with patch.object(FONTS.os, 'replace', side_effect=PermissionError('Windows sharing violation')):
                with self.assertRaisesRegex(OSError, 'Close running Geany Insight'):
                    FONTS.install(source, destination)
            self.assertEqual(target.read_bytes(), b'font-one')
            self.assertFalse(list(destination.rglob('.insight-font-*')))
            FONTS.install(source, destination)
            self.assertEqual(target.read_bytes(), b'font-two')

    # EN: Run the production Meson declaration out of tree, twice and with DESTDIR, to catch relative script paths and needless writes.
    # RU: Выполняет настоящее объявление Meson вне дерева исходников, повторно и с DESTDIR, проверяя путь скрипта и лишнюю перезапись.
    def test_meson_out_of_tree(self):
        binary = shutil.which('meson')
        script = os.environ.get('INSIGHT_TEST_MESON_SCRIPT')
        if not binary and not script:
            self.skipTest('Needs Meson executable or INSIGHT_TEST_MESON_SCRIPT.')
        command = [binary] if binary else [sys.executable, script]
        production = Path('meson.build').read_text()
        start = production.index('meson.add_install_script(python, ')
        declaration = production[start:production.index('\n\n', start)]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source = root / 'source'
            tools = source / 'insight' / 'tools'
            tools.mkdir(parents=True)
            shutil.copyfile('insight/tools/install_fonts.py', tools / 'install_fonts.py')
            fonts = source / 'fonts' / 'NotoSans'
            fonts.mkdir(parents=True)
            (fonts / 'font.ttf').write_bytes(b'font-fixture')
            (source / 'meson.build').write_text(
                "project('font-install-test')\n"
                "python = import('python').find_installation('python3')\n"
                "cdata = configuration_data()\n"
                "cdata.set('GEANY_DATA_DIR', get_option('prefix') / 'data')\n" + declaration + '\n')
            prefix = root / 'installed'
            build = root / 'build'
            subprocess.run(command + ['setup', '--backend=none', str(build), str(source),
                                     '--prefix=' + str(prefix)], check=True, capture_output=True)
            install = command + ['install', '-C', str(build), '--no-rebuild']
            subprocess.run(install, check=True, capture_output=True)
            target = prefix / 'data' / 'fonts' / 'NotoSans' / 'font.ttf'
            stamp = target.stat().st_mtime_ns
            subprocess.run(install, check=True, capture_output=True)
            self.assertEqual(target.stat().st_mtime_ns, stamp)
            stage = root / 'stage'
            subprocess.run(install, check=True, capture_output=True,
                           env=dict(os.environ, DESTDIR=str(stage)))
            self.assertEqual((stage / target.relative_to(target.anchor)).read_bytes(), b'font-fixture')

    # EN: Keep staging inside its root and reject relative installation paths before writing files.
    # RU: Сохраняет установку внутри промежуточного корня и отклоняет относительные пути до записи файлов.
    def test_staging(self):
        root = Path.cwd()
        destination = root / 'prefix' / 'data' / 'fonts'
        stage = root / 'stage'
        expected = stage / destination.relative_to(destination.anchor)
        self.assertEqual(FONTS.staged_destination(destination, str(stage)), expected)
        self.assertEqual(FONTS.staged_destination(destination, ''), destination)
        with self.assertRaises(ValueError):
            FONTS.staged_destination(Path('relative/fonts'), '')


if __name__ == '__main__':
    unittest.main()
