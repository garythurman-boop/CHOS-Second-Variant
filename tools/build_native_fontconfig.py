#!/usr/bin/env python3
"""Build original Fontconfig explicit catalogues with real FreeType/XML/threads."""
import argparse,hashlib,json,pathlib,shutil,subprocess,time
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--native-sdk',type=pathlib.Path)
p.add_argument('--host',action='store_true');p.add_argument('--sanitize',action='store_true')
p.add_argument('--freetype-prefix',type=pathlib.Path,required=True);p.add_argument('--xml-prefix',type=pathlib.Path,required=True)
p.add_argument('--gperf',type=pathlib.Path,required=True);p.add_argument('--host-cc',default='clang')
o=p.parse_args()
if o.host==bool(o.native_sdk):p.error('Select exactly one of --host or --native-sdk')
if o.sanitize and not o.host:p.error('--sanitize requires --host')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
verify=['python3',str(root/'tools/verify_native_fontconfig_sources.py')];subprocess.run(verify,check=True)
for tool in ['tools/verify_native_text_sources.py','tools/verify_native_xml_sources.py']:
 subprocess.run(['python3',str(root/tool)],check=True)
sdk=o.native_sdk.resolve() if o.native_sdk else None
if sdk:subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
ft=o.freetype_prefix.resolve();xml=o.xml_prefix.resolve();gperf=o.gperf.resolve()
for f in [ft/'include/freetype2/freetype/freetype.h',ft/'lib/libfreetype.a',xml/'include/libxml2/libxml/parser.h',xml/'lib/libxml2.a',gperf]:assert f.is_file(),f
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);source=out/'source';prefix=out/'prefix'
shutil.copytree(root/'third_party/fontconfig',source)
inputs=['tools/build_native_fontconfig.py','tools/verify_native_fontconfig_sources.py','tools/verify_native_text_sources.py','tools/verify_native_xml_sources.py','ports/fontconfig/memory-catalogue.patch','ports/fontconfig/generate_headers.py','ports/fontconfig/config.h','ports/fontconfig/CMakeLists.txt','ports/fontconfig/fontconfig.pc.in','ports/fontconfig/FontconfigConfig.cmake.in','tests/native_fontconfig/main.c','tests/native_fontconfig/CMakeLists.txt']
profile=['Actual original Fontconfig matching, app-font files, patterns/charset/language/XML rules and real pthreads','Application explicitly installs memory catalogue before global FcInit succeeds','Default config loading, directory scans, serialized disk caches/watcher remain unsupported; not full filesystem Fontconfig','Regular fixture fonts only; actual bold/italic face catalogue selection remains unqualified','WebCore, browser, physical hardware and Stage 3 acceptance incomplete']
depfiles={str(f):sha(f) for f in sorted(set([ft/'lib/libfreetype.a',xml/'lib/libxml2.a',gperf,*[f for f in (ft/'include/freetype2').rglob('*') if f.is_file()],*[f for f in (xml/'include/libxml2').rglob('*') if f.is_file()]]))}
report={'status':'BUILDING','executed':False,'host_reference':o.host,'host_sanitizers':o.sanitize,'stage3_complete':False,'source_manifest_sha256':sha(root/'docs/dependencies/native-fontconfig.json'),'SDK_manifest_sha256':sha(sdk/'chos-sdk.json') if sdk else None,'sources_sha256':{n:sha(root/n) for n in inputs},'dependency_files_sha256':depfiles,'commands':[],'limits':profile}
start=time.monotonic()
def save():(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
def run(name,args,cwd=root):
 report['commands'].append(args);save()
 with (out/(name+'.log')).open('wb') as f:code=subprocess.run(args,cwd=cwd,stdout=f,stderr=subprocess.STDOUT).returncode
 if code:report.update(status='BUILD_FAILED',failed_step=name,returncode=code);save();raise SystemExit(code)
run('patch',['patch','--batch','--fuzz=0','--no-backup-if-mismatch','-p1','-i',str(root/'ports/fontconfig/memory-catalogue.patch')],source)
shutil.copyfile(root/'ports/fontconfig/config.h',source/'config.h')
run('generate',['python3',str(root/'ports/fontconfig/generate_headers.py'),'--source',str(source),'--original-source',str(root/'third_party/fontconfig'),'--gperf',str(gperf),'--host-cc',o.host_cc])
cmake=shutil.which('cmake');ninja=shutil.which('ninja');assert cmake and ninja
base=[cmake,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+ninja,'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
if sdk:base+=['-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCHOS_APPLICATION_PROFILE=engine']
if o.sanitize:base+=['-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
run('setup',[*base,'-S',str(root/'ports/fontconfig'),'-B',str(out/'fontconfig'),'-DFONTCONFIG_SOURCE='+str(source),'-DFT_PREFIX='+str(ft),'-DXML_PREFIX='+str(xml)])
run('build',[cmake,'--build',str(out/'fontconfig'),'-j4']);run('install',[cmake,'--install',str(out/'fontconfig')])
run('application-setup',[*base,'-S',str(root/'tests/native_fontconfig'),'-B',str(out/'application'),'-DFONTCONFIG_PREFIX='+str(prefix)])
run('application-build',[cmake,'--build',str(out/'application'),'-j4'])
subprocess.run(verify,check=True)
for n,wanted in report['sources_sha256'].items():assert sha(root/n)==wanted,'Changedsource:'+n
for n,wanted in depfiles.items():assert sha(pathlib.Path(n))==wanted,'Changeddependency:'+n
assert sha(root/'docs/dependencies/native-fontconfig.json')==report['source_manifest_sha256']
if sdk:assert sha(sdk/'chos-sdk.json')==report['SDK_manifest_sha256'];subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
archive=prefix/'lib/libfontconfig.a';app=out/'application/native_fontconfig';members=subprocess.check_output(['llvm-ar','t',str(archive)],text=True).splitlines()
if sdk:
 assert subprocess.check_output(['llvm-readelf','-h',str(archive)],text=True).count('Machine:                           AArch64')==len(members)
 assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
report.update(status='LINKED_NOT_EXECUTED',returncode=0,seconds=time.monotonic()-start,archive={'sha256':sha(archive),'bytes':archive.stat().st_size,'members':len(members)},application={'path':str(app),'sha256':sha(app),'bytes':app.stat().st_size},installed_files_sha256={str(f.relative_to(prefix)):sha(f) for f in sorted(prefix.rglob('*')) if f.is_file()});save();print(json.dumps({k:report[k] for k in ['status','archive','application']},indent=2))
