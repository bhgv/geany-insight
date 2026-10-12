#!/usr/bin/env python3
"""Optional smoke test of real Geany parsers; no GUI or project compilation.
GEANY_BIN selects the executable; GEANY_DATA_DIR optionally supplies filedefs.
This tests upstream extraction, not the navigation dialog or directory scanner.
"""
import os, shutil, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
build=root/'build-parser-test'; config=build/'config'; config.mkdir(parents=True,exist_ok=True)
data=os.environ.get('GEANY_DATA_DIR')
if data:
    data=Path(data)
    shutil.copy2(data/'filetype_extensions.conf',config/'filetype_extensions.conf')
    shutil.copytree(data/'filedefs',config/'filedefs',dirs_exist_ok=True)
fixtures={
 'c':'int insight_fixture(int x) { return x; }\n',
 'cpp':'namespace Demo { int insight_fixture(int x) { return x; } }\n',
 'py':'def insight_fixture(x):\n    return x\n',
 'lua':'function insight_fixture(x)\n return x\nend\n',
 'js':'function insight_fixture(x) { return x; }\n',
 'sh':'insight_fixture() { echo hello; }\n',
 'asm':'global insight_fixture\ninsight_fixture:\n ret\n',
 'mak':'insight_fixture:\n\t@echo hello\n',
}
for ext,source in fixtures.items():
    path=build/('fixture.'+ext); path.write_text(source)
    tags=build/('fixture.'+ext+'.tags'); tags.unlink(missing_ok=True)
    subprocess.run([os.environ.get('GEANY_BIN','geany'),'-c',str(config),'-P','-g',str(tags),str(path)],check=True)
    assert b'insight_fixture' in tags.read_bytes(), f'{ext}: expected symbol missing'
    print(f'Geany parser {ext}: passed')
