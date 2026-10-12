#!/usr/bin/env python3
"""Package a complete native editor cross-build with a supplied GTK runtime."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--checks', type=Path, required=True)
    p.add_argument('--bundle', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--strip', required=True)
    a = p.parse_args()
    destination = a.out / 'geany-insight-0.8-native-windows-x64'
    if destination.exists():
        raise SystemExit('Output directory already exists: ' + str(destination))
    destination.mkdir(parents=True)
    for name in ['bin', 'etc', 'lib', 'share']:
        if (a.runtime / name).exists():
            shutil.copytree(a.runtime / name, destination / name)
    # The editor is our full build; no old editor plugin is included.
    shutil.rmtree(destination / 'lib/geany', ignore_errors=True)
    for name in ['grep.exe', 'sort.exe']:
        (destination / 'bin' / name).unlink(missing_ok=True)
    for name in ['geany.exe', 'libgeany-0.dll']:
        target = destination / 'bin' / name
        shutil.copy2(a.build / name, target)
        subprocess.run([a.strip, '--strip-unneeded', str(target)], check=True)
    shutil.copytree(ROOT / 'data', destination / 'data', ignore=shutil.ignore_patterns('*.in', 'Makefile*', 'meson.build'))
    shutil.copy2(a.build / 'data/filetypes.python', destination / 'data/filedefs/filetypes.python')
    shutil.copy2(ROOT / 'COPYING', destination / 'data/GPL-2')
    # Keep Russian and English (built into the program), reducing download size.
    locales = destination / 'share/locale'
    if locales.exists():
        for child in locales.iterdir():
            if child.name != 'ru':
                if child.is_dir(): shutil.rmtree(child)
                else: child.unlink()
    (destination / 'checks').mkdir()
    for exe in a.checks.glob('*_test.exe'):
        target = destination / 'checks' / exe.name
        shutil.copy2(exe, target)
        subprocess.run([a.strip, '--strip-unneeded', str(target)], check=True)
    for name in ['Start-Native-Insight.ps1', 'Start-Native-Insight.cmd', 'Run-Checks.ps1', 'Run-Checks.cmd']:
        shutil.copy2(ROOT / 'insight/packaging' / name, destination / name)
    shutil.copytree(ROOT / 'insight/examples', destination / 'examples')
    shutil.copytree(ROOT / 'insight/docs', destination / 'insight-docs')
    shutil.copy2(a.bundle, destination / a.bundle.name)
    for license_file in a.runtime.glob('*.txt'):
        shutil.copy2(license_file, destination / license_file.name)
    (destination / 'Copying.txt').write_bytes((ROOT / 'COPYING').read_bytes())
    info = {'revision': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
            'editor': 'Geany Insight native 0.8 / Geany 2.2.0 development',
            'architecture': 'Windows x64', 'windows_execution_verified': False}
    (destination / 'build-info.json').write_text(json.dumps(info, indent=2) + '\n')
    (destination / 'README-CHECK.txt').write_text(
        'Geany Insight 0.8 — Windows x64, проверочная сборка\n\n'
        'Распакуйте весь ZIP в каталог с правом записи. Запуск: Start-Native-Insight.cmd.\n'
        'Настройки хранятся в profile рядом с редактором. Context и Lua встроены в редактор.\n'
        'Navigate: workspace, files, usages, F12. Ctrl+click в Context: переход по определениям.\n'
        'Scripts > Load: examples/workbench.lua. Run-Checks.cmd: 9 проверок ядра.\n'
        'Сборка и таблицы импортов проверены; запуск GUI на Windows здесь не проверен.\n'
        'Документация и ограничения: insight-docs/native-workbench.md.\n', encoding='utf-8-sig')
    archive = a.out / (destination.name + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for file in sorted(destination.rglob('*')):
            if file.is_file(): z.write(file, file.relative_to(a.out))
    print(archive.resolve())


if __name__ == '__main__':
    main()
