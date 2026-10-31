#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare the internal compiler, refresh build files, and build Geany Insight."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
from select_clang_sdk import resolve as resolve_sdk

ROOT = Path(__file__).resolve().parents[2]


def arguments(argv=None, root=ROOT):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=('autotools', 'meson'), default='autotools')
    parser.add_argument('--clang-provider', choices=('internal', 'system', 'disabled'), default='internal')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--prefix', type=Path, default=root / 'out')
    parser.add_argument('--sdk', type=Path, default=root / '_external-clang-sdk')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--skip-sdk-build', action='store_true', help='Use an already prepared SDK; rebuild it yourself after editing Clang')
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--install', action='store_true', help='Install after building; never invokes sudo')
    parser.add_argument('--plan', action='store_true', help='Print commands as JSON without executing or writing files')
    parser.add_argument('extra', nargs=argparse.REMAINDER, help='Arguments after -- are passed to configure or meson setup')
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.configure_only and args.install:
        parser.error('--configure-only cannot be combined with --install')
    if args.build_dir is None:
        # Reuse a user's existing in-tree Autotools build without deleting it.
        args.build_dir = root if args.backend == 'autotools' and (root / 'config.status').exists() else root / '_editor-build' / args.backend
    for name in ('build_dir', 'prefix', 'sdk'):
        setattr(args, name, getattr(args, name).expanduser().resolve())
    if args.build_dir == args.sdk or args.build_dir == root / 'external/llvm-project':
        parser.error('Editor build directory must be separate from compiler files')
    if args.prefix == root:
        parser.error('Install into a separate prefix, not the source directory')
    if args.extra[:1] == ['--']:
        args.extra = args.extra[1:]
    reserved = {'--prefix', '-Dprefix', '--with-clang', '--without-clang', '--with-libclang',
                '--without-libclang', '--with-clang-provider', '-Dclang', '-Dclang-cpp',
                '-Dclang-provider', '-Dclang-sdk', '-Dclang-bootstrap', '--enable-clang-bootstrap', '--disable-clang-bootstrap'}
    if any(value.split('=', 1)[0] in reserved for value in args.extra):
        parser.error('Use the driver options for prefix and compiler selection')
    return args


def plan(args, root=ROOT):
    steps = []
    def add(command, cwd=root, env=None):
        steps.append({'command': [str(x) for x in command], 'cwd': str(cwd), 'env': env or {}})
    if args.clang_provider == 'internal':
        helper = 'select_clang_sdk.py' if args.skip_sdk_build else 'prepare_clang_sdk.py'
        command = [sys.executable, root / 'insight/tools' / helper, '--sdk', args.sdk]
        if not args.skip_sdk_build:
            command += ['--jobs', args.jobs]
        add(command)
    if args.backend == 'autotools':
        add(['sh', root / 'autogen.sh'], env={'NOCONFIGURE': '1'})
        command = ['sh', root / 'configure', '--prefix=' + str(args.prefix)]
        if args.clang_provider == 'internal':
            command += ['--with-clang-provider=internal', '--with-clang=' + str(args.sdk), '--disable-clang-bootstrap']
        elif args.clang_provider == 'system':
            command += ['--with-clang-provider=system', '--with-clang=yes']
        else:
            command += ['--without-clang']
        add(command + args.extra, args.build_dir)
        if not args.configure_only:
            add(['make', '-j' + str(args.jobs)], args.build_dir)
            if args.install:
                add(['make', 'install'], args.build_dir)
    else:
        command = ['meson', 'setup']
        if (args.build_dir / 'meson-private/coredata.dat').is_file():
            command += ['--reconfigure']
        command += [args.build_dir, root, '--prefix=' + str(args.prefix), '-Dclang-bundle=auto']
        if args.clang_provider == 'internal':
            command += ['-Dclang-sdk=' + str(args.sdk), '-Dclang-bootstrap=false']
        elif args.clang_provider == 'system':
            command += ['-Dclang-provider=system', '-Dclang-sdk=']
        else:
            command += ['-Dclang=disabled']
        # Clear cached provider/feature settings when reusing another mode's build.
        if args.clang_provider != 'disabled':
            command += ['-Dclang=enabled', '-Dclang-cpp=enabled']
        if args.clang_provider == 'internal':
            command += ['-Dclang-provider=internal']
        add(command + args.extra)
        if not args.configure_only:
            add(['meson', 'compile', '-C', args.build_dir, '-j', args.jobs])
            if args.install:
                add(['meson', 'install', '-C', args.build_dir])
    return steps


def prerequisites(args):
    tools = ['pkg-config']
    for variable, fallback in (('CC', 'cc'), ('CXX', 'c++')):
        compiler = shlex.split(os.environ.get(variable, fallback), posix=os.name != 'nt')
        if not compiler:
            raise ValueError(variable + ' must name a compiler')
        tools.append(compiler[0].strip('"'))
    if args.backend == 'autotools':
        tools += ['sh', 'autoreconf', 'autoconf', 'automake', 'autopoint', 'make']
    else:
        tools += ['meson', 'ninja']
    missing = [tool for tool in tools if not shutil.which(tool)]
    if args.backend == 'autotools' and not any(shutil.which(tool) for tool in ('libtoolize', 'glibtoolize')):
        missing.append('libtoolize (glibtoolize on macOS)')
    if missing:
        raise ValueError('Missing build tools: ' + ', '.join(missing) + '. Install these tools and GTK development files; system Clang is not required for the internal provider.')


def execute(args, root=ROOT, run=subprocess.run):
    prerequisites(args)
    if args.clang_provider == 'internal' and args.skip_sdk_build:
        resolve_sdk(args.sdk)
    steps = plan(args, root)
    for number, step in enumerate(steps, 1):
        cwd = Path(step['cwd'])
        cwd.mkdir(parents=True, exist_ok=True)
        environment = dict(os.environ, **step['env'])
        print(f'[{number}/{len(steps)}] {shlex.join(step["command"])}', flush=True)
        run(step['command'], cwd=cwd, env=environment, check=True)
    print('Configuration ready:' if args.configure_only else 'Build ready:', args.build_dir)
    if args.install:
        print('Installed:', args.prefix)


def main(argv=None):
    args = arguments(argv)
    if args.plan:
        print(json.dumps(plan(args), indent=2))
        return 0
    try:
        execute(args)
    except (ValueError, OSError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as exc:
        print(f'Build stopped: exit status {exc.returncode}', file=sys.stderr)
        return exc.returncode if exc.returncode > 0 else 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == '__main__':
    sys.exit(main())
