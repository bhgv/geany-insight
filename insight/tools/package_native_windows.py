#!/usr/bin/env python3
"""Package a complete native editor cross-build with a supplied GTK runtime."""
import argparse
import json
import re
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]



def install_editor_icons(destination):
    """Use the same theme layout as icons/meson.build for portable installs."""
    for theme in ['hicolor', 'Tango']:
        target = destination / 'share/icons' / theme
        target.mkdir(parents=True, exist_ok=True)
        if not (target / 'index.theme').exists():
            shutil.copy2(ROOT / 'icons/index.theme', target / 'index.theme')
        # Existing runtime caches do not contain the newly installed editor icons.
        (target / 'icon-theme.cache').unlink(missing_ok=True)
    for size in ['16x16', '24x24', '32x32', '48x48', 'scalable']:
        extension = '.svg' if size == 'scalable' else '.png'
        for name in ['geany', 'geany-build', 'geany-close-all', 'geany-save-all']:
            context = 'apps' if name == 'geany' else 'actions'
            target = destination / 'share/icons/hicolor' / size / context
            target.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / 'icons' / size / (name + extension), target / (name + extension))
        target = destination / 'share/icons/Tango' / size / 'actions'
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / 'icons/tango' / size / ('geany-save-all' + extension), target / ('geany-save-all' + extension))
    target = destination / 'share/icons/hicolor/16x16/apps'
    for source in sorted((ROOT / 'icons/16x16').glob('classviewer-*.png')):
        shutil.copy2(source, target / source.name)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--checks', type=Path, required=True)
    p.add_argument('--bundle', type=Path, help='Optional Git bundle; omitted for a binary-only check build')
    p.add_argument('--name', help='Archive and directory name (default includes Git revision)')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--strip', required=True)
    p.add_argument('--schema-compiler', type=Path, help='Host glib-compile-schemas; compiles the portable GTK schema database')
    p.add_argument('--openssh', type=Path, help='Extracted official OpenSSH-Win64 client directory')
    a = p.parse_args()
    options = json.loads((a.build / 'meson-info/intro-buildoptions.json').read_text())
    if not any(option['name'] == 'plugins' and option['value'] for option in options):
        raise SystemExit('Native Windows delivery requires a build with -Dplugins=true')
    revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    match = re.search(r'^#define REVISION "([^"]+)"', (a.build / 'config.h').read_text(), re.MULTILINE)
    if not match or not revision.startswith(match.group(1)):
        raise SystemExit('Build revision differs from Git HEAD; reconfigure Meson and rebuild first')
    package_name = a.name or ('geany-insight-' + revision[:8] + '-windows-x64')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', package_name):
        raise SystemExit('Package name must be a simple filename')
    destination = a.out / package_name
    if destination.exists():
        raise SystemExit('Output directory already exists: ' + str(destination))
    destination.mkdir(parents=True)
    for name in ['bin', 'etc', 'lib', 'share']:
        if (a.runtime / name).exists():
            shutil.copytree(a.runtime / name, destination / name)
    install_editor_icons(destination)
    schemas = destination / 'share/glib-2.0/schemas'
    if a.schema_compiler:
        subprocess.run([str(a.schema_compiler), '--strict', str(schemas)], check=True)
    compiled = schemas / 'gschemas.compiled'
    if not compiled.is_file() or compiled.stat().st_size < 8:
        raise SystemExit('GTK GSettings database missing; supply --schema-compiler or a compiled runtime')
    # Use plugins built against this editor, rather than the older runtime SDK.
    shutil.rmtree(destination / 'lib/geany', ignore_errors=True)
    plugins = destination / 'lib/geany'
    plugins.mkdir(parents=True)
    for module in sorted((a.build / 'plugins').glob('*.dll')):
        if module.name.startswith('demo'):
            continue
        target = plugins / module.name
        shutil.copy2(module, target)
        subprocess.run([a.strip, '--strip-unneeded', str(target)], check=True)
    # Preserve grep/sort supplied by the runtime for existing Geany commands.
    for name in ['geany.exe', 'libgeany-0.dll', 'geany-insight-askpass.exe']:
        target = destination / 'bin' / name
        shutil.copy2(a.build / name, target)
        subprocess.run([a.strip, '--strip-unneeded', str(target)], check=True)
    if a.openssh:
        for name in ['ssh.exe', 'sftp.exe', 'ssh-keygen.exe', 'ssh-keyscan.exe', 'ssh-add.exe', 'ssh-pkcs11-helper.exe', 'ssh-sk-helper.exe', 'libcrypto.dll']:
            shutil.copy2(a.openssh / name, destination / 'bin' / name)
        for name in ['LICENSE.txt', 'NOTICE.txt']:
            shutil.copy2(a.openssh / name, destination / ('OpenSSH-' + name))
    shutil.copytree(ROOT / 'data', destination / 'data', ignore=shutil.ignore_patterns('*.in', 'Makefile*', 'meson.build'))
    shutil.copy2(a.build / 'data/filetypes.python', destination / 'data/filedefs/filetypes.python')
    shutil.copy2(ROOT / 'COPYING', destination / 'data/GPL-2')
    shutil.copytree(ROOT / 'styles', destination / 'data/styles')
    shutil.copytree(ROOT / 'fonts', destination / 'data/fonts')
    # Keep Russian and English (built into the program), reducing download size.
    locales = destination / 'share/locale'
    if locales.exists():
        for child in locales.iterdir():
            if child.name != 'ru':
                if child.is_dir(): shutil.rmtree(child)
                else: child.unlink()
    (destination / 'checks').mkdir()
    for exe in list(a.checks.glob('*_test.exe')) + list((a.build / 'tests').glob('test_insight*.exe')):
        target = destination / 'checks' / exe.name
        shutil.copy2(exe, target)
        subprocess.run([a.strip, '--strip-unneeded', str(target)], check=True)
    for name in ['Start-Native-Insight.ps1', 'Start-Native-Insight.cmd', 'Diagnose-Windows.cmd', 'Run-Checks.ps1', 'Run-Checks.cmd']:
        shutil.copy2(ROOT / 'insight/packaging' / name, destination / name)
    shutil.copytree(ROOT / 'insight/examples', destination / 'examples')
    shutil.copytree(ROOT / 'insight/docs', destination / 'insight-docs')
    if a.bundle:
        shutil.copy2(a.bundle, destination / a.bundle.name)
    for name in ['LICENSE', 'LICENSES.md']:
        shutil.copy2(ROOT / name, destination / name)
    shutil.copy2(ROOT / 'scintilla/License.txt', destination / 'ScintillaLicense.txt')
    lua_header = (ROOT / 'insight/vendor/lua/lua.h').read_text()
    notice_start = lua_header.index('* Copyright (C)')
    notice_end = lua_header.index('******************************************************************************/', notice_start)
    notice = lua_header[notice_start:notice_end]
    (destination / 'LuaLicense.txt').write_text('\n'.join(line[2:] if line.startswith('* ') else line.lstrip('*') for line in notice.splitlines()) + '\n')
    check_names = sorted(file.name for file in (destination / 'checks').glob('*.exe'))
    runner = "$ErrorActionPreference = 'Stop'\n$env:PATH = (Join-Path $PSScriptRoot 'bin') + ';' + $env:PATH\n"
    runner += "foreach ($name in " + ', '.join("'" + name + "'" for name in check_names) + ") {\n"
    runner += '    & (Join-Path $PSScriptRoot "checks\\$name")\n    if ($LASTEXITCODE -ne 0) { throw "Check failed: $name ($LASTEXITCODE)" }\n}\n'
    runner += "Write-Host 'All " + str(len(check_names)) + " checks passed.'\n"
    (destination / 'Run-Checks.ps1').write_text(runner, encoding='utf-8-sig')
    for license_file in a.runtime.glob('*.txt'):
        shutil.copy2(license_file, destination / license_file.name)
    (destination / 'Copying.txt').write_bytes((ROOT / 'COPYING').read_bytes())
    info = {'revision': revision,
            'editor': 'Geany Insight / Geany 2.2.0 development',
            'check_programs': check_names,
            'architecture': 'Windows x64', 'windows_execution_verified': False}
    (destination / 'build-info.json').write_text(json.dumps(info, indent=2) + '\n')
    (destination / 'README-CHECK.txt').write_text(
        'Geany Insight ' + revision[:8] + ' — Windows x64, проверочная сборка\n\n'
        'Распакуйте весь ZIP в каталог с правом записи. Запуск: Start-Native-Insight.cmd.\n'
        'Настройки хранятся в profile рядом с редактором. Context и Lua встроены в редактор.\n'
        'Context: подчёркнутые имена открываются обычным кликом; F12 и Ctrl+click также работают.\n'
        'Search Results: проект, открытые/выбранные файлы, папка, Keywords/Boolean и Regex.\n'
        'SSH Terminal: удалённые файлы, папки, проекты, Save password и Auto upload on save.\n'
        'Пароли сохраняются открытым текстом в конфиге проекта только при включённой опции.\n'
        'SSH shell использует ввод строк; полноэкранные терминальные программы не эмулируются.\n'
        'Scripts > Load: examples/workbench.lua. Run-Checks.cmd запускает все включённые проверки.\n'
        'Сборка и таблицы импортов проверены; запуск GUI на Windows здесь не проверен.\n'
        'Документация и ограничения: insight-docs/native-workbench.md.\n', encoding='utf-8-sig')
    archive = a.out / (destination.name + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for file in sorted(destination.rglob('*')):
            if file.is_file(): z.write(file, file.relative_to(a.out))
    print(archive.resolve())


if __name__ == '__main__':
    main()
