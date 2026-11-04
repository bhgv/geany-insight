#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Stage the prepared slang runtime and a self-contained static C++ SDK."""
import argparse
import json
from pathlib import Path
import shutil
import sys

sys.dont_write_bytecode = True
from build_external_slang import PIN


def merge(source, destination):
    for path in source.rglob('*'):
        if path.is_file():
            target = destination / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(str(path), str(target))


def stage(build, prefix):
    build, prefix = Path(build).resolve(), Path(prefix).resolve()
    info = json.loads((build / 'source-manifest.json').read_text())
    if info.get('revision') != PIN:
        raise ValueError('slang build revision differs from the pinned adapter')
    source, deps = Path(info['source']), Path(info['deps_dir'])
    binary = build / 'slang/bin' / ('slang.exe' if sys.platform == 'win32' else 'slang')
    semantic = build / ('insight-slang-semantic.exe' if sys.platform == 'win32' else 'insight-slang-semantic')
    libraries = [build / 'slang/lib/libsvlang.a', build / 'slang/lib/libfmt.a']
    headers = [(source / 'include/slang', 'slang'), (build / 'slang/source/slang', 'slang'),
               (deps / 'fmt/include/fmt', 'fmt'), (deps / 'unordered_dense/include/ankerl', 'ankerl'),
               (deps / 'filesystem/include/ghc', 'ghc')]
    licenses = [(source / 'LICENSE', 'slang.txt'), (deps / 'fmt/LICENSE.rst', 'fmt.txt'),
                (deps / 'filesystem/LICENSE', 'filesystem.txt'),
                (deps / 'unordered_dense/LICENSE', 'unordered-dense.txt')]
    # Validate all inputs before writing a partial installation.
    required = [binary, semantic] + libraries + [p for p, _ in licenses] + [source / 'external/span.hpp', source / 'external/flat_hash_map.hpp']
    if not all(p.is_file() for p in required) or not all(p.is_dir() for p, _ in headers):
        raise ValueError('Incomplete slang build; run build_external_slang.py first')
    runtime = prefix / 'libexec/geany-insight/slang'
    sdk = prefix / 'lib/geany-insight/slang'
    runtime.mkdir(parents=True, exist_ok=True)
    (sdk / 'lib/pkgconfig').mkdir(parents=True, exist_ok=True)
    shutil.copy2(str(binary), str(runtime / binary.name))
    shutil.copy2(str(semantic), str(runtime / semantic.name))
    for lib in libraries:
        shutil.copy2(str(lib), str(sdk / 'lib' / lib.name))
    for folder, relative in headers:
        merge(folder, sdk / 'include' / relative)
    for name in ('span.hpp', 'flat_hash_map.hpp'):
        shutil.copy2(str(source / 'external' / name), str(sdk / 'include' / name))
    license_dir = sdk / 'licenses'
    license_dir.mkdir(exist_ok=True)
    if (source / 'LICENSES').is_dir():
        merge(source / 'LICENSES', license_dir / 'slang-supplemental')
    for path, name in licenses:
        shutil.copy2(str(path), str(license_dir / name))
    (sdk / 'source-manifest.json').write_text(json.dumps(info, indent=2) + '\n')
    (sdk / 'lib/pkgconfig/slang-compat.pc').write_text('''prefix=${pcfiledir}/../..
libdir=${prefix}/lib
includedir=${prefix}/include
Name: slang-compat
Description: Geany Insight adapted slang C++17 static SDK
Version: 3.0.0
Cflags: -I"${includedir}" -DSLANG_STATIC_DEFINE
Libs: -L"${libdir}" -lsvlang -lfmt -pthread
''')
    print('slang runtime:', runtime)
    print('slang static SDK:', sdk)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--prefix', type=Path, required=True)
    a = p.parse_args()
    try:
        stage(a.build, a.prefix)
    except (ValueError, OSError, KeyError) as exc:
        p.exit(1, 'slang installation stopped: ' + str(exc) + '\n')


if __name__ == '__main__':
    main()
