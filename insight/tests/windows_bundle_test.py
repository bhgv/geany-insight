#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise portable Git exports and offline asset checks without downloading toolchains.
# RU: Проверяет перенос Git-исходников и локальные файлы без загрузки инструментов.
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import windows_bundle as bundle


# EN: Remove read-only Git objects in Windows test fixtures, retrying only permission failures.
# RU: Удаляет защищённые от записи Git-объекты тестов Windows, повторяя только ошибки доступа.
def remove_fixture(path):
    def retry(function, filename, error):
        if not isinstance(error[1], PermissionError):
            raise error[1]
        os.chmod(filename, stat.S_IWRITE | stat.S_IREAD)
        function(filename)

    shutil.rmtree(path, onerror=retry)


# EN: Create a committed repository at path and return its revision.
# RU: Создаёт репозиторий с коммитом в path и возвращает его ревизию.
def repository(path):
    path.mkdir(parents=True)
    subprocess.check_call(['git', 'init', '-q', str(path)])
    (path / 'source.txt').write_text('source\n', encoding='utf-8')
    subprocess.check_call(['git', '-C', str(path), 'add', 'source.txt'])
    subprocess.check_call(['git', '-C', str(path), '-c', 'user.name=Test',
                           '-c', 'user.email=test@example.org', 'commit', '-qm', 'fixture'])
    return bundle.revision(path)


