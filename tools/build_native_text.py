#!/usr/bin/env python3
"""Build genuine FreeType/HarfBuzz and the native owned-image text backend.

Host reference and CHOS target builds are separate. No host library can satisfy
native dependency lookup. Build/link success is never reported as execution.
"""
import argparse,pathlib,json,hashlib,subprocess,shutil,time
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--native-sdk',type=pathlib.Path)
p.add_argument('--engine-kit',type=pathlib.Path,help='Verified a89db07 original engine handoff for actual JavaScript-generated text')
p.add_argument('--host',action='store_true');p.add_argument('--sanitize',action='store_true')
p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
assert o.host!=bool(o.native_sdk),'Choose host reference OR an explicit native SDK'
assert not o.engine_kit or not o.host,'Original engine objects cannot be linked into a host reference'
assert not o.sanitize or o.host,'Sanitizers are host reference checks,not native execution'
out=o.out.resolve();sdk=o.native_sdk.resolve() if o.native_sdk else None
m=json.loads((root/'docs/dependencies/native-fonts.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for project in m['projects']:
 base=root/project['root']
 actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
 assert actual==set(project['files']),'Font source inventory changed: '+project['name']
 for name,wanted in project['files'].items():assert sha(base/name)==wanted,name
if sdk:
 subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
 symbols=subprocess.check_output(['llvm-nm','--defined-only',str(sdk/'lib/libchos-c.a')],text=True)
 assert all(' T '+s+'\n' in symbols for s in ['setjmp','longjmp']),'Native SDK lacks real nonlocal jumps'
 assert (sdk/'include/chos/gui_surface.h').is_file()
out.mkdir(parents=True,exist_ok=False)
cmake=str(pathlib.Path(shutil.which(o.cmake) or o.cmake).resolve());ninja=str(pathlib.Path(shutil.which(o.ninja) or o.ninja).resolve())
prefix=out/'prefix';ft=out/'freetype';hb=out/'harfbuzz';app=out/'application';generated=out/'text-cases.h'
engineArgs=[]
if o.engine_kit:
 kit=o.engine_kit.resolve();manifest=kit/'MANIFEST.json'
 assert sha(manifest)=='c5f2aabcf448415a9567cb7f0d2d1ad11ace0c2ef44731f1c8ff0a810b1b4757'
 inventory=json.loads(manifest.read_text())
 for name,want in inventory['files'].items():
  source=kit/name;assert source.is_file() and not source.is_symlink() and source.resolve().is_relative_to(kit)
  assert source.stat().st_size==want['bytes'] and sha(source)==want['sha256'],name
 old=json.loads((kit/'fixture/engine-link-report.json').read_text());assert len(old['original_subtarget_objects'])==53 and len(old['engine_archive_members'])==482
 for archive in ['libJavaScriptCore.a','libWTF.a','libbmalloc.a']:
  entries=[e for e in old['engine_archive_members'] if e['archive']==archive]
  names=subprocess.check_output(['llvm-ar','t',str(kit/'engine/lib'/archive)],text=True).splitlines()
  assert len(names)==len(entries) and set(names)=={pathlib.Path(e['path']).name for e in entries}
  for e in entries:
   data=subprocess.check_output(['llvm-ar','p',str(kit/'engine/lib'/archive),pathlib.Path(e['path']).name])
   assert len(data)==e['bytes'] and hashlib.sha256(data).hexdigest()==e['sha256']
 headers=out/'engine-include/JavaScriptCore';headers.mkdir(parents=True)
 for header in (kit/'source/webkit/Source/JavaScriptCore/API').glob('*.h'):shutil.copyfile(header,headers/header.name)
 objects=out/'engine-objects.cmake'
 objects.write_text('set(TEXT_ENGINE_OBJECT_LIST\n'+''.join('  "'+str(kit/'engine'/e['path'])+'"\n' for e in old['original_subtarget_objects'])+')\n')
 engineArgs=['-DTEXT_ENGINE_KIT='+str(kit),'-DTEXT_ENGINE_OBJECTS='+str(objects),'-DTEXT_ENGINE_HEADERS='+str(out/'engine-include')]
commands=[];report={'status':'BUILDING','executed':False,'stage3_complete':False,'host_reference':o.host,'host_sanitizers':o.sanitize,'actual_JSC_linked':bool(o.engine_kit),'original_engine_manifest_sha256':sha(o.engine_kit.resolve()/'MANIFEST.json') if o.engine_kit else None,'SDK_manifest_sha256':sha(sdk/'chos-sdk.json') if sdk else None,'source_manifest_sha256':sha(root/'docs/dependencies/native-fonts.json'),'font_fixtures_manifest_sha256':sha(root/'docs/dependencies/text-fixtures.json'),'native_override_sha256':sha(root/'ports/text/harfbuzz_native.h'),'backend_source_sha256':{name:sha(root/name) for name in ['userland/text/text.cpp','userland/text/include/chos/text.h','tests/native_text/main.cpp','tests/native_text/javascript.cpp','tests/native_text/CMakeLists.txt']},'commands':commands,'limits':['Native font dependency/backend and selected shaping fixtures,not WebCore or the modern browser','Optional external font PNG/Brotli/bzip2/HVF and HarfBuzz GPU/raster/vector/subset helpers remain unqualified','Native uses original ICU79 Unicode funcs;host reference uses original HarfBuzz builtin Unicode funcs,with the same selected fonts/cases','Native mapped-file optimization disabled explicitly;original stdio/snapshot loader remains functional','Real native pthread mutex subset retained;CMake full pthread-cancel probe is not a complete POSIX claim']}
started=time.monotonic()
def run(name,command):
 commands.append(command);(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
 with (out/(name+'.log')).open('wb') as f:r=subprocess.run(command,cwd=root,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:
  report.update(status='BUILD_FAILED',failed_step=name,returncode=r.returncode,seconds=time.monotonic()-started)
  (out/'build.json').write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(r.returncode)
base=[cmake,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+ninja,'-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF']
if sdk:base+=['-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCHOS_APPLICATION_PROFILE=engine']
if o.sanitize:base+=['-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
run('freetype-configure',[*base,'-S',str(root/'third_party/freetype'),'-B',str(ft),'-DCMAKE_INSTALL_PREFIX='+str(prefix),*['-DFT_DISABLE_'+s+'=ON' for s in ['ZLIB','BZIP2','PNG','HARFBUZZ','BROTLI','HVF']]])
run('freetype-build',[cmake,'--build',str(ft),'-j4']);run('freetype-install',[cmake,'--install',str(ft)])
hb_args=[*base,'-S',str(root/'third_party/harfbuzz'),'-B',str(hb),'-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DCMAKE_CXX_STANDARD=23','-DHB_HAVE_FREETYPE=ON','-DHB_HAVE_ICU='+('ON' if sdk else 'OFF'),*['-DHB_HAVE_'+s+'=OFF' for s in ['GLIB','GOBJECT','CAIRO','GRAPHITE2']],*['-DHB_BUILD_'+s+'=OFF' for s in ['UTILS','SUBSET','RASTER','VECTOR','GPU','GPU_DEMO']],'-DFREETYPE_INCLUDE_DIR_ft2build='+str(prefix/'include/freetype2'),'-DFREETYPE_INCLUDE_DIR_freetype2='+str(prefix/'include/freetype2'),'-DFREETYPE_LIBRARY_RELEASE='+str(prefix/'lib/libfreetype.a')]
if sdk:hb_args+=['-DICU_INCLUDE_DIR='+str(sdk/'include'),'-DICU_UC_LIBRARY_RELEASE='+str(sdk/'lib/libicuuc.a'),'-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -include '+str(root/'ports/text/harfbuzz_native.h')]
run('harfbuzz-configure',hb_args);run('harfbuzz-build',[cmake,'--build',str(hb),'-j4']);run('harfbuzz-install',[cmake,'--install',str(hb)])
run('vectors',['python3',str(root/'tools/generate_text_cases.py'),'--out',str(generated)])
run('application-configure',[*base,'-S',str(root/'tests/native_text'),'-B',str(app),'-DTEXT_PREFIX='+str(prefix),'-DHB_BUILD='+str(hb),'-DTEXT_CASES_HEADER='+str(generated),'-DCMAKE_INSTALL_PREFIX='+str(prefix),*engineArgs])
run('application-build',[cmake,'--build',str(app),'-j4']);run('backend-install',[cmake,'--install',str(app)])
for name,wanted in report['backend_source_sha256'].items():assert sha(root/name)==wanted,'Source changed during build: '+name
licenses=prefix/'share/licenses/chos-text';licenses.mkdir(parents=True)
for source,name in [('third_party/freetype/docs/FTL.TXT','FreeType-FTL.txt'),('third_party/harfbuzz/COPYING','HarfBuzz-COPYING')]:shutil.copyfile(root/source,licenses/name)
elf=app/'native_text';undefined=subprocess.check_output(['llvm-nm','-u',str(elf)],text=True)
if sdk:assert not undefined.strip(),'Unresolved native symbols'
libs=[prefix/'lib/libfreetype.a',hb/'libharfbuzz.a']+([hb/'libharfbuzz-icu.a'] if sdk else [])
archives={}
for lib in libs:
 members=subprocess.check_output(['llvm-ar','t',str(lib)],text=True).splitlines()
 headers=subprocess.check_output(['llvm-readelf','-h',str(lib)],text=True)
 if sdk:assert headers.count('Machine:                           AArch64')==len(members)
 archives[str(lib.relative_to(out))]={'sha256':sha(lib),'members':len(members),'bytes':lib.stat().st_size}
report.update(status='LINKED_NOT_EXECUTED',returncode=0,seconds=time.monotonic()-started,archives=archives,application={'path':str(elf),'sha256':sha(elf),'bytes':elf.stat().st_size},backend_archive_sha256=sha(app/'libchos-text.a'),installed_files_sha256={str(p.relative_to(prefix)):sha(p) for p in sorted(prefix.rglob('*')) if p.is_file()})
(out/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'application':report['application']},indent=2))
