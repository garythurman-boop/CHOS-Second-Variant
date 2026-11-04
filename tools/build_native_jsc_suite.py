#!/usr/bin/env python3
"""Link original native Test262/checked-Wasm/watchdog probes;never execute them."""
import argparse,pathlib,subprocess,json,hashlib,shutil,time
from prepare_test262 import prepare
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for n in ['source','native-sdk','engine-build','out']:p.add_argument('--'+n,type=pathlib.Path,required=True)
p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
source=o.source.resolve();sdk=o.native_sdk.resolve();engine=o.engine_build.resolve();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
started=time.monotonic();selection=prepare(root/'third_party/test262-subset',out/'generated');commands=[]
fixture_sources={str(f.relative_to(root)):sha(f) for f in sorted((root/'tests/jsc_native_suite').iterdir()) if f.is_file()}
def run(command,name):
 commands.append(command)
 with (out/name).open('wb') as f:return subprocess.run(command,stdout=f,stderr=subprocess.STDOUT).returncode
code=run(['python3',str(root/'tools/probe_jsc_vm_link.py'),'--source',str(source),'--native-sdk',str(sdk),'--engine-build',str(engine),'--out',str(out/'base-link'),'--application-profile','engine','--cmake',o.cmake,'--ninja',o.ninja],'base-link.log')
base=None
if not code:
 base=json.loads((out/'base-link/result.json').read_text());assert not base['undefined_symbols'] and len(base['engine_archive_members'])==482 and len(base['original_subtarget_objects'])==53
 private=source/'Source/JavaScriptCore/API/JSContextRefPrivate.h'
 original=subprocess.check_output(['git','show',base['upstream_commit']+':Source/JavaScriptCore/API/JSContextRefPrivate.h'],cwd=source)
 assert private.read_bytes()==original
 code=run([o.cmake,'-S',str(root/'tests/jsc_native_suite'),'-B',str(out/'cmake'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(pathlib.Path(shutil.which(o.ninja)).resolve()),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCHOS_APPLICATION_PROFILE=engine','-DENGINE_BUILD='+str(engine),'-DWEBKIT_SOURCE='+str(source),'-DNATIVE_TEST_INPUTS='+str(out/'generated'),'-DJSC_LINK_OBJECTS_FILE='+str(out/'base-link/link-objects.cmake'),'-DCMAKE_BUILD_TYPE=Release'],'configure.log')
 if not code:code=run([o.cmake,'--build',str(out/'cmake'),'-j2'],'build.log')
 for member in base['engine_archive_members']+base['original_subtarget_objects']:
  f=engine/member['path'];assert f.stat().st_size==member['bytes'] and sha(f)==member['sha256']
for name,value in fixture_sources.items():assert sha(root/name)==value,name
report={'status':'BUILT_NOT_EXECUTED' if not code else 'BUILD_FAILED','returncode':code,'stage3_complete':False,'selection':selection,'commands':commands,'fixture_sources_sha256':fixture_sources,'sdk_manifest_sha256':sha(sdk/'chos-sdk.json'),'seconds':time.monotonic()-started,'limits':['Fixed88original Test262 scripts/172variants,not full conformance;no execution yet','Actual checked Wasm modules and CPU watchdog probes are linked,not qualified by linking','Full original ICU and535 original engine objects;no host JavaScript substituted','Modern WebCore/browser,WiFi,security and independent hardware gates remain open']}
if not code:
 app=out/'cmake/jsc_native_suite';assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
 report['application']={'bytes':app.stat().st_size,'sha256':sha(app)}
 report['private_API_header_sha256']=sha(private);report['engine_link_manifest_sha256']=sha(out/'base-link/result.json')
 code=run([str(out/'base-link/validate-native-elf'),str(app),'1'],'loader-policy.json')
 if code:report.update(status='LOADER_POLICY_FAILED',returncode=code)
 else:report['loader_policy']=json.loads((out/'loader-policy.json').read_text())
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['status','returncode','selection']},indent=2));raise SystemExit(code)
