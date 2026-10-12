#!/usr/bin/env python3
"""Build and test the portable core without CMake; requires a C99 compiler."""
from pathlib import Path
import concurrent.futures
import os
import subprocess
root=Path(__file__).resolve().parents[1]
build=root/'build-test'; build.mkdir(exist_ok=True)
excluded={'lua.c','luac.c','onelua.c','linit.c','liolib.c','loslib.c','loadlib.c','lcorolib.c','ldblib.c'}
sources=[root/'core'/name for name in ['runtime.c','index.c','document.c','context.c','occurrences.c']]
sources+=sorted(p for p in (root/'vendor/lua').glob('*.c') if p.name not in excluded)
common=[os.environ.get('CC','cc'),'-std=c99','-O1','-g','-Wall','-Wextra','-Werror','-UNDEBUG','-I'+str(root/'core'),'-I'+str(root/'vendor/lua')]
if os.environ.get('SANITIZE'):common+=['-fsanitize='+os.environ['SANITIZE'],'-fno-omit-frame-pointer']
def compile_one(source):
    obj=build/(source.parent.name+'_'+source.stem+'.o')
    subprocess.run(common+['-c',str(source),'-o',str(obj)],check=True)
    return str(obj)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    objects=list(pool.map(compile_one,sources))
for name in ['runtime','index','document','snapshot','commands','editor_api','events','context','occurrences']:
    output=build/(name+'_test')
    subprocess.run(common+[str(root/'tests'/(name+'_test.c')),*objects,'-lm','-o',str(output)],check=True)
    subprocess.run([str(output)],check=True)
