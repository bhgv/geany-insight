#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Automatically prepare the internal SDK; print its four resolved paths."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
from build_external_clang import PIN
from select_clang_sdk import resolve

ROOT = Path(__file__).resolve().parents[2]


def prepare(sdk, jobs=2, force=False, root=ROOT, run=subprocess.run):
    if jobs < 1:
        raise ValueError('Compiler jobs must be positive')
    sdk = Path(sdk).resolve()
    try:
        paths = resolve(sdk)
    except ValueError:
        paths = None
    source = root / 'external/llvm-project'
    changed = False
    if paths and not force and (source / '.git').exists():
        # Do not use a valid but stale SDK after local compiler edits. Never
        # reset those edits or change an already initialized source checkout.
        revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
        changed = revision != PIN or bool(subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain']))
        info = json.loads((sdk / 'source-manifest.json').read_text(encoding='utf-8'))
        if info.get('source') and not isinstance(info['source'], str):
            raise ValueError('Invalid source path in SDK manifest')
        if info.get('source') and Path(info['source']).resolve() != source.resolve():
            return paths  # A deliberately supplied SDK belongs to another checkout.
        changed = changed or bool(info.get('locally_modified'))
    if paths and not force and not changed:
        return paths
    manifest = sdk / 'source-manifest.json'
    if manifest.exists():
        try:
            info = json.loads(manifest.read_text(encoding='utf-8'))
        except (OSError, ValueError) as exc:
            raise ValueError('Refusing to replace an invalid SDK manifest; choose another SDK directory') from exc
        if not isinstance(info, dict) or info.get('revision') != PIN:
            raise ValueError('Refusing to replace an SDK from another revision; choose another SDK directory')
    elif sdk.exists() and any(sdk.iterdir()):
        raise ValueError('Refusing to overwrite a nonempty directory without an internal SDK manifest: ' + str(sdk))
    initialized = (source / 'llvm/CMakeLists.txt').is_file() and (source / 'clang/CMakeLists.txt').is_file()
    if not initialized and source.exists() and any(source.iterdir()):
        raise ValueError('Compiler source directory is nonempty but incomplete; refusing to replace it')
    missing = [name for name in ('git', 'cmake', 'ninja') if not shutil.which(name)]
    if missing:
        raise ValueError('Internal Clang preparation needs these build tools: ' + ', '.join(missing) + '. No system Clang installation is required.')
    def execute(command):
        print('Preparing internal Clang: ' + ' '.join(str(x) for x in command), file=sys.stderr, flush=True)
        run([str(x) for x in command], cwd=root, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'),
            stdout=sys.stderr, check=True)
    if not initialized:
        execute(['git', 'submodule', 'update', '--init', '--depth', '1', '--', 'external/llvm-project'])
    execute([sys.executable, root / 'insight/tools/build_external_clang.py', '--sdk', sdk, '--jobs', jobs])
    return resolve(sdk)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--force-build', action='store_true', help='Run the incremental compiler build even for a valid SDK')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    try:
        paths = prepare(args.sdk, args.jobs, args.force_build)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, str(exc) + '\nInternal Clang preparation stopped; system Clang was not selected.\n')
    print('\n'.join(str(p) for p in paths))


if __name__ == '__main__':
    main()
