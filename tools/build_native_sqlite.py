#!/usr/bin/env python3
"""Build genuine native SQLite with real pthreads and an explicit memory-only VFS."""
import argparse,pathlib,subprocess,hashlib,json,shutil,time
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--native-sdk',type=pathlib.Path);p.add_argument('--host',action='store_true');p.add_argument('--sanitize',action='store_true');o=p.parse_args()
if o.host==bool(o.native_sdk):p.error('Choose --host or --native-sdk')
if o.sanitize and not o.host:p.error('--sanitize requires --host')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
subprocess.run(['python3',str(root/'tools/verify_native_sqlite_sources.py')],check=True)
sdk=o.native_sdk.resolve() if o.native_sdk else None
if sdk:subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);prefix=out/'prefix';source=out/'source';shutil.copytree(root/'third_party/sqlite',source)
inputs=['tools/build_native_sqlite.py','tools/verify_native_sqlite_sources.py','ports/sqlite/native-mutex.patch','ports/sqlite/native_os.c','ports/sqlite/CMakeLists.txt','tests/native_sqlite/main.c','tests/native_sqlite/CMakeLists.txt']
report={'status':'BUILDING','host_reference':o.host,'host_sanitizers':o.sanitize,'stage3_complete':False,'executed':False,'source_manifest_sha256':sha(root/'docs/dependencies/native-sqlite.json'),'SDK_manifest_sha256':sha(sdk/'chos-sdk.json') if sdk else None,'sources_sha256':{n:sha(root/n) for n in inputs},'commands':[],'limits':['Genuine in-memory SQLite core prerequisite,not WebCore/browser integration','OS_OTHER uses real pthread mutexes,real entropy and guest wall clock;trustworthy production time remains separate','Persistent disk VFS,WAL filesystem/browser persistence and upstream full conformance not qualified']}
started=time.monotonic()
def save():(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
def run(n,args,cwd=root):
 report['commands'].append(args);save()
 with (out/(n+'.log')).open('wb') as f:rc=subprocess.run(args,cwd=cwd,stdout=f,stderr=subprocess.STDOUT).returncode
 if rc:report.update(status='BUILD_FAILED',failed_step=n,returncode=rc);save();raise SystemExit(rc)
run('patch',['patch','--batch','--fuzz=0','--no-backup-if-mismatch','-p1','-i',str(root/'ports/sqlite/native-mutex.patch')],source)
cmake=shutil.which('cmake');ninja=shutil.which('ninja')
base=[cmake,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+ninja,'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_INSTALL_PREFIX='+str(prefix)]
if sdk:base+=['-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCHOS_APPLICATION_PROFILE=engine']
if o.sanitize:base+=['-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
run('setup',[*base,'-S',str(root/'ports/sqlite'),'-B',str(out/'sqlite'),'-DSQLITE_SOURCE='+str(source)])
run('build',[cmake,'--build',str(out/'sqlite'),'-j2']);run('install',[cmake,'--install',str(out/'sqlite')])
run('application-setup',[*base,'-S',str(root/'tests/native_sqlite'),'-B',str(out/'application'),'-DSQLITE_PREFIX='+str(prefix)])
run('application-build',[cmake,'--build',str(out/'application'),'-j2'])
for n,h in report['sources_sha256'].items():assert sha(root/n)==h,'Source changed during build: '+n
assert sha(root/'docs/dependencies/native-sqlite.json')==report['source_manifest_sha256'],'Source manifest changed during build'
subprocess.run(['python3',str(root/'tools/verify_native_sqlite_sources.py')],check=True)
if sdk:subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
archive=prefix/'lib/libsqlite3.a';elf=out/'application/native_sqlite'
if sdk:
 assert not subprocess.check_output(['llvm-nm','-u',str(elf)],text=True).strip()
 assert 'AArch64' in subprocess.check_output(['llvm-readelf','-h',str(elf)],text=True)
 members=subprocess.check_output(['llvm-ar','t',str(archive)],text=True).splitlines()
 assert subprocess.check_output(['llvm-readelf','-h',str(archive)],text=True).count('Machine:                           AArch64')==len(members)
report.update(status='LINKED_NOT_EXECUTED',seconds=time.monotonic()-started,archive={'bytes':archive.stat().st_size,'sha256':sha(archive)},application={'path':str(elf),'bytes':elf.stat().st_size,'sha256':sha(elf)},installed_files_sha256={str(f.relative_to(prefix)):sha(f) for f in sorted(prefix.rglob('*')) if f.is_file()});save();print(json.dumps({'status':report['status'],'application':report['application']},indent=2))
