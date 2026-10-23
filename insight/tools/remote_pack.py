#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Local archive creation only. Never extracts or launches another process."""
import gzip
import os
from pathlib import Path
import shutil
import sys
import tarfile


def pack(mode, source, destination):
    source = Path(source)
    if source.is_symlink() or not source.exists():
        raise ValueError('Choose an existing regular file or folder, not a symbolic link.')
    if mode not in ('gz', 'tar.gz') or (mode == 'gz' and not source.is_file()):
        raise ValueError('gz accepts one regular file; use tar.gz for folders.')
    # Exclusive creation in the private staging folder; no overwrite of input.
    with open(destination, 'xb') as raw:
        if mode == 'gz':
            with source.open('rb') as data, gzip.GzipFile(filename=source.name, fileobj=raw, mode='wb') as out:
                shutil.copyfileobj(data, out, 1024 * 1024)
        else:
            count = 0
            def checked(info):
                nonlocal count
                count += 1
                if count > 100000 or not (info.isfile() or info.isdir() or info.issym() or info.islnk()):
                    raise ValueError('Archive exceeds 100000 entries or contains a device/socket/FIFO.')
                return info
            with tarfile.open(fileobj=raw, mode='w:gz', dereference=False) as archive:
                archive.add(str(source), arcname=source.name, filter=checked)


if __name__ == '__main__':
    try:
        if len(sys.argv) != 4:
            raise ValueError('Expected mode, source and archive path.')
        pack(*sys.argv[1:])
    except (OSError, ValueError, tarfile.TarError) as exc:
        print(str(exc)[:1024], file=sys.stderr)
        sys.exit(1)
