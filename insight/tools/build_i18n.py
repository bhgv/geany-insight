"""Refresh native gettext captions; insight.tsv is the authoritative editable table."""
import argparse
import ast
from pathlib import Path


def esc(text):
    return text.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t').replace('\r', '\\r')


def native_catalog():
    entries = {}
    for block in Path('po/ru.po').read_text().split('\n\n'):
        values, field = {}, None
        for line in block.splitlines():
            if line.startswith(('msgid ', 'msgstr ', 'msgid_plural ', 'msgstr[')):
                field, quoted = line.split(' ', 1)
                try:
                    values[field] = ast.literal_eval(quoted)
                except (ValueError, SyntaxError):
                    field = None
            elif line.startswith('"') and field:
                values[field] += ast.literal_eval(line)
        if values.get('msgid') and values.get('msgstr', values.get('msgstr[0]', '')):
            entries[values['msgid']] = values.get('msgstr', values.get('msgstr[0]'))
        if values.get('msgid_plural') and values.get('msgstr[1]'):
            entries[values['msgid_plural']] = values['msgstr[1]']
    return '# Native Geany English/Russian UI, derived from po/ru.po (GPL-2.0-or-later)\n' + ''.join(esc(k) + '\t' + esc(v) + '\n' for k, v in sorted(entries.items()))


def validate(path):
    entries = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line or line.startswith('#'):
            continue
        fields = line.split('\t')
        if len(fields) not in (2, 3) or any(not field for field in fields):
            raise SystemExit(f'{path}:{number}: expected key<TAB>Russian or key<TAB>English<TAB>Russian')
        if fields[0] in entries:
            raise SystemExit(f'{path}:{number}: duplicate key {fields[0]}')
        entries[fields[0]] = fields[1:]
    return len(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='validate without modifying catalogs')
    args = parser.parse_args()
    expected = native_catalog()
    native = Path('insight/locale/native.tsv')
    if args.check:
        if native.read_text() != expected:
            raise SystemExit('native.tsv is stale; run python3 insight/tools/build_i18n.py')
    else:
        native.write_text(expected)
    print('Native:', validate(native), 'Insight:', validate(Path('insight/locale/insight.tsv')))


if __name__ == '__main__':
    main()
