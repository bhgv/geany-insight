import ast
from pathlib import Path
entries={}
for block in Path('po/ru.po').read_text().split('\n\n'):
 vals={};field=None
 for line in block.splitlines():
  if line.startswith(('msgid ', 'msgstr ', 'msgid_plural ', 'msgstr[')):
   field,quoted=line.split(' ',1)
   try:vals[field]=ast.literal_eval(quoted)
   except:field=None
  elif line.startswith('"') and field:
   try:vals[field]+=ast.literal_eval(line)
   except:pass
 if vals.get('msgid') and vals.get('msgstr',vals.get('msgstr[0]','')):entries[vals['msgid']]=vals.get('msgstr',vals.get('msgstr[0]'))
 if vals.get('msgid_plural') and vals.get('msgstr[1]'):entries[vals['msgid_plural']]=vals['msgstr[1]']
def esc(s):return s.replace('\\','\\\\').replace('\n','\\n').replace('\t','\\t').replace('\r','\\r')
Path('insight/locale/native.tsv').write_text('# Native Geany English/Russian UI, derived from po/ru.po (GPL-2.0-or-later)\n'+''.join(esc(k)+'\t'+esc(v)+'\n' for k,v in sorted(entries.items())))
print(len(entries))
pairs={}
for line in Path('insight/locale/insight.pairs').read_text().splitlines():
 if not line or line.startswith('#'):continue
 a,b=line.split('|||',1)
 if a in pairs and pairs[a]!=b:raise SystemExit('Conflicting translation: '+a)
 pairs[a]=b
Path('insight/locale/insight.tsv').write_text('# Insight English/Russian UI; SPDX-License-Identifier: GPL-2.0-or-later\n'+''.join(esc(a)+'\t'+esc(b)+'\n' for a,b in pairs.items()))
print('Insight:',len(pairs))
