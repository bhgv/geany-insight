#!/usr/bin/env python3
"""Bundle the exact linked Clang SDK runtime and resource headers (Meson install)."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-lib', type=Path, required=True)
    parser.add_argument('--libdir', default='lib')
    parser.add_argument('--datadir', default='share/geany')
    parser.add_argument('--resources-only', action='store_true')
    parser.add_argument('--resource-destination', type=Path)
    args = parser.parse_args()
    if args.resource_destination and not args.resources_only:
        parser.error('--resource-destination requires --resources-only')
    prefix = Path('.') if args.resource_destination else Path(os.environ['MESON_INSTALL_DESTDIR_PREFIX'])
    library_dir = args.sdk_lib.resolve()
    resource_roots = [p for p in (library_dir / 'clang').glob('*') if (p / 'include').is_dir()]
    if not resource_roots:
        raise SystemExit('Clang SDK resource headers missing: ' + str(library_dir / 'clang'))
    resource = max(resource_roots, key=lambda p: tuple(int(n) for n in re.findall(r'\d+', p.name)))
    data = args.resource_destination if args.resource_destination else prefix / args.datadir / 'insight/clang'
    shutil.copytree(resource / 'include', data / 'resources/include', dirs_exist_ok=True)
    licenses = data / 'licenses'
    licenses.mkdir(parents=True, exist_ok=True)
    sdk = library_dir.parent
    candidates = list((sdk / 'share/licenses').glob('**/*LICENSE*')) + list(Path('/usr/share/doc').glob('libclang*/copyright')) + list(Path('/usr/share/doc').glob('llvm-*/copyright'))
    for i, source in enumerate(p for p in candidates if p.is_file()):
        shutil.copy2(source, licenses / (str(i) + '-' + source.name))
    if not any(licenses.iterdir()):
        raise SystemExit('Clang/LLVM redistribution license files missing from SDK')
    if args.resources_only:
        return
    if sys.platform == 'win32':
        source = next((p for p in (sdk / 'bin').glob('*.dll') if 'clang-cpp' in p.name.lower()), None)
        if not source:
            raise SystemExit('clang-cpp DLL missing in SDK bin')
        destination = prefix / 'bin'
        destination.mkdir(parents=True, exist_ok=True)
        sdk_bin = {p.name.lower(): p for p in (sdk / 'bin').glob('*.dll')}
        system = Path(os.environ['SystemRoot']) / 'System32'
        system_names = {p.name.lower() for p in system.iterdir() if p.is_file()}
        queue = [source]
        copied = set()
        while queue:
            source = queue.pop()
            if source.name.lower() in copied:
                continue
            copied.add(source.name.lower())
            shutil.copy2(source, destination / source.name)
            imports = subprocess.check_output(['objdump', '-p', str(source)], text=True, errors='replace')
            for name in re.findall(r'DLL Name:\s*(\S+)', imports):
                key = name.lower()
                if key in system_names or key.startswith(('api-ms-win-', 'ext-ms-win-')):
                    continue
                if key not in sdk_bin:
                    raise SystemExit('Clang SDK dependency missing: ' + name)
                queue.append(sdk_bin[key])
        (data / 'runtime.json').write_text(json.dumps({'files': sorted(copied), 'resources': resource.name}), encoding='utf-8')
        return
    if not sys.platform.startswith('linux'):
        raise SystemExit('Clang portable bundling currently supports Linux and Windows; use an SDK-linked non-bundle build here')
    source = library_dir / 'libclang-cpp.so'
    if not source.is_file():
        raise SystemExit('Clang shared library missing: ' + str(source))
    if not shutil.which('patchelf'):
        raise SystemExit('Portable Clang bundling requires patchelf')
    destination = prefix / args.libdir / 'geany/clang'
    destination.mkdir(parents=True, exist_ok=True)
    queue = [(source.name, source)]
    seen = set()
    while queue:
        alias, source = queue.pop()
        real = source.resolve()
        if alias in seen:
            continue
        seen.add(alias)
        output = subprocess.check_output(['ldd', str(real)], text=True)
        for name, path in re.findall(r'^\s*(\S+)\s+=>\s+(/\S+)', output, re.M):
            if not re.fullmatch(r'lib(c|m|dl|pthread|rt|resolv|util)\.so(?:\..*)?', name):
                queue.append((name, Path(path)))
        target = destination / alias
        shutil.copy2(real, target)
        subprocess.run(['patchelf', '--set-rpath', '$ORIGIN', str(target)], check=True)
        dynamic = subprocess.check_output(['readelf', '-d', str(real)], text=True)
        soname = re.search(r'\(SONAME\).*\[(.*?)\]', dynamic)
        if soname and soname[1] != alias:
            soname_path = destination / soname[1]
            if not soname_path.exists():
                soname_path.symlink_to(alias)
    print('Bundled Clang/LLVM runtime:', len(seen), 'libraries; resources:', resource.name)


if __name__ == '__main__':
    main()
