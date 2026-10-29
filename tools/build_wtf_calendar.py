#!/usr/bin/env python3
"""Link actual upstream WTF calendar,UTF and native process helpers."""
import argparse,hashlib,json,pathlib,shutil,subprocess,time
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for name in ('source','native-sdk','engine-build','out'):p.add_argument('--'+name,type=pathlib.Path,required=True)
p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
source=o.source.resolve();sdk=o.native_sdk.resolve();engine=o.engine_build.resolve();out=o.out.resolve()
m=json.loads((root/'docs/dependencies/webkit-jsc-bringup.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=source)
assert git('rev-parse','HEAD').decode().strip()==m['commit'] and not git('diff','--cached','--name-only').strip()
assert set(git('diff','--name-only').decode().splitlines())==set(m['patched_files_sha256'])
for name,wanted in m['source_files_sha256'].items():assert hashlib.sha256(git('show',m['commit']+':'+name)).hexdigest()==wanted
for name,wanted in m['patched_files_sha256'].items():assert sha(source/name)==wanted
for name,wanted in m['native_port_files_sha256'].items():assert sha(root/name)==wanted
assert sha(root/m['patch'])==m['patch_sha256']
subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
assert (engine/'cmakeconfig.h').is_file()
cache=(engine/'CMakeCache.txt').read_text()
systems=list((engine/'CMakeFiles').glob('*/CMakeSystem.cmake'));assert len(systems)==1
system=systems[0].read_text();assert 'set(CMAKE_SYSTEM_NAME "CHOS")' in system and 'set(CMAKE_SYSTEM_PROCESSOR "aarch64")' in system
assert 'CHOS_WTF_PLATFORM_DIR:UNINITIALIZED='+str(root/'ports/webkit/chos') in cache
out.mkdir(parents=True,exist_ok=False)
commands=[[o.cmake,'-S',str(root/'tests/wtf_calendar'),'-B',str(out/'cmake'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(pathlib.Path(shutil.which(o.ninja)).resolve()),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DWEBKIT_SOURCE='+str(source),'-DENGINE_BUILD='+str(engine),'-DCMAKE_BUILD_TYPE=Release'],[o.cmake,'--build',str(out/'cmake'),'-j4']]
code=0;started=time.monotonic()
for i,command in enumerate(commands):
 with (out/('configure.log' if not i else 'build.log')).open('wb') as log:r=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
 code=r.returncode
 if code:break
report={'status':'BUILT_NOT_EXECUTED' if not code else 'BUILD_FAILED','stage3_complete':False,'engine_built':False,'engine_executed':False,'upstream_commit':m['commit'],'patch_sha256':m['patch_sha256'],'native_port_files_sha256':m['native_port_files_sha256'],'sdk_manifest_sha256':sha(sdk/'chos-sdk.json'),'upstream_configuration_sha256':sha(engine/'cmakeconfig.h'),'commands':commands,'returncode':code,'seconds':time.monotonic()-started,'application_sha256':sha(out/'cmake/wtf_calendar') if not code else None,'scope':'actual upstream DateMath/current-time/SIMDUTF/WTFProcess units and native UTC/error/sleep helpers','limits':['no full WTF/JSC library,JavaScript or rendering','thread lifecycle/stack discovery/GC suspension remain separate services;no signal emulation','UTC-only calendar;explicit DST unsupported;C byte locale only;not an Intl/browser or time trust qualification','native guest tests required;no new physical/Fusion qualification']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(code)
