#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Check incremental font installation, staging and preservation when replacement fails.
# RU: Проверяет повторную установку шрифтов, промежуточный каталог и сохранность при ошибке замены.
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
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
