#!/usr/bin/env python3
"""Build original libxml2 with explicit native memory/custom-I/O transport policy."""
import argparse,pathlib,json,hashlib,subprocess,shutil,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--native-sdk',type=pathlib.Path)
p.add_argument('--host',action='store_true');p.add_argument('--sanitize',action='store_true')
p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
if o.host==bool(o.native_sdk):p.error('Select exactly one of --host or --native-sdk')
if o.sanitize and not o.host:p.error('--sanitize requires --host')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
verify=['python3',str(ROOT/'tools/verify_native_xml_sources.py')];subprocess.run(verify,check=True)
sdk=o.native_sdk.resolve() if o.native_sdk else None
if sdk:subprocess.run(['python3',str(ROOT/'tools/verify_native_sdk.py'),str(sdk)],check=True)
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);prefix=out/'prefix';source=out/'source'
shutil.copytree(ROOT/'third_party/libxml2',source)
inputs=['ports/xml/native-memory-io.patch','tools/build_native_xml.py','tools/verify_native_xml_sources.py','tests/native_xml/main.c','tests/native_xml/CMakeLists.txt']
report={'status':'BUILDING','executed':False,'stage3_complete':False,'host_reference':o.host,'host_sanitizers':o.sanitize,'SDK_manifest_sha256':sha(sdk/'chos-sdk.json') if sdk else None,'source_manifest_sha256':sha(ROOT/'docs/dependencies/native-xml.json'),'port_and_fixture_sources':{n:sha(ROOT/n) for n in inputs},'commands':[],'limits':['SelectedXMLmemory/customIOparserprerequisite;notWebCoreintegration orfullXMLconformance','Native defaultPOSIXfile/fdtransport unsupported;FILE/customcallbacks remainreal','Catalog/XInclude/modules/iconv/ICU/zlibexcluded;per-context externalresourceloaderpolicy required','No productionhostileXMLsecurity/physicalFusion/Pi5/laptopclaim']}
start=time.monotonic()
def save():(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
def run(name,args,cwd=ROOT):
 report['commands'].append(args);save()
 with (out/(name+'.log')).open('wb') as f:
  code=subprocess.run(args,cwd=cwd,stdout=f,stderr=subprocess.STDOUT).returncode
 if code:report.update(status='BUILD_FAILED',failed_step=name,returncode=code);save();raise SystemExit(code)
run('port-patch',['patch','--batch','--fuzz=0','--no-backup-if-mismatch','-p1','-i',str(ROOT/'ports/xml/native-memory-io.patch')],source)
cmake=shutil.which(o.cmake) or o.cmake;ninja=shutil.which(o.ninja) or o.ninja
base=[cmake,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DCMAKE_INSTALL_LIBDIR=lib']
if sdk:base+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCHOS_APPLICATION_PROFILE=engine']
if o.sanitize:base+=['-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
options={'BUILD_SHARED_LIBS':'OFF','LIBXML2_WITH_PROGRAMS':'OFF','LIBXML2_WITH_TESTS':'OFF','LIBXML2_WITH_PYTHON':'OFF','LIBXML2_WITH_MODULES':'OFF','LIBXML2_WITH_ICONV':'OFF','LIBXML2_WITH_ICU':'OFF','LIBXML2_WITH_ZLIB':'OFF','LIBXML2_WITH_CATALOG':'OFF','LIBXML2_WITH_XINCLUDE':'OFF','LIBXML2_WITH_TLS':'ON'}
run('xml-setup',[*base,'-S',str(source),'-B',str(out/'xml'),*['-D'+k+'='+v for k,v in options.items()]])
run('xml-build',[cmake,'--build',str(out/'xml'),'-j4']);run('xml-install',[cmake,'--install',str(out/'xml')])
run('application-setup',[*base,'-S',str(ROOT/'tests/native_xml'),'-B',str(out/'application'),'-DXML_PREFIX='+str(prefix)])
run('application-build',[cmake,'--build',str(out/'application'),'-j4'])
subprocess.run(verify,check=True)
for name,wanted in report['port_and_fixture_sources'].items():assert sha(ROOT/name)==wanted,'Sourcechangedduringbuild:'+name
assert sha(ROOT/'docs/dependencies/native-xml.json')==report['source_manifest_sha256']
if sdk:assert sha(sdk/'chos-sdk.json')==report['SDK_manifest_sha256'];subprocess.run(['python3',str(ROOT/'tools/verify_native_sdk.py'),str(sdk)],check=True)
archive=prefix/'lib/libxml2.a';members=subprocess.check_output(['llvm-ar','t',str(archive)],text=True).splitlines();elf=out/'application/native_xml'
if sdk:
 assert subprocess.check_output(['llvm-readelf','-h',str(archive)],text=True).count('Machine:                           AArch64')==len(members)
 assert not subprocess.check_output(['llvm-nm','-u',str(elf)],text=True).strip()
report.update(status='LINKED_NOT_EXECUTED',returncode=0,seconds=time.monotonic()-start,archive={'bytes':archive.stat().st_size,'members':len(members),'sha256':sha(archive)},application={'path':str(elf),'bytes':elf.stat().st_size,'sha256':sha(elf)},installed_files_sha256={str(p.relative_to(prefix)):sha(p) for p in sorted(prefix.rglob('*')) if p.is_file()},options=options)
save();print(json.dumps({k:report[k] for k in ['status','application','archive']},indent=2))
