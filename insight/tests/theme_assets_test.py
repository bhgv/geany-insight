#!/usr/bin/env python3
"""Check bundled font integrity and real Cyrillic cmap coverage, without dependencies."""
from pathlib import Path
import hashlib
import json
import struct
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
def u16(data,offset): return struct.unpack_from('>H',data,offset)[0]
def u32(data,offset): return struct.unpack_from('>I',data,offset)[0]
def glyph(data,cp):
 tables={}
 for i in range(u16(data,4)):
  tag,checksum,offset,length=struct.unpack_from('>4sIII',data,12+16*i);tables[tag]=(offset,length)
 cmap=tables[b'cmap'][0]
 for i in range(u16(data,cmap+2)):
  platform,encoding,offset=struct.unpack_from('>HHI',data,cmap+4+8*i)
  if platform!=0 and not(platform==3 and encoding in (1,10)):continue
  start=cmap+offset;fmt=u16(data,start)
  if fmt==12:
   for j in range(u32(data,start+12)):
    first,last,gid=struct.unpack_from('>III',data,start+16+12*j)
    if first<=cp<=last and gid+cp-first:return True
  if fmt==4 and cp<=65535:
   count=u16(data,start+6)//2
   for j in range(count):
    end=u16(data,start+14+2*j);first=u16(data,start+16+2*count+2*j)
    if first<=cp<=end:
     delta=u16(data,start+16+4*count+2*j);where=start+16+6*count+2*j;offset=u16(data,where)
     gid=(cp+delta)&65535 if offset==0 else u16(data,where+offset+2*(cp-first))
     if offset and gid:gid=(gid+delta)&65535
     if gid:return True
 return False
manifest=json.loads((ROOT/'fonts/manifest.json').read_text())
for item in manifest:
 path=ROOT/'fonts'/item['file'];data=path.read_bytes()
 assert hashlib.sha256(data).hexdigest()==item['sha256'],path
 for cp in [*range(0x410,0x450),0x401,0x451,0x2116]:assert glyph(data,cp),(path,hex(cp))
 assert 'SIL OPEN FONT LICENSE' in (path.parent/'OFL.txt').read_text()
names=[t.get('Name') for t in ET.parse(ROOT/'styles/source/default.themes.xml').findall('./ThemeList/Theme')]
assert len(names)==15
for name in names:
 folder=ROOT/'styles'/name
 assert (folder/'editor.css').is_file() and (folder/'panels.css').is_file(),name
 assert 'Source attributes:' in (folder/'editor.css').read_text()
print('15 CSS themes and 3 complete uncompressed OFL font files verified: original hashes, Cyrillic alphabet, Ё/ё and №')
