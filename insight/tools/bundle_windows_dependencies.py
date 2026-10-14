#!/usr/bin/env python3
"""Complete a native Windows portable package using its exact compiler SDK."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--package', type=Path, required=True)
parser.add_argument('--sdk', type=Path, required=True)
args = parser.parse_args()
package = args.package.resolve()
sdk = args.sdk.resolve()
sdk_bin = {p.name.lower(): p for p in (sdk / 'bin').glob('*.dll')}
system = Path(os.environ['SystemRoot']) / 'System32'
system_dlls = {p.name.lower() for p in system.iterdir() if p.is_file()}  # Includes WINSPOOL.DRV.

# Keep the GTK bundle layout/data, but use the binaries that we linked against.
for target in package.rglob('*'):
    if target.is_file() and target.suffix.lower() == '.dll':
        relative = target.relative_to(package)
        source = sdk / relative
        if relative.parts[0] == 'bin':
            source = sdk_bin.get(target.name.lower(), source)
        if source.is_file():
            shutil.copy2(source, target)

shutil.copy2(sdk / 'bin/gdk-pixbuf-query-loaders.exe', package / 'bin')
queue = [p for p in package.rglob('*') if p.suffix.lower() in ('.exe', '.dll')]
seen = set()
missing = set()
while queue:
    binary = queue.pop()
    if binary in seen:
        continue
    seen.add(binary)
    output = subprocess.check_output(['objdump', '-p', str(binary)], text=True, errors='replace')
    for name in re.findall(r'DLL Name:\s*(\S+)', output):
        key = name.lower()
        if key.startswith(('api-ms-win-', 'ext-ms-win-')) or key in system_dlls:
            continue
        destination = package / 'bin' / name
        existing = next((p for p in (package / 'bin').iterdir() if p.name.lower() == key), None)
        if existing:
            queue.append(existing)
        elif key in sdk_bin:
            shutil.copy2(sdk_bin[key], destination)
            print('Included dependency:', name, flush=True)
            queue.append(destination)
        else:
            missing.add((str(binary.relative_to(package)), name))
if missing:
    raise SystemExit('Unresolved imports: ' + repr(sorted(missing)))
print('Verified dependency closure for', len(seen), 'Windows binaries.', flush=True)
