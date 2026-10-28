#!/usr/bin/env python3
"""Build a real upstream VM header integration fixture with verified source/SDK."""
import argparse,pathlib,hashlib,json,subprocess,shutil,time
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=pathlib.Path,required=True);p.add_argument('--native-sdk',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
source=o.source.resolve();sdk=o.native_sdk.resolve();out=o.out.resolve();m=json.loads((root/'docs/dependencies/webkit-jsc-bringup.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=source)
assert git('rev-parse','HEAD').decode().strip()==m['commit']
assert not git('diff','--cached','--name-only').strip()
assert set(git('diff','--name-only').decode().splitlines())==set(m['patched_files_sha256'])
for name,wanted in m['source_files_sha256'].items():assert hashlib.sha256(git('show',m['commit']+':'+name)).hexdigest()==wanted
for name,wanted in m['patched_files_sha256'].items():assert sha(source/name)==wanted
assert sha(root/m['patch'])==m['patch_sha256']
subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
out.mkdir(parents=True,exist_ok=False)
commands=[[o.cmake,'-S',str(root/'tests/bmalloc_vm'),'-B',str(out/'cmake'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(pathlib.Path(shutil.which(o.ninja)).resolve()),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DWEBKIT_SOURCE='+str(source),'-DCMAKE_BUILD_TYPE=Release'],[o.cmake,'--build',str(out/'cmake'),'-j4']]
start=time.monotonic();code=0
for i,command in enumerate(commands):
 with (out/('configure.log' if not i else 'build.log')).open('wb') as f:r=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,cwd=root)
 code=r.returncode
 if code:break
report={'status':'BUILT_NOT_EXECUTED' if not code else 'BUILD_FAILED','returncode':code,'stage3_complete':False,'engine_built':False,'engine_executed':False,'upstream_commit':m['commit'],'patch_sha256':m['patch_sha256'],'sdk_manifest_sha256':sha(sdk/'chos-sdk.json'),'commands':commands,'seconds':time.monotonic()-start,'application_sha256':sha(out/'cmake/bmalloc_vm') if not code else None,'scope':'original inline VM helpers plus checked CHOS identity/discard adaptation;not bmalloc allocator or JavaScriptCore','limits':['bounded private anonymous subset;fixed/file/shared/executable maps unsupported','real guest tests are required separately;no full engine or browser/conformance claim']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(code)
