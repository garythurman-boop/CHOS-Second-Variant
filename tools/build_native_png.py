#!/usr/bin/env python3
"""Build real PNG and zlib memory-stream dependencies and original-corpus fixture."""
import argparse,pathlib,json,hashlib,subprocess,shutil,time
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--native-sdk',type=pathlib.Path)
p.add_argument('--host',action='store_true');p.add_argument('--sanitize',action='store_true')
p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
if o.host==bool(o.native_sdk):p.error('Select exactly one of --host or --native-sdk')
if o.sanitize and not o.host:p.error('--sanitize requires --host')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
subprocess.run(['python3',str(root/'tools/verify_native_png_sources.py')],check=True)
sdk=o.native_sdk.resolve() if o.native_sdk else None
if sdk:subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);prefix=out/'prefix';commands=[]
inputs=['ports/png/zlib-memory/CMakeLists.txt','ports/png/native-utc.patch','tests/native_png/main.c','tests/native_png/CMakeLists.txt','tools/generate_png_cases.py','tools/build_native_png.py','tools/verify_native_png_sources.py']
report={'status':'BUILDING','executed':False,'stage3_complete':False,'host_reference':o.host,'host_sanitizers':o.sanitize,'SDK_manifest_sha256':sha(sdk/'chos-sdk.json') if sdk else None,'source_manifest_sha256':sha(root/'docs/dependencies/native-png.json'),'port_and_fixture_sources':{n:sha(root/n) for n in inputs},'commands':commands,'limits':['PNG/zlib memory-stream prerequisite,not WebCore or full browser integration','Z_SOLO requires actual caller allocation callbacks;no gzip-file or compress/uncompress utility API','Selected RGBA8 sample normalization;ICC/gamma management and APNG playback not qualified','Selected corpus/bound/error tests do not constitute production hostile-content security or full upstream conformance']}
started=time.monotonic();cmake=shutil.which(o.cmake) or o.cmake;ninja=shutil.which(o.ninja) or o.ninja
base=[cmake,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_INSTALL_PREFIX='+str(prefix)]
if sdk:base+=['-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCHOS_APPLICATION_PROFILE=engine']
if o.sanitize:base+=['-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
def run(name,argv):
 commands.append(argv);(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
 with (out/(name+'.log')).open('wb') as f:
  try:code=subprocess.run(argv,cwd=root,stdout=f,stderr=subprocess.STDOUT).returncode
  except OSError as e:f.write((str(e)+'\n').encode());code=127
 if code:
  report.update(status='BUILD_FAILED',failed_step=name,returncode=code,seconds=time.monotonic()-started);(out/'build.json').write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(code)
run('zlib-setup',[*base,'-S',str(root/'ports/png/zlib-memory'),'-B',str(out/'zlib')])
run('zlib-build',[cmake,'--build',str(out/'zlib'),'-j4']);run('zlib-install',[cmake,'--install',str(out/'zlib')])
source=out/'png-source';shutil.copytree(root/'third_party/libpng',source)
with (out/'port-patch.log').open('wb') as f:subprocess.run(['patch','--batch','--fuzz=0','--no-backup-if-mismatch','-p1','-i',str(root/'ports/png/native-utc.patch')],cwd=source,stdout=f,stderr=subprocess.STDOUT,check=True)
# Upstream explicitly supports this original prebuilt config for cross builds.
# Its custom preprocessing commands otherwise omit CMAKE_C_COMPILER_TARGET.
run('png-setup',[*base,'-S',str(source),'-B',str(out/'png'),'-DPNG_SHARED=OFF','-DPNG_STATIC=ON','-DPNG_TESTS=OFF','-DPNG_TOOLS=OFF','-DPNG_LIBCONF_HEADER='+str(source/'scripts/pnglibconf.h.prebuilt'),'-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG -DZ_SOLO=1','-DZLIB_LIBRARY='+str(prefix/'lib/libz-memory.a'),'-DZLIB_INCLUDE_DIR='+str(prefix/'include'),'-DCMAKE_INSTALL_LIBDIR=lib'])
run('png-build',[cmake,'--build',str(out/'png'),'-j4']);run('png-install',[cmake,'--install',str(out/'png')])
run('cases',['python3',str(root/'tools/generate_png_cases.py'),'--out',str(out/'cases')])
archive=next(p for p in (prefix/'lib').glob('*png*.a') if not p.is_symlink())
run('application-setup',[*base,'-S',str(root/'tests/native_png'),'-B',str(out/'application'),'-DPNG_PREFIX='+str(prefix),'-DPNG_ARCHIVE='+str(archive),'-DPNG_CASES='+str(out/'cases')])
run('application-build',[cmake,'--build',str(out/'application'),'-j4'])
for name,want in report['port_and_fixture_sources'].items():assert sha(root/name)==want,'Source changed during build'
assert sha(root/'docs/dependencies/native-png.json')==report['source_manifest_sha256'],'Dependency manifest changed during build'
subprocess.run(['python3',str(root/'tools/verify_native_png_sources.py')],check=True)
if sdk:
 assert sha(sdk/'chos-sdk.json')==report['SDK_manifest_sha256'],'SDK manifest changed during build'
 subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
archives={}
for p in [archive,prefix/'lib/libz-memory.a']:
 members=subprocess.check_output(['llvm-ar','t',str(p)],text=True).splitlines()
 if sdk:assert subprocess.check_output(['llvm-readelf','-h',str(p)],text=True).count('Machine:                           AArch64')==len(members)
 archives[p.name]={'members':len(members),'bytes':p.stat().st_size,'sha256':sha(p)}
elf=out/'application/native_png'
if sdk:assert not subprocess.check_output(['llvm-nm','-u',str(elf)],text=True).strip()
report.update(status='LINKED_NOT_EXECUTED',returncode=0,seconds=time.monotonic()-started,archives=archives,application={'path':str(elf),'bytes':elf.stat().st_size,'sha256':sha(elf)},cases_manifest_sha256=sha(out/'cases/cases.json'),installed_files_sha256={str(p.relative_to(prefix)):sha(p) for p in sorted(prefix.rglob('*')) if p.is_file()})
(out/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'application':report['application'],'archives':archives},indent=2))
