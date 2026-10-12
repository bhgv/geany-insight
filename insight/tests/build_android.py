#!/usr/bin/env python3
"""Build this dependency-free Android app with official SDK/NDK tools offline.
No Gradle dependency resolution is needed. This produces a DEBUG-signed APK.
"""
import argparse, os, subprocess, zipfile, shutil
from pathlib import Path
import xml.etree.ElementTree as ET
parser=argparse.ArgumentParser()
parser.add_argument('--ndk',type=Path,required=True)
parser.add_argument('--cmake',type=Path,required=True)
parser.add_argument('--ninja',type=Path,required=True)
parser.add_argument('--build-tools',type=Path,required=True)
parser.add_argument('--android-jar',type=Path,required=True)
parser.add_argument('--jdk',type=Path,required=True)
parser.add_argument('--abis',nargs='+',default=['arm64-v8a'])
args=parser.parse_args()
for field in ['ndk','cmake','ninja','build_tools','android_jar','jdk']:
 setattr(args,field,getattr(args,field).resolve())
root=Path(__file__).resolve().parents[1]
build=root/'build-android-apk'; build.mkdir(exist_ok=True)
env=os.environ.copy(); env['JAVA_HOME']=str(args.jdk)
env['PATH']=str(args.jdk/'bin')+os.pathsep+env.get('PATH','')
env['LD_LIBRARY_PATH']=str(args.jdk/'lib')+os.pathsep+str(args.build_tools/'lib64')+os.pathsep+env.get('LD_LIBRARY_PATH','')
env['TMPDIR']=str(build)
def run(command): subprocess.run([str(x) for x in command],env=env,check=True)
for abi in args.abis:
 if abi not in ['arm64-v8a','armeabi-v7a','x86_64']: raise ValueError('Unsupported ABI: '+abi)
 native=build/abi
 run([args.cmake,'-S',root,'-B',native,'-G','Ninja',
  '-DCMAKE_MAKE_PROGRAM='+str(args.ninja),'-DCMAKE_TOOLCHAIN_FILE='+str(args.ndk/'build/cmake/android.toolchain.cmake'),
  '-DANDROID_ABI='+abi,'-DANDROID_PLATFORM=android-26','-DCMAKE_BUILD_TYPE=Release'])
 run([args.cmake,'--build',native,'--target','insight_android','-j','4'])
classes=build/'classes'; shutil.rmtree(classes,ignore_errors=True); classes.mkdir()
sources=list((root/'android/app/src/main/java').rglob('*.java'))
run([args.jdk/'bin/javac','-Xlint:all','-Werror','-cp',args.android_jar,'-d',classes,*sources])
dex=build/'dex'; shutil.rmtree(dex,ignore_errors=True); dex.mkdir()
run(['bash',args.build_tools/'d8','--release','--min-api','26','--lib',args.android_jar,'--output',dex,*classes.rglob('*.class')])
manifest=ET.parse(root/'android/app/src/main/AndroidManifest.xml')
manifest.getroot().set('package','org.geany.insight')
ET.register_namespace('android','http://schemas.android.com/apk/res/android')
manifest.write(build/'AndroidManifest.xml',encoding='utf-8',xml_declaration=True)
unsigned=build/'unsigned.apk'; unsigned.unlink(missing_ok=True)
run([args.build_tools/'aapt2','link','-I',args.android_jar,'--manifest',build/'AndroidManifest.xml',
 '--min-sdk-version','26','--target-sdk-version','35','--version-code','3','--version-name','0.3.0','-o',unsigned])
with zipfile.ZipFile(unsigned,'a') as z:
 for file in dex.glob('*.dex'): z.write(file,file.name,compress_type=zipfile.ZIP_DEFLATED)
 for abi in args.abis: z.write(build/abi/'libinsight_android.so','lib/'+abi+'/libinsight_android.so',compress_type=zipfile.ZIP_STORED)
aligned=build/'aligned.apk'; aligned.unlink(missing_ok=True)
run([args.build_tools/'zipalign','-P','16','-f','4',unsigned,aligned])
key=build/'debug.p12'
if not key.exists():
 run([args.jdk/'bin/keytool','-genkeypair','-keystore',key,'-storetype','PKCS12','-storepass','android',
  '-keypass','android','-alias','androiddebugkey','-keyalg','RSA','-keysize','2048',
  '-validity','10000','-dname','CN=Android Debug,O=Android,C=US'])
apk=build/'geany-insight-0.3-debug.apk'
run(['bash',args.build_tools/'apksigner','sign','--ks',key,'--ks-pass','pass:android',
 '--key-pass','pass:android','--out',apk,aligned])
run(['bash',args.build_tools/'apksigner','verify','--verbose',apk])
run([args.build_tools/'zipalign','-c','-P','16','4',apk])
run([args.build_tools/'aapt2','dump','badging',apk])
print('APK:',apk)
