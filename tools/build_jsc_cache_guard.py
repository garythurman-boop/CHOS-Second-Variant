#!/usr/bin/env python3
"""Build and check the actual ARM64 no-JIT cache-publication boundary and ELF conventions."""
import argparse,hashlib,json,pathlib,shutil,subprocess,time,struct,shlex,re
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
assert 'CHOS_SDK_ROOT:UNINITIALIZED='+str(sdk) in cache
out.mkdir(parents=True,exist_ok=False)
commands=[[o.cmake,'--build',str(engine),'--target','WTF','-j4'],[o.cmake,'-S',str(root/'tests/jsc_cache_guard'),'-B',str(out/'cmake'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(pathlib.Path(shutil.which(o.ninja)).resolve()),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DWEBKIT_SOURCE='+str(source),'-DENGINE_BUILD='+str(engine),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'],[o.cmake,'--build',str(out/'cmake'),'-j4']]
code=0;started=time.monotonic()
for i,command in enumerate(commands):
 with (out/(['engine-build.log','configure.log','build.log'][i])).open('wb') as log:r=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
 code=r.returncode
 if code:break
deployment = None
if not code:
 object_symbols=subprocess.check_output(['llvm-readelf','-sW',str(out/'cmake/CMakeFiles/jsc_cache_guard.dir/main.cpp.obj')],text=True)
 assert re.search(r'GLOBAL\s+HIDDEN\s+\S+\s+chos_jsc_guard_address\b',object_symbols),'native ELF object lacks hidden global symbol'
 full = out/'cmake/jsc_cache_guard'
 deployment = out/'cmake/jsc_cache_guard.elf'
 subprocess.run(['llvm-objcopy','--strip-debug',str(full),str(deployment)],check=True)
 def loaded(path):
  data=path.read_bytes();assert data[:6]==b'\x7fELF\x02\x01'
  h=struct.unpack_from('<HHIQQQIHHHHHH',data,16);assert h[1]==183 and h[8]==56
  values=[]
  for i in range(h[9]):
   t,f,o,v,p,n,m,a=struct.unpack_from('<IIQQQQQQ',data,h[4]+i*h[8])
   if t not in (1,4):continue
   if t==1:assert o>=64+h[9]*h[8] and not(f&1 and f&2)
   values.append((t,f,v,p,n,m,a,hashlib.sha256(data[o:o+n]).hexdigest()))
  return h[3],values
 assert loaded(full)==loaded(deployment),'strip-debug changed native loaded segments or ABI notes'
 assert deployment.stat().st_size<=4*1024*1024
negative = None
if not code:
 probe=out/'jit-enabled-negative.cpp'
 probe.write_text('#include "config.h"\n#undef ENABLE_JIT\n#define ENABLE_JIT 1\n#include <JavaScriptCore/ARM64Assembler.h>\n')
 entry=json.loads((out/'cmake/compile_commands.json').read_text())[0]
 args=shlex.split(entry['command']);filtered=[];skip=False
 for arg in args:
  if skip:skip=False;continue
  if arg=='-o':skip=True;continue
  if arg in ('-c',entry['file']):continue
  filtered.append(arg)
 command=filtered+['-fsyntax-only',str(probe)]
 with (out/'jit-enabled-negative.log').open('wb') as log:r=subprocess.run(command,cwd=entry['directory'],stdout=log,stderr=subprocess.STDOUT)
 assert r.returncode and 'The cacheFlush support is missing on this platform.' in (out/'jit-enabled-negative.log').read_text(), 'JIT-enabled profile lost its explicit unsupported-backend diagnostic'
 negative={'expected_failure':True,'returncode':r.returncode,'command':command,'log_sha256':sha(out/'jit-enabled-negative.log')}
report={'status':'BUILT_NOT_EXECUTED' if not code else 'BUILD_FAILED','stage3_complete':False,'engine_built':False,'engine_executed':False,'upstream_commit':m['commit'],'patch_sha256':m['patch_sha256'],'sdk_manifest_sha256':sha(sdk/'chos-sdk.json'),'configuration_sha256':sha(engine/'cmakeconfig.h'),'commands':commands,'returncode':code,'seconds':time.monotonic()-started,'application_sha256':sha(deployment) if deployment else None,'deployment':str(deployment) if deployment else None,'debug_application_sha256':sha(out/'cmake/jsc_cache_guard') if not code else None,'strip_verification':'entry,loaded segment addresses/flags/bytes and ABI notes identical' if deployment else None,'negative_JIT_probe':negative,'scope':'actual ARM64 header fail-closed cache publication and real ELF GOT/local labels;not an executable allocator or JavaScript runtime','limits':['no dynamic code publication/cache flush backend or JIT support','metadata extractors/assembly compilation do not establish JavaScript/browser execution','guest fault containment and GOT addresses require separate qualification;no Fusion/Pi5/laptop claim']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(code)
