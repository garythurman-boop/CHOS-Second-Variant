#!/usr/bin/env python3
"""Build the real native writable working-copy application."""
import argparse,pathlib,subprocess,json,hashlib,shutil
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native-sdk',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
sdk=o.native_sdk.resolve();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
commands=[[o.cmake,'-S',str(root/'tests/native_write'),'-B',str(out/'cmake'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(pathlib.Path(shutil.which(o.ninja)).resolve()),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCMAKE_BUILD_TYPE=Release'],[o.cmake,'--build',str(out/'cmake'),'-j4']]
code=0
for i,cmd in enumerate(commands):
 with (out/('configure.log' if not i else 'build.log')).open('wb') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 code=r.returncode
 if code:break
sha=lambda q:hashlib.sha256(q.read_bytes()).hexdigest()
report={'status':'BUILD_FAILED' if code else 'BUILT_NOT_EXECUTED','stage3_complete':False,'sdk_manifest_sha256':sha(sdk/'chos-sdk.json'),'commands':commands,'returncode':code,'application_sha256':sha(out/'cmake/native_write') if not code else None,'limits':['native bounded working-copy descriptors;no full POSIX/engine/browser completion','native guest qualification required;no hardware target claim']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(code)
