#!/usr/bin/env python3
"""Host JVM integration test of the exact Android JNI C implementation.
Requires JDK 17 (JAVA_HOME) and a Linux C compiler; does not verify ART/APK.
"""
import os, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
jdk=Path(os.environ['JAVA_HOME'])
build=root/'build-jni-test'; build.mkdir(exist_ok=True)
lua=[str(p) for p in sorted((root/'vendor/lua').glob('*.c')) if p.name not in {'lua.c','luac.c','onelua.c','linit.c','liolib.c','loslib.c','loadlib.c','lcorolib.c','ldblib.c'}]
cmd=[os.environ.get('CC','cc'),'-std=c99','-O1','-g','-Wall','-Wextra','-Werror','-fPIC','-shared','-I'+str(jdk/'include'),'-I'+str(jdk/'include/linux'),'-I'+str(root/'core'),'-I'+str(root/'vendor/lua'),str(root/'android/jni.c'),str(root/'core/document.c'),str(root/'core/runtime.c'),str(root/'core/occurrences.c'),str(root/'core/index.c'),*lua,'-lm','-o',str(build/'libinsight_android.so')]
subprocess.run(cmd,check=True)
subprocess.run([str(jdk/'bin/javac'),'-d',str(build),str(root/'android/app/src/main/java/org/geany/insight/NativeBridge.java'),str(root/'tests/NativeBridgeTest.java')],check=True)
subprocess.run([str(jdk/'bin/java'),'-Xcheck:jni','-Djava.library.path='+str(build),'-cp',str(build),'NativeBridgeTest'],check=True)
