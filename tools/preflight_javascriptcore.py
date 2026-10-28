#!/usr/bin/env python3
"""Run actual pinned JavaScriptCore CMake dependency triage, not an engine build.
The sole source patch is applied to the provided reference checkout if clean.
Missing dependencies return the original nonzero CMake status with an honest report.
"""
import argparse,hashlib,json,pathlib,re,shutil,subprocess,sys,time
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=pathlib.Path,required=True)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--native-sdk',type=pathlib.Path,help='use real executable links against this staged SDK');p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');p.add_argument('--ruby',default='ruby')
o=p.parse_args();source=o.source.resolve();out=o.out.resolve()
m=json.loads((root/'docs/dependencies/webkit-jsc-bringup.json').read_text())
def git(*args):return subprocess.check_output(['git',*args],cwd=source)
def sha(data):return hashlib.sha256(data).hexdigest()
if git('rev-parse','HEAD').decode().strip()!=m['commit']:p.error('source HEAD differs from the pinned inspected commit')
for name in m['required_source_roots']:
 if not (source/name).is_dir():p.error('incomplete JSCOnly checkout; missing source root: '+name)
closure=git('ls-tree','-r','--name-only',m['commit'],'--',*m['required_source_roots']).decode().splitlines()
missing=[name for name in closure if not (source/name).exists() and not (source/name).is_symlink()]
if missing:p.error('incomplete JSCOnly checkout; missing tracked source: '+missing[0])
for name,wanted in m['source_files_sha256'].items():
 if sha(git('show',m['commit']+':'+name))!=wanted:p.error('upstream source identity mismatch: '+name)
patch=root/m['patch']
if sha(patch.read_bytes())!=m['patch_sha256']:p.error('port patch hash mismatch')
if git('diff','--cached','--name-only').strip():p.error('reference checkout has staged changes')
changed=set(git('diff','--name-only').decode().splitlines())
if not changed:
 subprocess.run(['git','apply','--check',str(patch)],cwd=source,check=True)
 subprocess.run(['git','apply',str(patch)],cwd=source,check=True)
elif changed!=set(m['patched_files_sha256']):p.error('unexpected reference source changes')
for name,wanted in m['patched_files_sha256'].items():
 if sha((source/name).read_bytes())!=wanted:p.error('source differs from the recorded patch: '+name)
for name,wanted in m['source_files_sha256'].items():
 if name not in m['patched_files_sha256'] and sha((source/name).read_bytes())!=wanted:p.error('modified checked source: '+name)
programs={}
for name in ['cmake','ninja','ruby']:
 found=shutil.which(getattr(o,name))
 if not found:p.error('missing build tool: '+name)
 programs[name]=str(pathlib.Path(found).resolve())
for name in ['libcxx/include/__config_site','libm/include/math.h']:
 if not (root/'build/userland'/name).is_file():p.error('build native SDK prerequisites first: make libcxx libm')
out.mkdir(parents=True,exist_ok=False)
command=[programs['cmake'],'-S',str(source),'-B',str(out/'cmake'),'-G','Ninja',
 '-DCMAKE_MAKE_PROGRAM='+programs['ninja'],'-DRuby_EXECUTABLE='+programs['ruby'],
 '-DCMAKE_TOOLCHAIN_FILE='+str(root/('ports/native/chos-toolchain.cmake' if o.native_sdk else 'ports/webkit/chos-compile-preflight.cmake')),
 '-DCMAKE_BUILD_TYPE=Release','-DPORT=JSCOnly','-DENABLE_STATIC_JSC=ON',
 '-DENABLE_JIT=OFF','-DUSE_SYSTEM_MALLOC=ON','-DENABLE_TOOLS=OFF']
if o.native_sdk:
 subprocess.run([sys.executable,str(root/'tools/verify_native_sdk.py'),str(o.native_sdk.resolve())],check=True)
 command.append('-DCHOS_SDK_ROOT='+str(o.native_sdk.resolve()))
start=time.monotonic()
with (out/'configure.log').open('wb') as log:r=subprocess.run(command,cwd=root,stdout=log,stderr=subprocess.STDOUT)
log=(out/'configure.log').read_text(errors='replace')
cache=(out/'cmake/CMakeCache.txt').read_text(errors='replace') if (out/'cmake/CMakeCache.txt').exists() else ''
int128=re.search(r'^INT128_IS_USABLE:INTERNAL=(.*)$',cache,re.M)
versions={}
for name,program in {**programs,'clang':'clang','clang++':'clang++'}.items():
 try:
  v=subprocess.run([program,'--version'],capture_output=True,text=True,timeout=10)
  versions[name]=(v.stdout or v.stderr).splitlines()[0]
 except (OSError,IndexError,subprocess.TimeoutExpired):versions[name]='unavailable'

report={'status':'CONFIGURED_NOT_BUILT' if r.returncode==0 else 'CONFIGURATION_INCOMPLETE',
 'stage3_complete':False,'engine_built':False,'engine_executed':False,'upstream_commit':m['commit'],
 'source_manifest':'docs/dependencies/webkit-jsc-bringup.json','command':command,'returncode':r.returncode,
 'seconds':time.monotonic()-start,'log_sha256':sha((out/'configure.log').read_bytes()),
 'tool_versions':versions,'int128_type_compile_probe':('PASS' if int128[1]=='1' else 'FAIL') if int128 else 'NOT_RUN',
 'compile_only_cmake_checks':not bool(o.native_sdk),
 'native_sdk':str(o.native_sdk.resolve()) if o.native_sdk else None,
 'source_closure_files_checked':len(closure),
 'threads_missing':'Could NOT find Threads' in log,'target_library_search':'CHOS sysroot only; host executables permitted',
 'icu_missing':'Failed to find all ICU components' in log,
 'limits':(['Real executable links are used; failed native POSIX thread checks remain failures.'] if o.native_sdk else ['Static try-compile results do not prove executable linking or runtime support.'])+[
 'CMake FindThreads can falsely report -lpthreads in archive mode; that is NOT accepted evidence. Native partial pthread runtime has separate guest tests.',
 'The qualified C++20/23 container subset is not complete C++23/ABI/POSIX support. Read CMakeConfigureLog.yaml for specific compile failures; integer128 type compilation is not arithmetic-runtime qualification.',
 'No WebCore/WPE rendering,JavaScript execution,browser or conformance result.']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['status','engine_built','engine_executed','icu_missing','returncode']},indent=2))
print('Full log: '+str(out/'configure.log'));sys.exit(r.returncode)
