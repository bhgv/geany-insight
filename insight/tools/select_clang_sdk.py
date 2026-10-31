#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Resolve only an explicitly staged internal SDK; never search system paths."""
import argparse
import json
from pathlib import Path
from build_external_clang import PIN


def resolve(sdk):
    sdk = Path(sdk).resolve()
    if '\n' in str(sdk) or '\r' in str(sdk):
        raise ValueError('SDK path must not contain line breaks')
    try:
        manifest = json.loads((sdk / 'source-manifest.json').read_text(encoding='utf-8'))
    except (OSError, ValueError) as exc:
        raise ValueError('Missing or invalid internal Clang source manifest') from exc
    if not isinstance(manifest, dict):
        raise ValueError('Invalid internal Clang source manifest')
    if manifest.get('revision') != PIN:
        raise ValueError('Internal Clang SDK revision does not match external/llvm-project')
    for header in ('clang/Tooling/Tooling.h', 'llvm/Config/llvm-config.h'):
        if not (sdk / 'include' / header).is_file():
            raise ValueError('Incomplete internal Clang SDK: missing ' + header)
    def first(folder, names):
        for name in names:
            path = sdk / folder / name
            if path.is_file():
                return path
        raise ValueError('Incomplete internal Clang SDK: missing ' + names[0])
    cpp = first('lib', ('libclang-cpp.so', 'libclang-cpp.dylib', 'libclang-cpp.dll.a', 'clang-cpp.lib'))
    llvm = first('lib', ('libLLVM.so', 'libLLVM.dylib', 'libLLVM.dll.a', 'LLVM.lib'))
    config = first('bin', ('llvm-config', 'llvm-config.exe'))
    return sdk, cpp, llvm, config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True)
    args = parser.parse_args()
    try:
        paths = resolve(args.sdk)
    except ValueError as exc:
        parser.exit(1, str(exc) + '\nBuild it first: git submodule update --init --depth 1 external/llvm-project\n'
                    'python3 insight/tools/build_external_clang.py\n'
                    'For a system compiler, explicitly select clang-provider=system.\n')
    print('\n'.join(str(p) for p in paths))


if __name__ == '__main__':
    main()
