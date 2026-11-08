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

def editor_query(path, line, receiver, prefix, units, overlays=()):
    command = [service, '--editor-complete', str(path), str(line), receiver, prefix]
    for original, replacement in overlays:
        command += ['--overlay', str(original), str(replacement)]
    raw = subprocess.check_output(command + ['--'] + units).decode('utf-8').splitlines()
    assert raw[0] == 'insight-slang-completion-v1'
    return [tuple(bytes.fromhex(field).decode('utf-8') for field in row.split('\t')) for row in raw[1:]]
with tempfile.TemporaryDirectory(prefix='slang editor ') as directory:
    root = Path(directory)
    leaf, top, unsaved = root / 'leaf.sv', root / 'top.sv', root / 'unsaved.sv'
    leaf.write_text('module leaf; logic old_signal; endmodule\n')
    top.write_text('module top; leaf u_leaf(); endmodule\n')
    unsaved.write_text('module leaf; logic new_signal; endmodule\n')
    units = [str(leaf), str(top)]
    assert editor_query(top, 1, 'u_leaf', 'old_', units) == [('old_signal', 'Variable')]
    assert editor_query(top, 1, 'u_leaf', 'new_', units, [(leaf, unsaved)]) == [('new_signal', 'Variable')]
    assert 'old_signal' in leaf.read_text()  # snapshot never writes the original
    assert editor_query(top, 1, 'absent', '', units) == []
    top.write_text('module top; leaf first(); leaf second(); endmodule\n')
    assert editor_query(leaf, 1, '', 'old_', units) == []  # two elaborated instances: decline
print('Editor completion protocol: local hierarchy, unsaved cross-file overlays and ambiguous scopes passed')

def definition_query(path, line, column, units, overlays=()):
    command = [service, '--editor-definition', str(path), str(line), str(column)]
    for original, replacement in overlays:
        command += ['--overlay', str(original), str(replacement)]
    rows = subprocess.check_output(command + ['--'] + units).decode('utf-8').splitlines()
    assert rows[0] == 'insight-slang-definition-v1'
    if len(rows) == 1:
        return None
    assert len(rows) == 2
    name, line, column = rows[1].split('\t')
    return Path(bytes.fromhex(name).decode('utf-8')), int(line), int(column)
with tempfile.TemporaryDirectory(prefix='slang navigation ') as directory:
    root = Path(directory)
    leaf, top, unsaved = root / 'leaf.sv', root / 'top.sv', root / 'snapshot.sv'
    leaf.write_text('module leaf(input logic data_i); endmodule\n')
    text = 'module top; logic data; leaf u_leaf(.data_i(data)); initial $display(u_leaf.data_i); endmodule\n'
    top.write_text(text)
    units = [str(leaf), str(top)]
    for word in ['leaf u_leaf', 'u_leaf(', 'data_i(data)', 'data_i);']:
        target = definition_query(top, 1, text.index(word) + 1, units)
        assert target[0] == leaf, (word, target)
    target = definition_query(top, 1, text.index('data));') + 1, units)
    assert target[0] == top and target[2] == text.index('data;') + 1
    assert definition_query(top, 1, 1, units) is None
    unsaved.write_text('module leaf; logic new_signal; endmodule\n')
    text = 'module top; leaf u_leaf(); initial $display(u_leaf.new_signal); endmodule\n'
    top.write_text(text)
    target = definition_query(top, 1, text.index('new_signal') + 1, units, [(leaf, unsaved)])
    assert target == (leaf, 1, 20), target
    package = root / 'pkg.sv'
    package.write_text('package util; function int increment(int x); return x+1; endfunction endpackage\n')
    text = 'module top; int x; initial x = util::increment(1); endmodule\n'
    top.write_text(text)
    target = definition_query(top, 1, text.index('increment') + 1, [str(package), str(top)])
    assert target[0] == package and target[2] == 28, target
    assert definition_query(top, 1, text.index('util::') + 1, [str(package), str(top)]) is None
    assert definition_query(top, 1, text.index('(1)') + 2, [str(package), str(top)]) is None
    leaf.write_text('module leaf #(parameter P=0);\nif(P) begin: branch logic value; end\nelse begin: branch logic value; end\ninitial $display(branch.value);\nendmodule\n')
    top.write_text('module top; leaf #(.P(1)) first(); leaf #(.P(0)) second(); endmodule\n')
    assert definition_query(leaf, 4, 25, units) is None
print('Native HDL navigation: module/instance/port/value/function targets, overlays and ambiguous elaboration passed')

index_output = subprocess.check_output([service, '--editor-index', '--'] + files)
assert index_output.startswith(b'insight-slang-index-v2\n') and b'\r' not in index_output
raw = index_output.decode('utf-8').splitlines()
assert raw[0] == 'insight-slang-index-v2' and raw[1] == '0'
rows = [line.split('\t') for line in raw[2:]]
assert all(len(row) == 8 for row in rows)
assert any(row[0] == 'D' and bytes.fromhex(row[1]).decode() == 'data_i' for row in rows)
assert any(row[0] == 'R' and bytes.fromhex(row[7]).decode() == 'module' and bytes.fromhex(row[1]).decode() == 'leaf' for row in rows)
if not any(row[0] == 'R' and bytes.fromhex(row[7]).decode() == 'read'
           and bytes.fromhex(row[1]).decode() == 'data_i' for row in rows):
    raise RuntimeError("Expected a read of data_i in the HDL project index")
print('Project index protocol: declarations, module links, named ports and value references passed')

