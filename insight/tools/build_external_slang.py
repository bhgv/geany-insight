#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare and build the pinned C++17 slang adapter using CMake >= 3.10.

Works with Python 3.6+. Does not enable a Verilog backend in the editor yet.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PIN = '7efcca2e348ecf1e01d1859f7ea85d6202fc9f91'  # slang v3.0 (last C++17 major)
DEPS = {
    'fmt': ('https://github.com/fmtlib/fmt.git', 'a33701196adfad74917046096bf5a2aa0ab0bb50'),
    'unordered_dense': ('https://github.com/martinus/unordered_dense.git', 'a7ece061e831e0b9f954ded1b25e06a5f9e3287c'),
    'filesystem': ('https://github.com/gulrak/filesystem.git', '8a2edd6d92ed820521d42c94d179462bf06b5ed3'),
}


def run(command, **kwargs):
    print('+ ' + ' '.join(str(x) for x in command), flush=True)
    subprocess.check_call([str(x) for x in command], **kwargs)


def output(command):
    return subprocess.check_output([str(x) for x in command], universal_newlines=True).strip()


def checkout(path, url, revision):
    if not (path / '.git').exists():
        if path.exists() and any(path.iterdir()):
            raise ValueError('Refusing to overwrite nonempty dependency: ' + str(path))
        path.mkdir(parents=True, exist_ok=True)
        run(['git', 'init', path])
        run(['git', '-C', path, 'remote', 'add', 'origin', url])
        run(['git', '-C', path, 'fetch', '--depth', '1', 'origin', revision])
        run(['git', '-C', path, 'checkout', '--detach', 'FETCH_HEAD'])
    if output(['git', '-C', path, 'rev-parse', 'HEAD']) != revision:
        raise ValueError('Unexpected dependency revision (not reset): ' + str(path))


def prepare(source, deps):
    if not (source / 'CMakeLists.txt').is_file():
        if source.exists() and any(source.iterdir()):
            raise ValueError('Incomplete nonempty slang checkout; refusing to overwrite it')
        run(['git', '-C', ROOT, 'submodule', 'update', '--init', '--depth', '1', '--', 'external/slang'])
    if output(['git', '-C', source, 'rev-parse', 'HEAD']) != PIN:
        raise ValueError('slang revision differs from pinned v3.0; no checkout was reset')
    for name, (url, revision) in DEPS.items():
        checkout(deps / name, url, revision)
    patch = ROOT / 'external/slang-compat/cmake310-gcc7.patch'
    command = ['git', '-C', source, 'apply']
    quiet = dict(stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    if subprocess.call([str(x) for x in command + ['--reverse', '--check', patch]], **quiet) == 0:
        return
    check = subprocess.run([str(x) for x in command + ['--check', patch]], **quiet)
    if check.returncode:
        raise ValueError('Compatibility patch conflicts with local slang edits; nothing patched: ' + check.stderr)
    run(command + [patch])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / '_external-slang')
    parser.add_argument('--deps', type=Path, default=ROOT / 'external/slang-deps')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--check', action='store_true', help='Run the native C++ multi-file elaboration probe')
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    try:
        generator = 'Ninja' if shutil.which('ninja') else 'Unix Makefiles'
        build_tool = 'ninja' if generator == 'Ninja' else 'make'
        for tool in ('git',) if args.prepare_only else ('git', 'cmake', build_tool, os.environ.get('CXX', 'c++')):
            if not shutil.which(tool):
                raise ValueError('Missing tool: ' + tool)
        source = ROOT / 'external/slang'
        deps, build = args.deps.resolve(), args.build.resolve()
        if build == ROOT or build == source or source in build.parents or deps == source:
            raise ValueError('Build and dependency directories must be separate from source files')
        prepare(source, deps)
        if args.prepare_only:
            return 0
        build.mkdir(parents=True, exist_ok=True)
        cache = build / 'CMakeCache.txt'
        if cache.is_file():
            for line in cache.read_text().splitlines():
                if line.startswith('CMAKE_GENERATOR:INTERNAL='):
                    generator = line.split('=', 1)[1]
        run(['cmake', '-G', generator, '-H' + str(ROOT / 'external/slang-compat'),
             '-B' + str(build), '-DGEANY_SLANG_SOURCE=' + str(source),
             '-DGEANY_SLANG_DEPS=' + str(deps), '-DCMAKE_BUILD_TYPE=Release',
             '-DPYTHON_EXECUTABLE=' + sys.executable])
        run(['cmake', '--build', build, '--', '-j' + str(args.jobs)])
        if args.check:
            run(['ctest', '--output-on-failure'], cwd=str(build))
        manifest = {'revision': PIN, 'source': str(source), 'dependencies': DEPS,
                    'cmake': output(['cmake', '--version']).splitlines()[0],
                    'compiler': os.environ.get('CXX', 'c++'), 'deps_dir': str(deps),
                    'generator': generator, 'editor_backend_enabled': False}
        (build / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('slang compatibility library and probe ready: ' + str(build))
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        print('slang compatibility build stopped: ' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
