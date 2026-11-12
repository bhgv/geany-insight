#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Install bundled fonts without overwriting identical files that Windows may have loaded.
# RU: Устанавливает комплект шрифтов без перезаписи одинаковых файлов, загруженных Windows.
import argparse
import filecmp
import os
from pathlib import Path
import shutil
import sys
import tempfile


# EN: Resolve absolute destination under optional DESTDIR staging, stripping its drive/root once.
# RU: Размещает абсолютный destination внутри необязательного DESTDIR, однократно убирая диск/корень.
def staged_destination(destination, staging):
    if not destination.is_absolute():
        raise ValueError('Font destination must be absolute: ' + str(destination))
    return Path(staging) / destination.relative_to(destination.anchor) if staging else destination


# EN: Copy source's files into destination atomically; retain identical files and report paths/errors without deleting old fonts.
# RU: Атомарно копирует файлы source в destination; сохраняет одинаковые файлы и сообщает пути/ошибки без удаления старых шрифтов.
def install(source, destination):
    if not source.is_dir():
        raise ValueError('Font source directory is missing: ' + str(source))
    for original in sorted(source.rglob('*')):
        if not original.is_file():
            continue
        target = destination / original.relative_to(source)
        temporary = None
        try:
            if target.is_file() and filecmp.cmp(original, target, shallow=False):
                print('Font unchanged: ' + str(target), flush=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            descriptor, name = tempfile.mkstemp(prefix='.insight-font-', dir=target.parent)
            os.close(descriptor)
            temporary = Path(name)
            shutil.copyfile(original, temporary)
            os.replace(temporary, target)
            print('Font installed: ' + str(target), flush=True)
        except OSError as error:
            raise OSError(
                f'Cannot install bundled font file {original} -> {target}: {error}. '
                'Close running Geany Insight instances and retry; also check destination permissions '
                'and free disk space. Existing font files have not been deleted.') from error
        finally:
            if temporary and temporary.exists():
                temporary.unlink()


# EN: Run Meson's font installation from argv with DESTDIR support; print actionable errors and return a nonzero failure status.
# RU: Выполняет установку шрифтов Meson по argv с DESTDIR; печатает диагностику и возвращает ненулевой код при ошибке.
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--destination', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        install(args.source, staged_destination(args.destination, os.environ.get('DESTDIR', '')))
    except (OSError, ValueError) as error:
        print('Font installation failed: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