with tempfile.TemporaryDirectory(prefix='slang include ') as directory:
    root = Path(directory)
    header, leaf, top, snapshot = root / 'defs.svh', root / 'leaf.sv', root / 'top.sv', root / 'snapshot.svh'
    header.write_text('logic old_signal;\n')
    snapshot.write_text('logic new_signal;\n')
    leaf.write_text('module leaf;\n`include "defs.svh"\nendmodule\n')
    text = 'module top; leaf u_leaf(); initial $display(u_leaf.new_signal); endmodule\n'
    top.write_text(text)
    units = [str(top), str(leaf), str(header)]  # header follows its includer
    result = editor_query(top, 1, 'u_leaf', 'new_', units, [(header, snapshot)])
    assert result == [('new_signal', 'Variable')], result
    target = definition_query(top, 1, text.index('new_signal') + 1, units, [(header, snapshot)])
    assert target == (header, 1, 7), target
    output = subprocess.check_output([service, '--editor-index', '--overlay', str(header), str(snapshot), '--'] + units).decode('utf-8').splitlines()
    assert output[1] == '0', output[:2]
    assert 'old_signal' in header.read_text()
print('Unsaved include snapshots are primed before source parsing; headers are not duplicate units')

with tempfile.TemporaryDirectory(prefix='slang diagnostics ') as directory:
    source = Path(directory) / 'typo.sv'
    source.write_text('module typo;\nlogic target_signal;\ninitial $display(target_signl);\nendmodule\n')
    output = subprocess.check_output([service, '--editor-index', '--', str(source)]).decode('utf-8').splitlines()
    diagnostics = [row.split('\t') for row in output[2:] if row.startswith('G\t')]
    assert int(output[1]) > 0 and diagnostics
    decoded = [tuple(bytes.fromhex(row[i]).decode('utf-8') if i in (1,2,5,6,7) else row[i] for i in range(8)) for row in diagnostics]
    suggestion = next(row for row in decoded if row[5] == 'TypoIdentifier')
    assert suggestion[1] == 'error' and Path(suggestion[2]) == source
    assert suggestion[3:5] == ('3','18'), suggestion
    assert 'target_signl' in suggestion[6] and 'target_signal' in suggestion[6] and 'did you mean' in suggestion[6]
    assert source.read_text().find('target_signl') >= 0
    source.write_text('module broken;\nlogic signal\nendmodule\n')
    output = subprocess.check_output([service, '--editor-index', '--', str(source)]).decode('utf-8').splitlines()
    assert any(row.startswith('G\t') for row in output[2:]) and int(output[1]) > 0
print('HDL diagnostic protocol: syntax errors, exact typo location and compiler name suggestion passed')

with tempfile.TemporaryDirectory(prefix='slang bit mapping ') as directory:
    source = Path(directory) / 'wiring.sv'
    source.write_text("""module wiring_leaf(input wire [0:3] input_data,
                                           output wire [7:4] output_data);
        assign output_data = input_data;
    endmodule
    module wiring(
        input wire [7:0] data,
        input wire [0:7] ascending,
        input wire [2:0] index,
        output wire [1:0] reordered,
        output wire [1:0] repeated,
        output wire [1:0] uncertain,
        output wire high_bit, low_bit
    );
        assign reordered = {data[0], data[7]};
        assign {high_bit, low_bit} = ascending[2+:2];
        assign repeated = {2{data[3]}};
        assign uncertain = {data[index], data[7]};
        wire [1:0] upper, lower;
        wiring_leaf leaf(.input_data({data[3:2], ascending[5:6]}), .output_data({upper, lower}));
    endmodule
""", encoding='utf-8')
    output = subprocess.check_output([service, '--editor-index', '--', str(source)])
    rows = output.decode('utf-8').splitlines()
    assert rows[1] == '0', rows[:2]
    edges = {tuple(bytes.fromhex(row[i]).decode('utf-8') for i in (5, 1, 6, 7))
             for row in (line.split('\t') for line in rows[2:]) if row[0] == 'P'}
    for target, peer, detail in (
        ('reordered', 'data', '[0] / [7]'),
        ('reordered', 'data', '[1] / [0]'),
        ('high_bit', 'ascending', '[0] / [2]'),
        ('low_bit', 'ascending', '[0] / [3]'),
        ('repeated', 'data', '[0] / [3]'),
        ('repeated', 'data', '[1] / [3]'),
        ('uncertain', 'data', 'all / [?]'),
        ('uncertain', 'index', ''),
    ):
        edge = (f'wiring.{target}', f'wiring.{peer}', detail, 'assign-source')
        assert edge in edges, (edge, sorted(edges))
    for edge in (
        ('wiring.leaf.input_data', 'wiring.data', '[0:1] => [3:2]', 'port-source'),
        ('wiring.leaf.input_data', 'wiring.ascending', '[2:3] => [5:6]', 'port-source'),
        ('wiring.leaf.output_data', 'wiring.leaf.input_data', '[7:4] => [0:3]', 'assign-source'),
        ('wiring.upper', 'wiring.leaf.output_data', '[1:0] => [7:6]', 'port-source'),
        ('wiring.leaf.output_data', 'wiring.lower', '[5:4] => [1:0]', 'port-destination'),
    ):
        assert edge in edges, (edge, sorted(edges))
    assert ('wiring.reordered', 'wiring.data', '[0] / [0]', 'assign-source') not in edges
    assert ('wiring.data', 'wiring.reordered', '[7] / [0]', 'assign-destination') in edges
print('HDL wiring: bit order, indexed ascending slice, replication and conservative fallback passed')
