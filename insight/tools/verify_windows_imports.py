#!/usr/bin/env python3
"""Verify the x64 module's named imports against supplied Windows runtime DLLs.

This checks linking compatibility only; it does not execute Windows code.
"""
import argparse
from pathlib import Path
import re
import subprocess

SYSTEM = {'kernel32.dll', 'msvcrt.dll', 'ucrtbase.dll', 'user32.dll', 'advapi32.dll',
          'shell32.dll', 'ole32.dll', 'ws2_32.dll', 'ntdll.dll', 'wsock32.dll',
          'comdlg32.dll', 'gdi32.dll', 'winspool.drv', 'oleaut32.dll', 'dwmapi.dll'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdump', default='x86_64-w64-mingw32-objdump')
    parser.add_argument('--module', type=Path, required=True)
    parser.add_argument('--runtime-bin', type=Path, required=True)
    args = parser.parse_args()
    def inspect(path):
        return subprocess.check_output([args.objdump, '-p', str(path)], text=True)
    text = inspect(args.module)
    if 'file format pei-x86-64' not in text:
        raise SystemExit('Expected a PE x64 module')
    imports = {}
    current = None
    for line in text.splitlines():
        found = re.search(r'DLL Name: (\S+)', line)
        if found:
            current = found[1]
            imports[current] = []
            continue
        found = re.match(r'^\s+[0-9a-f]+\s+\d+\s+(\S+)\s*$', line)
        if found and current:
            imports[current].append(found[1])
    if not imports:
        raise SystemExit('No import table found')
    for name, symbols in imports.items():
        dll = args.runtime_bin / name
        if not dll.exists():
            if name.lower() not in SYSTEM:
                raise SystemExit('Missing runtime DLL: '+name)
            print(name+': Windows system DLL')
            continue
        table = inspect(dll).split('[Ordinal/Name Pointer] Table', 1)[1].split('The Function Table', 1)[0]
        exports = set(re.findall(r'^\s*\[\s*\d+\]\s+(\S+)', table, re.M))
        missing = set(symbols)-exports
        if missing:
            raise SystemExit(name+': missing '+', '.join(sorted(missing)))
        print(f'{name}: {len(symbols)} named imports resolved')


if __name__ == '__main__':
    main()
