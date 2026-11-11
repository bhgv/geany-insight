#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Collect pinned editor dependencies and standalone Git sources for offline Windows builds.
# RU: Собирает закреплённые зависимости и автономные Git-исходники для сборки Windows без сети.
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

from build_external_slang import DEPS, PIN as SLANG_PIN, prepare as prepare_slang
from build_external_clang import PIN as CLANG_PIN

ROOT = Path(__file__).resolve().parents[2]


# EN: Run command visibly, raising on a failed child.
# RU: Показывает и выполняет command, сообщая об ошибке дочернего процесса.
def run(command):
    print('+ ' + ' '.join(str(part) for part in command), flush=True)
    subprocess.check_call([str(part) for part in command])


# EN: Return the SHA-256 of path using bounded memory.
# RU: Возвращает SHA-256 файла path с ограниченным расходом памяти.
def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


# EN: Read the checked-out Git revision at source.
# RU: Читает текущую ревизию Git в source.
def revision(source):
    return subprocess.check_output(
        ['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()


# EN: Check every required local dependency in source and the pinned archive in cache.
# RU: Проверяет локальные зависимости в source и закреплённый архив в cache.
def validate(source, cache, provider):
    paths = [(source / 'external/slang', SLANG_PIN)]
    paths.extend((source / 'external/slang-deps' / name, pin) for name, (_, pin) in DEPS.items())
    if provider == 'internal':
        paths.append((source / 'external/llvm-project', CLANG_PIN))
    for path, expected in paths:
        if not (path / '.git').exists() or revision(path) != expected:
            raise ValueError('Missing or incompatible offline dependency: ' + str(path))
    manifest = json.loads((source / 'insight/tools/windows-toolchain.json').read_text(encoding='utf-8'))
    simulator = manifest['iverilog']
    archive = cache / ('iverilog-' + simulator['version'] + '.tar.gz')
    if not archive.is_file() or digest(archive) != simulator['sha256']:
        raise ValueError('Missing or corrupt offline simulator archive: ' + str(archive))
    return archive


# EN: Clone the exact committed source into destination with independent Git objects.
# RU: Копирует точную сохранённую ревизию source в destination с независимыми объектами Git.
def clone(source, destination):
    if destination.is_dir() and not any(destination.iterdir()):
        destination.rmdir()
    run(['git', '-c', 'core.autocrlf=false', 'clone', '--no-hardlinks', '--no-checkout',
         source, destination])
    run(['git', '-C', destination, 'config', 'core.autocrlf', 'false'])
    run(['git', '-C', destination, 'checkout', '--detach', revision(source)])


# EN: Copy tracked edits and nonignored new files from source to destination, excluding dependency roots.
# RU: Копирует правки и новые неигнорируемые файлы из source в destination, исключая корни зависимостей.
def snapshot_changes(source, destination, excluded=()):
    changes = subprocess.check_output([
        'git', '-C', str(source), 'diff', '--binary', '--no-ext-diff', '--no-textconv',
        '--ignore-submodules=all', 'HEAD', '--', '.'])
    if changes:
        with tempfile.TemporaryDirectory(prefix='insight-edits-') as directory:
            patch = Path(directory) / 'working-tree.patch'
            patch.write_bytes(changes)
            run(['git', '-c', 'core.autocrlf=false', '-C', destination,
                 'apply', '--binary', '--whitespace=nowarn', patch])
    untracked = subprocess.check_output([
        'git', '-C', str(source), 'ls-files', '--others', '--exclude-standard', '-z'])
    for entry in untracked.split(bytes([0])):
        if not entry:
            continue
        relative = entry.decode('utf-8')
        if any(relative == root or relative.startswith(root + '/') for root in excluded):
            continue
        original = source / relative
        if original.is_file() or original.is_symlink():
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, target, follow_symlinks=False)


# EN: Download the pinned simulator into cache and verify before publishing the archive.
# RU: Загружает закреплённый симулятор в cache и проверяет архив до сохранения.
def simulator_archive(cache):
    manifest = json.loads((ROOT / 'insight/tools/windows-toolchain.json').read_text(encoding='utf-8'))
    simulator = manifest['iverilog']
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / ('iverilog-' + simulator['version'] + '.tar.gz')
    if archive.is_file():
        if digest(archive) != simulator['sha256']:
            raise ValueError('Cached simulator checksum mismatch: ' + str(archive))
        return archive
    temporary = archive.with_suffix('.partial')
    command = ['curl', '--disable', '--fail', '--location', '--retry', '3',
               '--proto', '=https', '--output', temporary, simulator['url']]
    if os.environ.get('INSIGHT_MSYS_SKIP_TLS_VERIFY') == '1':
        command.insert(2, '--insecure')
    try:
        run(command)
        if digest(temporary) != simulator['sha256']:
            raise ValueError('Downloaded simulator checksum mismatch')
        temporary.replace(archive)
    finally:
        if temporary.exists():
            temporary.unlink()
    return archive


# EN: Collect downloads into bundle; export source repos with no absolute submodule links.
# RU: Собирает загрузки в bundle, экспортируя репозитории без абсолютных ссылок подмодулей.
def prepare(bundle, cache, provider):
    prepare_slang(ROOT / 'external/slang', ROOT / 'external/slang-deps')
    archive = simulator_archive(cache)
    validate(ROOT, cache, provider)
    shutil.copy2(archive, bundle / archive.name)
    with tempfile.TemporaryDirectory(prefix='insight-bundle-') as temporary:
        exported = Path(temporary) / 'source'
        clone(ROOT, exported)
        snapshot_changes(ROOT, exported, (
            'external/slang', 'external/slang-deps', 'external/llvm-project'))
        manifest = json.loads((ROOT / 'insight/tools/windows-toolchain.json').read_text(encoding='utf-8'))
        run(['git', '-C', exported, 'remote', 'set-url', 'origin', manifest['repository']])
        dependencies = ['external/slang'] + ['external/slang-deps/' + name for name in DEPS]
        if provider == 'internal':
            dependencies.append('external/llvm-project')
        for relative in dependencies:
            clone(ROOT / relative, exported / relative)
            snapshot_changes(ROOT / relative, exported / relative)
        validate(exported, cache, provider)
        # EN: MSYS tar expects POSIX paths when launched by native UCRT Python.
        # RU: MSYS tar ожидает POSIX-пути при запуске из родного UCRT Python.
        paths = []
        for path in (bundle / 'sources.tar.gz', exported):
            if shutil.which('cygpath'):
                paths.append(subprocess.check_output(['cygpath', '-u', str(path)], text=True).strip())
            else:
                paths.append(str(path))
        run(['tar', '-czf', paths[0], '-C', paths[1], '.'])


# EN: Parse argv and prepare or validate a bundle, returning a shell exit status.
# RU: Разбирает argv, готовит или проверяет набор, возвращая код завершения оболочки.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'validate'))
    parser.add_argument('--bundle', type=Path)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--provider', choices=('internal', 'system', 'disabled'), required=True)
    args = parser.parse_args(argv)
    if args.action == 'prepare' and args.bundle is None:
        parser.error('--bundle is required for preparation')
    try:
        if args.action == 'prepare':
            prepare(args.bundle.resolve(), args.cache.resolve(), args.provider)
        else:
            validate(ROOT, args.cache.resolve(), args.provider)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('Offline bundle: ' + str(error), flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
