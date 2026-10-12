#!/usr/bin/env python3
"""Compile Insight module and nine core test executables against an explicit SDK.

No downloads or editor installation. On Linux, executes the tests. On Windows
cross builds, produces .exe checks for Run-Checks.ps1 on the target machine.
"""
from pathlib import Path
import argparse
import concurrent.futures
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {'lua.c', 'luac.c', 'onelua.c', 'linit.c', 'liolib.c', 'loslib.c',
            'loadlib.c', 'lcorolib.c', 'ldblib.c'}
TESTS = ['runtime', 'index', 'document', 'snapshot', 'commands', 'editor_api',
         'events', 'context', 'occurrences']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['linux', 'windows'], required=True)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--geany-prefix', type=Path, required=True)
    parser.add_argument('--gtk-prefix', type=Path, required=True)
    parser.add_argument('--gtk-libdir', type=Path, required=True)
    parser.add_argument('--geany-library', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    objects = args.out / 'objects'
    objects.mkdir(exist_ok=True)
    sdk = args.gtk_prefix.resolve()
    geany = args.geany_prefix.resolve()
    includes = [ROOT/'core', ROOT/'vendor/lua', geany/'include/geany',
                geany/'include/geany/scintilla', geany/'include/geany/tagmanager',
                args.gtk_libdir.resolve()/'glib-2.0/include']
    includes += [sdk/'include'/p for p in ['gtk-3.0', 'glib-2.0', 'pango-1.0',
                'harfbuzz', 'cairo', 'gdk-pixbuf-2.0', 'atk-1.0']]
    common = [args.cc, '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
              '-DGTK', '-UNDEBUG'] + ['-I'+str(p) for p in includes]
    windows = args.platform == 'windows'
    if not windows:
        common += ['-D_DEFAULT_SOURCE', '-fPIC']
    core = [ROOT/'core'/p for p in ['runtime.c', 'index.c', 'document.c', 'context.c', 'occurrences.c']]
    core += sorted(p for p in (ROOT/'vendor/lua').glob('*.c') if p.name not in EXCLUDED)
    desktop = [ROOT/'desktop'/p for p in ['plugin.c', 'navigation.c', 'index_cache.c', 'extensions.c']]

    def compile_one(source):
        obj = objects / (source.parent.name+'_'+source.stem+'.o')
        subprocess.run(common+['-c', str(source), '-o', str(obj)], check=True)
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        compiled = list(pool.map(compile_one, core+desktop))
    core_objects = compiled[:len(core)]
    module = args.out / ('insight_lua.dll' if windows else 'insight_lua.so')
    libraries = ['-L'+str(args.gtk_libdir.resolve())]
    if windows:
        libraries += ['-lgtk-3', '-lgobject-2.0', '-lgio-2.0', '-lglib-2.0', '-static-libgcc', '-Wl,--no-undefined']
    else:
        libraries += ['-l:libgtk-3.so.0', '-l:libgobject-2.0.so.0', '-l:libgio-2.0.so.0', '-l:libglib-2.0.so.0', '-Wl,-z,defs']
    subprocess.run([args.cc, '-shared', *map(str, compiled), str(args.geany_library.resolve()),
                    *libraries, '-lm', '-o', str(module)], check=True)
    for name in TESTS:
        output = args.out / (name+'_test'+('.exe' if windows else ''))
        subprocess.run(common+[str(ROOT/'tests'/(name+'_test.c')), *map(str, core_objects),
                        '-lm', *(['-static-libgcc'] if windows else []), '-o', str(output)], check=True)
        if not windows:
            subprocess.run([str(output)], check=True)
    version = subprocess.check_output([args.cc, '--version'], text=True).splitlines()[0]
    (args.out/'build-info.json').write_text(json.dumps({
        'platform': args.platform, 'compiler': version, 'module': module.name,
        'tests': TESTS, 'executed_here': not windows,
        'revision': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
    }, indent=2)+'\n')
    print('Built', module, flush=True)


if __name__ == '__main__':
    main()
