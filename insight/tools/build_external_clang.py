#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the pinned, editable external LLVM/Clang source into a local editor SDK."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

PIN = '3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff'
ROOT = Path(__file__).resolve().parents[2]


def run(args):
    subprocess.run([str(a) for a in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'external/llvm-project')
    parser.add_argument('--build', type=Path, default=ROOT / '_external-clang')
    parser.add_argument('--sdk', type=Path, default=ROOT / '_external-clang-sdk')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--targets', default={'x86_64': 'X86', 'AMD64': 'X86', 'aarch64': 'AArch64', 'arm64': 'AArch64'}.get(platform.machine(), 'X86'))
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--reuse-build', action='store_true', help='Stage an already compiled, trusted CI build cache from a clean pinned checkout')
    parser.add_argument('--plan', action='store_true', help='Print the exact build commands without running CMake')
    args = parser.parse_args()
    source, build, sdk = (p.resolve() for p in (args.source, args.build, args.sdk))
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if not (source / 'llvm/CMakeLists.txt').is_file() or not (source / 'clang/CMakeLists.txt').is_file():
        parser.error('Missing sources: git submodule update --init --depth 1 external/llvm-project')
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != PIN:
        parser.error('External LLVM revision differs from the pinned source; update this build integration deliberately')
    # Local source edits are intentional. Never reset, clean or download over them.
    configure = ['cmake', '-G', 'Ninja', '-S', str(source / 'llvm'), '-B', str(build),
                 '-DCMAKE_BUILD_TYPE=Release', '-DLLVM_ENABLE_PROJECTS=clang',
                 '-DLLVM_TARGETS_TO_BUILD=' + args.targets,
                 '-DLLVM_BUILD_LLVM_DYLIB=ON', '-DLLVM_LINK_LLVM_DYLIB=ON',
                 '-DCLANG_LINK_CLANG_DYLIB=ON', '-DLLVM_INCLUDE_TESTS=OFF',
                 '-DCLANG_INCLUDE_TESTS=OFF', '-DLLVM_INCLUDE_EXAMPLES=OFF',
                 '-DLLVM_INCLUDE_BENCHMARKS=OFF', '-DLLVM_ENABLE_BINDINGS=OFF',
                 '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF',
                 '-DLLVM_ENABLE_TERMINFO=OFF', '-DLLVM_ENABLE_LIBXML2=OFF']
    compile_command = ['cmake', '--build', str(build), '--parallel', str(args.jobs),
                       '--target', 'clang-cpp', 'llvm-config', 'clang-resource-headers']
    if args.plan:
        print(json.dumps({'revision': revision, 'configure': configure, 'build': compile_command, 'sdk': str(sdk)}, indent=2))
        return
    shared_suffix = '.dylib' if platform.system() == 'Darwin' else '.so'
    if args.reuse_build:
        if args.configure_only:
            parser.error('--reuse-build and --configure-only cannot be combined')
        dirty = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True)
        if dirty.strip():
            parser.error('Do not reuse a compiler cache with local source edits; rebuild normally')
        required = [build / 'CMakeCache.txt', build / 'bin' / ('llvm-config.exe' if os.name == 'nt' else 'llvm-config')]
        if os.name != 'nt':
            required += [build / ('lib/libclang-cpp' + shared_suffix), build / ('lib/libLLVM' + shared_suffix)]
        if not all(p.is_file() for p in required):
            parser.error('Incomplete compiler cache; rebuild normally')
        print('Reusing pinned source compiler outputs; no source timestamps are used as a cache key')
    else:
        run(configure)
        if args.configure_only:
            return
        run(compile_command)
    include = sdk / 'include'
    for folder in (source / 'llvm/include', build / 'include', source / 'clang/include', build / 'tools/clang/include'):
        if folder.is_dir():
            shutil.copytree(folder, include, dirs_exist_ok=True)
    (sdk / 'bin').mkdir(parents=True, exist_ok=True)
    executable = 'llvm-config.exe' if os.name == 'nt' else 'llvm-config'
    shutil.copy2(build / 'bin' / executable, sdk / 'bin' / executable)
    library = sdk / 'lib'
    if os.name == 'nt':
        shutil.copytree(build / 'lib', library, dirs_exist_ok=True)
        for dll in (build / 'bin').glob('*.dll'):
            shutil.copy2(dll, sdk / 'bin' / dll.name)
    elif not library.exists():
        library.symlink_to(build / 'lib', target_is_directory=True)
    elif library.resolve() != (build / 'lib').resolve():
        parser.error('SDK lib already points elsewhere; choose another SDK directory')
    # LLVM may use libLLVM.so.18.1 or a libLLVM-18.so name, depending on
    # its shared-library configuration. Prefer the generated development link.
    if os.name != 'nt' and not (library / ('libLLVM' + shared_suffix)).exists():
        patterns = ('libLLVM*.dylib',) if shared_suffix == '.dylib' else ('libLLVM.so.*', 'libLLVM-[0-9]*.so*')
        candidates = sorted(p for pattern in patterns for p in library.glob(pattern) if p.is_file())
        if not candidates:
            parser.error('Source build is missing the LLVM shared library')
        alias = library / ('libLLVM' + shared_suffix)
        if alias.is_symlink():
            parser.error('SDK contains a broken LLVM development link; choose another SDK directory')
        alias.symlink_to(candidates[0].name)
    for base in (sdk, build):
        licenses = base / 'share/licenses/llvm'
        licenses.mkdir(parents=True, exist_ok=True)
        for project in ('llvm', 'clang'):
            shutil.copy2(source / project / 'LICENSE.TXT', licenses / (project + '-LICENSE.TXT'))
    changes = subprocess.check_output(['git', '-C', str(source), 'diff', '--binary', 'HEAD'])
    (sdk / 'source-manifest.json').write_text(json.dumps({'revision': revision, 'source': str(source), 'locally_modified': bool(changes)}, indent=2))
    print('SDK ready:', sdk)
    print('Editor: meson setup build-editor -Dclang-sdk=' + str(sdk) + ' -Dclang=enabled -Dclang-cpp=enabled -Dclang-bundle=true')


if __name__ == '__main__':
    main()
