#!/usr/bin/env python3
"""Build actual native WTF allocator APIs in an explicit engine executable."""
import argparse,pathlib,json,hashlib,subprocess,shutil,time
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for n in ['source','native-sdk','engine-build','out']:p.add_argument('--'+n,type=pathlib.Path,required=True)
p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
source=o.source.resolve();sdk=o.native_sdk.resolve();engine=o.engine_build.resolve();out=o.out.resolve()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(*a):return subprocess.check_output(['git',*a],cwd=source)
m=json.loads((root/'docs/dependencies/webkit-jsc-bringup.json').read_text())
assert git('rev-parse','HEAD').decode().strip()==m['commit'] and not git('diff','--cached','--name-only').strip()
assert set(git('diff','--name-only').decode().splitlines())==set(m['patched_files_sha256'])
for n,h in m['source_files_sha256'].items():assert hashlib.sha256(git('show',m['commit']+':'+n)).hexdigest()==h
for n,h in m['patched_files_sha256'].items():assert sha(source/n)==h
for n,h in m['native_port_files_sha256'].items():assert sha(root/n)==h
assert sha(root/m['patch'])==m['patch_sha256']
subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
systems=list((engine/'CMakeFiles').glob('*/CMakeSystem.cmake'));assert len(systems)==1
system=systems[0].read_text();assert 'set(CMAKE_SYSTEM_NAME "CHOS")' in system and 'set(CMAKE_SYSTEM_PROCESSOR "aarch64")' in system
cache=(engine/'CMakeCache.txt').read_text()
assert 'CHOS_SDK_ROOT:UNINITIALIZED='+str(sdk) in cache and 'CHOS_WTF_PLATFORM_DIR:UNINITIALIZED='+str(root/'ports/webkit/chos') in cache
assert 'USE_SYSTEM_MALLOC:BOOL=ON' in cache
out.mkdir(parents=True,exist_ok=False);started=time.monotonic()
commands=[[o.cmake,'--build',str(engine),'--target','WTF','-j2'],[o.cmake,'-S',str(root/'tests/wtf_allocator'),'-B',str(out/'cmake'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(pathlib.Path(shutil.which(o.ninja)).resolve()),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCHOS_APPLICATION_PROFILE=engine','-DWEBKIT_SOURCE='+str(source),'-DENGINE_BUILD='+str(engine),'-DCMAKE_BUILD_TYPE=Release'],[o.cmake,'--build',str(out/'cmake'),'-j2']]
code=0
for i,cmd in enumerate(commands):
 with (out/['engine-build.log','configure.log','build.log'][i]).open('wb') as f:code=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT).returncode
 if code:break
report={'status':'BUILT_NOT_EXECUTED' if not code else 'BUILD_FAILED','returncode':code,'stage3_complete':False,'upstream_commit':m['commit'],'patch_sha256':m['patch_sha256'],'sdk_manifest_sha256':sha(sdk/'chos-sdk.json'),'commands':commands,'seconds':time.monotonic()-started,'limits':['Selected actual WTF allocator APIs;not full library,JS/Wasm/browser qualification','Bootstrap ICU dependency for this component only;full Intl VM separately qualified']}
if not code:
 app=out/'cmake/wtf_allocator';assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
 report['application_sha256']=sha(app);report['file_bytes']=app.stat().st_size
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(code)
