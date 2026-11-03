#!/usr/bin/env python3
"""Build an actual engine-reservation or legacy-refusal application."""
import argparse,pathlib,json,hashlib,subprocess,shutil
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native-sdk',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--profile',choices=['engine','legacy'],default='engine');p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
sdk=o.native_sdk.resolve();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
commands=[[o.cmake,'-S',str(root/'tests/engine_reserve'),'-B',str(out/'cmake'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(pathlib.Path(shutil.which(o.ninja)).resolve()),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCHOS_APPLICATION_PROFILE='+o.profile,'-DCMAKE_BUILD_TYPE=Release'],[o.cmake,'--build',str(out/'cmake'),'-j2']]
code=0
for i,command in enumerate(commands):
 with (out/('configure.log' if i==0 else 'build.log')).open('wb') as f:code=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT).returncode
 if code:break
report={'status':'BUILT_NOT_EXECUTED' if not code else 'BUILD_FAILED','returncode':code,'profile':o.profile,'stage3_complete':False,'commands':commands,'sdk_manifest_sha256':hashlib.sha256((sdk/'chos-sdk.json').read_bytes()).hexdigest()}
if not code:
 app=out/'cmake/engine_reserve';assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
 report['application_sha256']=hashlib.sha256(app.read_bytes()).hexdigest();report['file_bytes']=app.stat().st_size
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(code)
