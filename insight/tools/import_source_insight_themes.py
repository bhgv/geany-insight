#!/usr/bin/env python3
"""Convert the supplied SI 4 theme export to editable GTK/editor CSS.

Usage: python3 insight/tools/import_source_insight_themes.py [input.xml] [styles]
The original attributes are retained in comments, including unsupported metrics.
"""
from pathlib import Path
import json
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
PANELS = {
 'SymbolPane': ['insight-local-symbols'],
 'ClipWindow/ListDisplay': ['insight-clips'],
 'BookmarkWindow/ListDisplay': ['insight-bookmarks'],
 'ProjectSymbolWindow/ListDisplay': ['insight-project-symbols'],
 'ProjectFileWindow/ListDisplay': ['insight-project-files'],
 'ProjectFileTypeWindow/ListDisplay': ['insight-file-types'],
 'ProjectSymbolTypeWindow/ListDisplay': ['insight-symbol-categories'],
 'ProjectFolderWindow/ListDisplay': ['insight-project-folders', 'insight-disk-files'],
 'WindowListWindow/ListDisplay': ['insight-window-list'],
 'RelationWindow/ListDisplay': ['insight-relations'],
}
CATEGORIES = {'keyword': 'Keyword', 'comment': 'Comment', 'string': 'String',
              'number': 'Number', 'operator': 'Operator', 'type': 'Ref to Class',
              'preprocessor': 'Preprocessor', 'line-number': 'Line Number'}


def font_stack(name):
 fallback = 'Noto Sans Mono' if name == 'Consolas' else 'Noto Sans'
 return f'{json.dumps(name)}, ' + ('' if fallback.endswith('Mono') or name == 'Verdana' else '"Verdana", ') + f'"{fallback}", ' + ('monospace' if fallback.endswith('Mono') else 'sans-serif')


def properties(attrs):
 out = []
 for key, prop in [('TextColor', 'color'), ('BackColor', 'background-color')]:
  if key in attrs:
   if not re.fullmatch(r'#[0-9a-fA-F]{6}', attrs[key]): raise ValueError('Invalid color')
   out.append(f'{prop}: {attrs[key]};')
 if attrs.get('FontName'): out.append(f'font-family: {font_stack(attrs["FontName"])};')
 for key, prop, on, off in [('Bold','font-weight','bold','normal'), ('Italic','font-style','italic','normal')]:
  if key in attrs: out.append(f'{prop}: {on if attrs[key] == "1" else off};')
 if 'Underline' in attrs: out.append('text-decoration-line: ' + ('none' if attrs['Underline'] in ('None','0') else 'underline') + ';')
 if 'Scale' in attrs: out.append(f'font-size: {int(attrs["Scale"])}%;')
 return ' '.join(out)


def convert(source, destination):
 root = ET.parse(source).getroot()
 if root.tag != 'SourceInsightVisualThemes': raise ValueError('Not a Source Insight theme export')
 names = []
 for theme in root.findall('./ThemeList/Theme'):
  name = theme.attrib['Name']
  if name in ('.','..') or '/' in name or '\\' in name or name in names: raise ValueError('Unsafe/duplicate name')
  names.append(name)
  folder = destination / name; folder.mkdir(parents=True, exist_ok=True)
  definitions = {s.attrib['Name']: dict(s.attrib) for s in theme.findall('./Styles/Style')}
  def resolve(key, seen=()):
   if key in seen: raise ValueError('Style inheritance cycle')
   attrs = definitions.get(key, {})
   parent = attrs.get('ParentStyle')
   return (resolve(parent, (*seen,key)) if parent else {}) | attrs
  colors = {i.get('Name'): i.get('Color') for i in theme.findall('./DisplayColors/Item')}
  base = {'TextColor': colors['DefaultText']} if 'DefaultText' in colors else {}
  if 'WindowBackground' in colors: base['BackColor'] = colors['WindowBackground']
  css = ['/* Generated from Source Insight ' + root.get('AppVer','') + '; editable CSS. */',
         'editor { font-family: "Consolas", "Noto Sans Mono", monospace; font-size: 12pt; font-weight: normal; font-style: normal; text-decoration-line: none; ' + properties(base | resolve('Default Text')) + ' }']
  for category, original in CATEGORIES.items(): css.append(f'.{category} {{ {properties(resolve(original))} }}')
  for original, attrs in definitions.items():
   slug = re.sub('[^a-z0-9]+', '-', original.lower()).strip('-')
   css += ['/* Source attributes: ' + json.dumps(attrs,ensure_ascii=False).replace('*/','* /') + ' */',
           f'.si-{slug} {{ {properties(resolve(original))} }}']
  (folder/'editor.css').write_text('\n'.join(css)+'\n')
  panels = ['/* GTK3 panel CSS. Local Font & Color overrides have higher priority. */']
  default = theme.find('./DisplayColors/DefaultPanelFontAndColor')
  if default is not None:
   attrs = dict(default.attrib);font = default.find('Font')
   if font is not None: attrs['FontName'] = font.get('name','Segoe UI')
   rules = properties(attrs)
   if font is not None: rules += f' font-size: {font.get("psize","8")}pt;'
   panels.append('.insight-visual-theme treeview.view:not(:selected) { '+rules+' }')
  for path, ids in PANELS.items():
   node = theme.find(path)
   if node is None: continue
   attrs = dict(node.attrib);font = node.find('Font')
   if font is not None: attrs['FontName'] = font.get('name','Segoe UI')
   rules = properties(attrs)
   if font is not None: rules += f' font-size: {font.get("psize","8")}pt;'
   panels.append(', '.join(f'#{id}:not(:selected)' for id in ids)+' { '+rules+' }')
  if colors:
   if 'FrameBackground' in colors:
    panels.append('.insight-visual-theme { background-color: '+colors['FrameBackground']+'; color: '+colors.get('FrameText','#202020')+'; }')
   for node in ['toolbar','menubar','notebook > header','statusbar']:
    panels.append('.insight-visual-theme '+node+' { background-color: '+colors.get('FrameBackground','#eeeeee')+'; color: '+colors.get('FrameText','#202020')+'; }')
  (folder/'panels.css').write_text('\n'.join(panels)+'\n')
 return names


if __name__ == '__main__':
 names=convert(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'styles/source/default.themes.xml',Path(sys.argv[2]) if len(sys.argv)>2 else ROOT/'styles')
 print('Converted',len(names),'themes')
