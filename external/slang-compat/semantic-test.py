#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual native service, without GTK or an external language server."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

service, fixtures = sys.argv[1], Path(sys.argv[2])
files = [str(fixtures / 'leaf.sv'), str(fixtures / 'top.sv')]
def query(*args, **kwargs):
    output = subprocess.check_output([service] + list(args) + ['--'] + kwargs.get('files', files))
    reply = json.loads(output.decode('utf-8'))
    assert reply['protocol'] == 1
    return reply

lookup = query('--lookup', 'top.u_leaf.data_i')
assert lookup['diagnostics_count'] == 0
assert len(lookup['records']) == 1
row = lookup['records'][0]
assert Path(row['declaration']['path']).name == 'leaf.sv'
assert row['declaration']['line'] == 2 and row['declaration']['column'] == 28
assert query('--lookup', 'top.missing')['records'] == []
assert query('--complete', 'top.missing', '')['records'] == []
complete = query('--complete', 'top.u_leaf', 'data_')['records']
assert sorted(r['name'] for r in complete) == ['data_i', 'data_o']  # port/storage pair deduplicated
assert query('--complete', 'top.u_leaf', 'DATA_')['records'] == []  # HDL names are case-sensitive
refs = query('--references')['records']
instance = next(r for r in refs if r.get('instance') == 'top.u_leaf')
assert Path(instance['source']['path']).name == 'top.sv'
assert instance['source']['line'] == 3
assert Path(instance['target']['path']).name == 'leaf.sv'
assert instance['definition'] == 'leaf'
assert any(r.get('target', {}).get('hierarchy') == 'top.u_leaf.data_i' for r in refs)
assert any(r['hierarchy'] == 'top.u_leaf.WIDTH' for r in query('--symbols')['records'])
with tempfile.TemporaryDirectory(prefix='slang space ') as directory:
    path = Path(directory) / 'quoted name.sv'
    path.write_text('module broken; missing_module bad(); endmodule\n')
    assert query('--symbols', files=[str(path)])['diagnostics_count'] > 0
    path.write_text('module standalone; logic signal; endmodule\n')
    reply = query('--complete', 'standalone', 'sig', files=[str(path)])
    assert [r['name'] for r in reply['records']] == ['signal']
    package = Path(directory) / 'pkg.sv'
    package.write_text('package util; function int increment(int value); return value + 1; endfunction endpackage\n')
    path.write_text('module caller; int x; initial x = util::increment(2); endmodule\n')
    package_files = [str(package), str(path)]
    reply = query('--references', files=package_files)
    assert reply['diagnostics_count'] == 0
    call = next(r for r in reply['records'] if r.get('target', {}).get('name') == 'increment')
    assert Path(call['source']['path']) == path
    assert Path(call['target']['declaration']['path']) == package
    assert call['target']['kind'] == 'Subroutine'
    missing = subprocess.run([service, '--symbols', '--', str(path) + '.absent'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert missing.returncode != 0 and not missing.stdout
bad = subprocess.run([service, '--unknown', '--'] + files, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
assert bad.returncode != 0 and not bad.stdout
print('Native HDL lookup, instance edges, references, completion and failure protocol passed')
