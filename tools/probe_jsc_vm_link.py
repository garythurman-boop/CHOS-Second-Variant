#!/usr/bin/env python3
"""Compile the original JSC static framework closure and diagnose a real full-ICU VM link.

This tool never executes or publishes its output. A failed link is a recorded
blocker, not a passing VM test; the original native arena assertion stays active.
"""
import argparse,hashlib,json,pathlib,re,shutil,subprocess,time
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for n in ('source','native-sdk','engine-build','out'):p.add_argument('--'+n,type=pathlib.Path,required=True)
p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
source=o.source.resolve();sdk=o.native_sdk.resolve();engine=o.engine_build.resolve();out=o.out.resolve()
m=json.loads((root/'docs/dependencies/webkit-jsc-bringup.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(*a):return subprocess.check_output(['git',*a],cwd=source)
assert git('rev-parse','HEAD').decode().strip()==m['commit'] and not git('diff','--cached','--name-only').strip()
assert set(git('diff','--name-only').decode().splitlines())==set(m['patched_files_sha256'])
for n,v in m['source_files_sha256'].items():assert hashlib.sha256(git('show',m['commit']+':'+n)).hexdigest()==v
for n,v in m['patched_files_sha256'].items():assert sha(source/n)==v
for n,v in m['native_port_files_sha256'].items():assert sha(root/n)==v
assert sha(root/m['patch'])==m['patch_sha256']
subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
cache=(engine/'CMakeCache.txt').read_text();systems=list((engine/'CMakeFiles').glob('*/CMakeSystem.cmake'));assert len(systems)==1
system=systems[0].read_text();assert 'set(CMAKE_SYSTEM_NAME "CHOS")' in system and 'set(CMAKE_SYSTEM_PROCESSOR "aarch64")' in system
assert 'CHOS_SDK_ROOT:UNINITIALIZED='+str(sdk) in cache and 'CHOS_WTF_PLATFORM_DIR:UNINITIALIZED='+str(root/'ports/webkit/chos') in cache
for n,v in [('ENABLE_JIT','OFF'),('ENABLE_DFG_JIT','OFF'),('ENABLE_FTL_JIT','OFF'),('ENABLE_SAMPLING_PROFILER','OFF'),('ENABLE_WEBASSEMBLY','ON'),('USE_SYSTEM_MALLOC','ON')]:assert n+':BOOL='+v in cache
ninja=pathlib.Path(shutil.which(o.ninja)).resolve();out.mkdir(parents=True,exist_ok=False)
commands=[];started=time.monotonic();objects=[];code=0;phase='engine-build';sections=[]
def run(command,log):
 commands.append(command)
 with (out/log).open('wb') as f:return subprocess.run(command,stdout=f,stderr=subprocess.STDOUT).returncode
code=run([o.cmake,'--build',str(engine),'--target','JavaScriptCore','JavaScriptCoreJIT','-j2'],'engine-build.log')
if not code:
 def inputs(target,label):
  result=subprocess.check_output([str(ninja),'-C',str(engine),'-t','query',target],text=True);(out/label).write_text(result)
  lines=result.split('  input:',1)[1].split('  outputs:',1)[0].splitlines()[1:]
  return {(engine/v.strip().lstrip('| ').strip()).resolve() for v in lines if v.strip().endswith('.obj')}
 actual=inputs('Source/JavaScriptCore/JavaScriptCoreJIT','subtarget-query.txt');consumer=inputs('bin/jsc','upstream-consumer-query.txt');other=consumer-actual
 # Check the original framework consumer's transitive interface, not a glob of
 # whatever stale object files happen to exist. The only excluded object is its
 # shell main; it is replaced by this fixture's actual public-API main.
 assert len(actual)==53 and actual<=consumer and len(other)==1 and next(iter(other)).name=='jsc.cpp.obj'
 for path in sorted(actual):
  assert path.is_file() and path.is_relative_to(engine)
  objects.append({'path':str(path.relative_to(engine)),'bytes':path.stat().st_size,'sha256':sha(path)})
 (out/'link-objects.cmake').write_text('set(JSC_LINK_OBJECTS\n'+''.join('  "'+str(engine/v['path'])+'"\n' for v in objects)+')\n')
 phase='configure'
 code=run([o.cmake,'-S',str(root/'tests/jsc_vm_probe'),'-B',str(out/'cmake'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DENGINE_BUILD='+str(engine),'-DJSC_LINK_OBJECTS_FILE='+str(out/'link-objects.cmake'),'-DCMAKE_BUILD_TYPE=Release'],'configure.log')
 if not code:
  phase='link';code=run([o.cmake,'--build',str(out/'cmake'),'-j2'],'link.log')
log=(out/'link.log').read_text() if (out/'link.log').exists() else ''
mapfile=out/'cmake/jsc_vm_probe.map'
if mapfile.exists():
 lines=mapfile.read_text().splitlines()
 assert lines and 'Out' in lines[0] and 'Symbol' in lines[0]
 out_column=lines[0].index('Out')
 for line in lines[1:]:
  # LLD indents input sections and symbols into later columns. Numeric fields
  # and a five-token row alone do not distinguish them from output sections.
  if len(line)<=out_column or line[out_column].isspace():continue
  fields=line.split()
  if len(fields)!=5:continue
  try:address,lma,size,align=(int(v,16) for v in fields[:4])
  except ValueError:continue
  if address<0x20000000 or address>=0x30000000:continue
  sections.append({'name':fields[4],'address':address,'bytes':size})
span=max((v['address']+v['bytes']-0x20000000 for v in sections),default=None)
report={'status':'LINKED_NOT_EXECUTED' if not code else phase.upper()+'_FAILED','returncode':code,'stage3_complete':False,'VM_executed':False,'JavaScript_executed':False,'Intl_executed':False,'runtime_admitted':False,'upstream_commit':m['commit'],'patch_sha256':m['patch_sha256'],'native_port_files_sha256':m['native_port_files_sha256'],'sdk_manifest_sha256':sha(sdk/'chos-sdk.json'),'configuration_sha256':sha(engine/'cmakeconfig.h'),'commands':commands,'seconds':time.monotonic()-started,'object_subtarget':'JavaScriptCoreJIT (includes bytecode/interpreter;JIT stays OFF)','original_subtarget_objects':objects,'libraries':{n:{'sha256':sha(d/n),'bytes':(d/n).stat().st_size} for d,n in [(engine/'lib','libJavaScriptCore.a'),(engine/'lib','libWTF.a'),(engine/'lib','libbmalloc.a'),(sdk/'lib','libicui18n.a'),(sdk/'lib','libicuuc.a'),(sdk/'lib','libicudata.a')]},'undefined_symbols':list(dict.fromkeys(re.findall(r'ld.lld: error: undefined symbol: (.+)',log))),'bounded_image_rejected':'Native application exceeds bounded4MiB arena' in log,'proposed_image_sections':sections,'proposed_image_span_bytes':span,'limits':['diagnostic link only;no engine output executed,published or admitted','original4MiB arena/linker assertion and native resource limits retained;no ignored symbols or bootstrap ICU substituted','full original CAPI VM creation,JavaScript/JSON,Intl and GC calls are linked;not evidence they execute','static framework consumers also require original separate object subtarget even with JIT/DFG/FTL OFF','foreign GC coordination,admitted resource profile,WebCore/browser,WiFi/security/platform gates remain open']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['status','returncode','undefined_symbols','bounded_image_rejected','proposed_image_span_bytes']},indent=2));raise SystemExit(code)