class OfflineBundle(unittest.TestCase):
    # EN: Set up a temporary root containing spaces for each test.
    # RU: Создаёт временный корень с пробелами для каждого теста.
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='offline bundle ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    # EN: Reject hidden control bytes that can corrupt paths in bootstrap scripts.
    # RU: Отклоняет скрытые управляющие байты, повреждающие пути скриптов сборки.
    def test_bootstrap_scripts_have_no_hidden_control_bytes(self):
        tools = Path(__file__).resolve().parents[1] / 'tools'
        for path in sorted(tools.glob('*.ps1')):
            with self.subTest(script=path.name):
                invalid = [(offset, byte) for offset, byte in enumerate(path.read_bytes())
                           if byte < 32 and byte not in (9, 10, 13)]
                self.assertEqual(invalid, [])

    # EN: Ignore any native tar earlier in PATH and pair MSYS tar with POSIX filenames.
    # RU: Выбирает MSYS tar независимо от PATH и передаёт ему имена в формате POSIX.
    def test_windows_archive_selects_matching_tar_and_paths(self):
        converter = 'C:/tools/msys64/usr/bin/cygpath.exe'
        expected_tar = 'C:/tools/msys64/usr/bin/tar.exe'
        with patch.object(bundle.shutil, 'which', return_value=converter), patch.object(
                bundle.subprocess, 'check_output',
                side_effect=[expected_tar, '/c/transfer/source.tar.gz', '/tmp/source']), patch.object(
                bundle, 'run') as run:
            bundle.archive_sources(Path('C:/temporary/source'), Path('C:/transfer/source.tar.gz'))
            run.assert_called_once_with([
                expected_tar, '-czf', '/c/transfer/source.tar.gz', '-C', '/tmp/source', '.'])

    # EN: Prove an exported nested repo still works after the original checkout disappears.
    # RU: Проверяет вложенный репозиторий после удаления исходного рабочего дерева.
    def test_git_export_survives_relocation(self):
        donor = self.root / 'donor'
        expected = repository(donor)
        outer = self.root / 'export'
        repository(outer)
        target = outer / 'external/dependency'
        bundle.clone(donor, target)
        remove_fixture(donor)
        moved = self.root / 'moved'
        outer.rename(moved)
        exported = moved / 'external/dependency'
        self.assertEqual(bundle.revision(exported), expected)
        self.assertTrue((exported / '.git').is_dir())
        self.assertFalse((exported / '.git/objects/info/alternates').exists())

    # EN: Validate all pins and archive digests, including missing and corrupted inputs.
    # RU: Проверяет ревизии и суммы архивов, включая отсутствующие и повреждённые файлы.
    def test_offline_validation_rejects_missing_and_corrupt_assets(self):
        source = self.root / 'source'
        cache = self.root / 'cache'
        pin = repository(source / 'external/slang')
        dep_pin = repository(source / 'external/slang-deps/dep')
        cache.mkdir()
        archive = cache / 'iverilog-13.0.tar.gz'
        archive.write_bytes(b'verified fixture')
        metadata = source / 'insight/tools/windows-toolchain.json'
        metadata.parent.mkdir(parents=True)
        metadata.write_text(json.dumps({'iverilog': {
            'version': '13.0', 'sha256': bundle.digest(archive)}}))
        with patch.object(bundle, 'SLANG_PIN', pin), patch.object(
                bundle, 'DEPS', {'dep': ('unused', dep_pin)}):
            self.assertEqual(bundle.validate(source, cache, 'disabled'), archive)
            archive.write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'simulator archive'):
                bundle.validate(source, cache, 'disabled')
            with self.assertRaisesRegex(ValueError, 'llvm-project'):
                bundle.validate(source, cache, 'internal')
            remove_fixture(source / 'external/slang-deps/dep')
            with self.assertRaisesRegex(ValueError, 'offline dependency'):
                bundle.validate(source, cache, 'disabled')

    # EN: A failed download must not publish an archive or leave its partial file.
    # RU: Неудачная загрузка не должна оставлять готовый архив либо временный файл.
    def test_corrupt_download_is_not_published(self):
        metadata = self.root / 'insight/tools/windows-toolchain.json'
        metadata.parent.mkdir(parents=True)
        metadata.write_text(json.dumps({'iverilog': {
            'version': '13.0', 'sha256': hashlib.sha256(b'valid').hexdigest(),
            'url': 'https://example.invalid/archive'}}))

        def download(command):
            Path(command[command.index('--output') + 1]).write_bytes(b'bad')

        with patch.object(bundle, 'ROOT', self.root), patch.object(bundle, 'run', download):
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                bundle.simulator_archive(self.root / 'cache')
        self.assertEqual(list((self.root / 'cache').iterdir()), [])

    # EN: Export, move and validate a complete miniature source bundle with network Git disabled.
    # RU: Экспортирует, переносит и проверяет малый набор исходников с запретом сетевых протоколов Git.
    def test_complete_source_export_can_build_from_local_assets(self):
        source = self.root / 'source'
        repository(source)
        pin = repository(source / 'external/slang')
        dep_pin = repository(source / 'external/slang-deps/dep')
        cache = self.root / 'cache'
        cache.mkdir()
        archive = cache / 'iverilog-13.0.tar.gz'
        archive.write_bytes(b'verified fixture')
        metadata = source / 'insight/tools/windows-toolchain.json'
        metadata.parent.mkdir(parents=True)
        (source / 'tracked.bin').write_bytes(bytes(range(256)))
        (source / 'deleted.c').write_text('int removed;')
        (source / '.gitignore').write_text('generated.obj')
        metadata.write_text(json.dumps({
            'repository': 'https://example.invalid/editor.git',
            'iverilog': {'version': '13.0', 'sha256': bundle.digest(archive)}}))
        subprocess.check_call(['git', '-C', str(source), 'add', 'insight', 'tracked.bin', 'deleted.c', '.gitignore'])
        subprocess.check_call(['git', '-C', str(source), '-c', 'user.name=Test',
                               '-c', 'user.email=test@example.org', 'commit', '-qm', 'metadata'])
        output = self.root / 'usb bundle'
        output.mkdir()
        with patch.object(bundle, 'ROOT', source), patch.object(bundle, 'SLANG_PIN', pin), patch.object(
                bundle, 'DEPS', {'dep': ('unused', dep_pin)}), patch.object(
                bundle, 'prepare_slang'), patch.dict(os.environ, {'GIT_ALLOW_PROTOCOL': 'file'}):
            bundle.prepare(output, cache, 'disabled')
        remove_fixture(source)
        restored = self.root / 'restored source'
        restored.mkdir()
        subprocess.check_call(['tar', '-xzf', str(output / 'sources.tar.gz'), '-C', str(restored)])
        with patch.object(bundle, 'SLANG_PIN', pin), patch.object(
                bundle, 'DEPS', {'dep': ('unused', dep_pin)}):
            self.assertEqual(bundle.validate(restored, output, 'disabled'), output / archive.name)

    # EN: Confirm cache reuse works without invoking a network command.
    # RU: Проверяет использование готового кэша без сетевой команды.
    def test_verified_cache_never_downloads(self):
        metadata = self.root / 'insight/tools/windows-toolchain.json'
        metadata.parent.mkdir(parents=True)
        archive = self.root / 'cache/iverilog-13.0.tar.gz'
        archive.parent.mkdir()
        archive.write_bytes(b'valid')
        metadata.write_text(json.dumps({'iverilog': {
            'version': '13.0', 'sha256': bundle.digest(archive)}}))
        with patch.object(bundle, 'ROOT', self.root), patch.object(bundle, 'run') as run:
            self.assertEqual(bundle.simulator_archive(archive.parent), archive)
            run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
