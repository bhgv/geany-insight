#!/usr/bin/env python3
"""Package an installed editor, Insight module, executable checks and sources."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['linux', 'windows'], required=True)
    parser.add_argument('--editor-root', type=Path, required=True)
    parser.add_argument('--checks-root', type=Path, required=True)
    parser.add_argument('--source-bundle', type=Path, required=True)
    parser.add_argument('--upstream-source', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--verification-note', action='append', default=[])
    parser.add_argument('--languages', nargs='+', help='Keep only these locale directories (English is builtin)')
    parser.add_argument('--strip', help='Strip tool for own module and test executables only')
    args = parser.parse_args()
    if args.platform == 'windows' and not args.upstream_source:
        parser.error('Windows package needs Geany 2.1 source archive')
    args.out.mkdir(parents=True, exist_ok=True)
    name = 'geany-insight-0.7-check-'+args.platform+'-x64'
    dest = args.out/name
    if dest.exists():
        parser.error('Staging destination already exists: '+str(dest))
    shutil.copytree(args.editor_root, dest, symlinks=args.platform == 'linux',
                    ignore=shutil.ignore_patterns('include', 'pkgconfig', '$PLUGINSDIR'))
    if args.languages and (dest/'share/locale').exists():
        for locale in (dest/'share/locale').iterdir():
            if locale.is_dir() and locale.name not in args.languages:
                shutil.rmtree(locale)
    windows = args.platform == 'windows'
    module = 'insight_lua.dll' if windows else 'insight_lua.so'
    (dest/'lib/geany').mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.checks_root/module, dest/'lib/geany'/module)
    checks = dest/'checks'
    checks.mkdir()
    for path in args.checks_root.glob('*_test.exe' if windows else '*_test'):
        shutil.copy2(path, checks/path.name)
    if args.strip:
        for binary in [dest/'lib/geany'/module, *checks.iterdir()]:
            subprocess.run([args.strip, '--strip-unneeded', str(binary)], check=True)
    launchers = ['Start-Insight.cmd', 'Start-Insight.ps1', 'Run-Checks.cmd', 'Run-Checks.ps1'] if windows else ['start-insight.sh', 'run-checks.sh']
    for filename in launchers:
        shutil.copy2(ROOT/'packaging'/filename, dest/filename)
    shutil.copytree(ROOT/'examples', dest/'examples')
    shutil.copytree(ROOT/'docs', dest/'docs')
    source = dest/'source'
    source.mkdir()
    shutil.copy2(args.source_bundle, source/'geany-insight.git.bundle')
    if args.upstream_source:
        shutil.copy2(args.upstream_source, source/args.upstream_source.name)
    licenses = dest/'licenses'
    licenses.mkdir()
    shutil.copy2(ROOT.parent/'COPYING', licenses/'Geany-GPL-2.txt')
    shutil.copy2(ROOT/'vendor/lua/lua.h', licenses/'Lua-MIT-header.txt')
    shutil.copy2(ROOT/'docs/desktop-check-builds.md', dest/'README.md')
    info = json.loads((args.checks_root/'build-info.json').read_text())
    info['source_revision'] = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    info['included_locales'] = args.languages or 'all available'
    info['native_debug_symbols_stripped'] = bool(args.strip)
    info['editor_base'] = 'official Geany 2.1 x64' if windows else 'Geany 2.2 development built from included sources'
    info['verification'] = (['Cross compiled; Windows execution not verified'] if windows else ['8 core checks passed during compilation']) + args.verification_note
    info['linux_requirements'] = None if windows else 'Ubuntu 24.04 x64 or compatible: glibc >= 2.38, GLib >= 2.80, GTK3 >= 3.24 and dependencies'
    (dest/'BUILD-INFO.json').write_text(json.dumps(info, indent=2)+'\n')
    hashes = []
    for path in sorted(dest.rglob('*')):
        if path.is_file() and not path.is_symlink():
            hashes.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.relative_to(dest).as_posix())
    (dest/'SHA256SUMS').write_text('\n'.join(hashes)+'\n')
    if windows:
        archive = args.out/(name+'.zip')
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for path in sorted(dest.rglob('*')):
                if path.is_file():
                    z.write(path, name+'/'+path.relative_to(dest).as_posix())
    else:
        archive = args.out/(name+'.tar.gz')
        with tarfile.open(archive, 'w:gz', compresslevel=6) as t:
            t.add(dest, arcname=name)
    print(archive.resolve())


if __name__ == '__main__':
    main()
