#!/usr/bin/env python3
"""Build and test without CMake; requires a C99 compiler."""
from pathlib import Path
import os, subprocess
root=Path(__file__).resolve().parents[1]
build=root/'build-test'; build.mkdir(exist_ok=True)
lua=[str(p) for p in sorted((root/'vendor/lua').glob('*.c')) if p.name not in {'lua.c','luac.c','onelua.c','linit.c','liolib.c','loslib.c','loadlib.c','lcorolib.c','ldblib.c'}]
cmd=[os.environ.get('CC','cc'),'-std=c99','-O1','-g','-Wall','-Wextra','-I'+str(root/'core'),'-I'+str(root/'vendor/lua'),str(root/'core/runtime.c'),str(root/'core/index.c'),str(root/'tests/runtime_test.c'),*lua,'-lm','-o',str(build/'runtime_test')]
if os.environ.get('SANITIZE'): cmd[1:1]=['-fsanitize='+os.environ['SANITIZE'],'-fno-omit-frame-pointer']
subprocess.run(cmd,check=True)
subprocess.run([str(build/'runtime_test')],check=True)

index_cmd=[os.environ.get('CC','cc'),'-std=c99','-O1','-g','-Wall','-Wextra','-I'+str(root/'core'),str(root/'core/index.c'),str(root/'tests/index_test.c'),'-o',str(build/'index_test')]
if os.environ.get('SANITIZE'): index_cmd[1:1]=['-fsanitize='+os.environ['SANITIZE'],'-fno-omit-frame-pointer']
subprocess.run(index_cmd,check=True)
subprocess.run([str(build/'index_test')],check=True)

document_cmd=[os.environ.get('CC','cc'),'-std=c99','-O1','-g','-Wall','-Wextra','-I'+str(root/'core'),'-I'+str(root/'vendor/lua'),str(root/'core/document.c'),str(root/'core/runtime.c'),str(root/'core/index.c'),str(root/'tests/document_test.c'),*lua,'-lm','-o',str(build/'document_test')]
if os.environ.get('SANITIZE'): document_cmd[1:1]=['-fsanitize='+os.environ['SANITIZE'],'-fno-omit-frame-pointer']
subprocess.run(document_cmd,check=True)
subprocess.run([str(build/'document_test')],check=True)

snapshot_cmd=[os.environ.get('CC','cc'),'-std=c99','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(root/'core'),str(root/'core/index.c'),str(root/'tests/snapshot_test.c'),'-o',str(build/'snapshot_test')]
if os.environ.get('SANITIZE'): snapshot_cmd[1:1]=['-fsanitize='+os.environ['SANITIZE'],'-fno-omit-frame-pointer']
subprocess.run(snapshot_cmd,check=True)
subprocess.run([str(build/'snapshot_test')],check=True)
