#!/usr/bin/env python3
"""Build/run the dependency-free C core checks with a GCC-compatible compiler.

Run from any writable directory. GTK, Meson, CMake and system Lua are not needed.
"""
import ctypes
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unicodedata

root = Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get('CC', 'cc'))
strict = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
includes = ['-I' + str(root / 'insight/core'), '-I' + str(root / 'insight/vendor/lua')]
core = ['runtime', 'index', 'document', 'context', 'occurrences', 'structure_select', 'diff']
excluded = {'lua', 'luac', 'onelua', 'linit', 'liolib', 'loslib', 'loadlib', 'lcorolib', 'ldblib'}
checks = ['runtime', 'index', 'document', 'snapshot', 'commands', 'editor_api',
          'events', 'context', 'occurrences', 'structure_select', 'relation_graph_layout']

def run(command, **kwargs):
    subprocess.run(command, check=True, **kwargs)

with tempfile.TemporaryDirectory(prefix='insight-core-', dir=Path.cwd()) as directory:
    build = Path(directory)
    objects = []
    for path in [root / 'insight/core' / (name + '.c') for name in core] + sorted(
            p for p in (root / 'insight/vendor/lua').glob('*.c') if p.stem not in excluded):
        out = build / (path.stem + '.o')
        flags = strict if path.parent.name == 'core' else ['-std=c99']
        run(cc + flags + ['-O1'] + includes + ['-c', str(path), '-o', str(out)])
        objects.append(str(out))
    for name in checks:
        target = build / name
        run(cc + strict + includes + [str(root / 'insight/tests' / (name + '_test.c')),
                                     *objects, '-lm', '-o', str(target)])
        run([str(target)])
    # Independent property oracle for the committed Unicode version.
    wrapper = build / 'identifier.c'
    wrapper.write_text('#include "identifier.h"\nunsigned flags(unsigned c) { return insight_identifier_flags(c); }\n')
    shared = build / 'identifier.so'
    run(cc + strict + includes + ['-shared', '-fPIC', str(wrapper), '-o', str(shared)])
    lib = ctypes.CDLL(str(shared))
    lib.flags.argtypes = [ctypes.c_uint]
    lib.flags.restype = ctypes.c_uint
    if unicodedata.unidata_version == '15.0.0':
        for code in range(0x110000):
            char = chr(code)
            expected = int(char.isidentifier()) | (int(('a' + char).isidentifier()) << 1)
            assert lib.flags(code) == expected, hex(code)
        print('Unicode 15.0: all 1,114,112 scalar/codepoint entries agree with the independent oracle.')
    else:
        print('Unicode oracle skipped: Python database is', unicodedata.unidata_version, '(table is 15.0.0).')
    environment = dict(os.environ, TMPDIR=str(build))
    run([sys.executable, str(root / 'insight/tests/diff_test.py')], env=environment)
print('Portable core: 11 test executables and Diff regressions passed; no GUI or Windows build performed.')
