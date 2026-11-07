#!/usr/bin/env python3
"""Link a genuine JSC pixel client from the verified published a89db07 handoff.

This deliberately uses the existing qualified engine/SDK bytes. It does not
recompile or substitute engine code, execute target code, or claim WebCore.
"""
import argparse,hashlib,json,pathlib,shutil,subprocess
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--kit',type=pathlib.Path,required=True)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--cxx',default='clang++')
o=p.parse_args();kit=o.kit.resolve();out=o.out.resolve()
# Anchor every input to the published verified handoff, not a mutable local build.
manifest=kit/'MANIFEST.json'
assert hashlib.sha256(manifest.read_bytes()).hexdigest()=='c5f2aabcf448415a9567cb7f0d2d1ad11ace0c2ef44731f1c8ff0a810b1b4757','Unrecognized handoff manifest'
m=json.loads(manifest.read_text());assert m['source_commit']=='a89db07f60345bae56676eacfbefc7cf08f5973e'
for name,want in m['files'].items():
 path=kit/name
 assert not pathlib.Path(name).is_absolute() and '..' not in pathlib.Path(name).parts
 assert path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(kit),name
 assert path.stat().st_size==want['bytes'] and hashlib.sha256(path.read_bytes()).hexdigest()==want['sha256'],name
subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(kit/'sdk')],check=True)
report=json.loads((kit/'fixture/engine-link-report.json').read_text())
assert len(report['original_subtarget_objects'])==53 and len(report['engine_archive_members'])==482
# The published regular archives have different container bytes from the old
# thin archives. Verify the original compiled object bytes inside each archive.
for archive in ['libJavaScriptCore.a','libWTF.a','libbmalloc.a']:
 path=kit/'engine/lib'/archive
 names=subprocess.check_output(['llvm-ar','t',str(path)],text=True).splitlines()
 entries=[e for e in report['engine_archive_members'] if e['archive']==archive]
 assert len(names)==len(entries) and len(set(names))==len(names)
 assert set(names)=={pathlib.Path(e['path']).name for e in entries}
 for entry in entries:
  data=subprocess.check_output(['llvm-ar','p',str(path),pathlib.Path(entry['path']).name])
  assert len(data)==entry['bytes'] and hashlib.sha256(data).hexdigest()==entry['sha256']
for entry in report['original_subtarget_objects']:
 data=(kit/'engine'/entry['path']).read_bytes()
 assert len(data)==entry['bytes'] and hashlib.sha256(data).hexdigest()==entry['sha256']
out.mkdir(parents=True,exist_ok=False);headers=out/'include/JavaScriptCore';headers.mkdir(parents=True)
for header in (kit/'source/webkit/Source/JavaScriptCore/API').glob('*.h'):shutil.copyfile(header,headers/header.name)
# Pin the headers for this new ABI overlay, leaving the recovered SDK immutable.
( out/'include/chos').mkdir()
for name in ['gui_protocol.h','gui_surface.h']:shutil.copyfile(root/'include/chos'/name,out/'include/chos'/name)
base=['--target=aarch64-none-elf','--sysroot='+str(kit/'sdk'),'-D__CHOS__=1','-fhosted','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie','-femulated-tls','-ffunction-sections','-fdata-sections','-mcpu=cortex-a72','-mstrict-align','-fno-exceptions','-fno-rtti','-nostdinc++','-isystem',str(kit/'sdk/include/c++/v1'),'-isystem',str(kit/'sdk/include'),'-O3','-DNDEBUG']
source=root/'tests/jsc_pixel_surface/main.cpp';obj=out/'main.cpp.obj';elf=out/'jsc_pixel_surface.elf'
compile_command=[o.cxx,*base,'-std=c++23','-I'+str(out/'include'),'-c',str(source),'-o',str(obj)]
# Reuse the published, exact ordered static link closure; do not execute the
# handoff's helper script or shell-interpolate its command strings.
link=json.loads((kit/'relink-argv.json').read_text())
link=[s.replace('@ROOT@',str(kit)) for s in link];link[0]=o.cxx
for i,s in enumerate(link):
 if s==str(kit/'fixture/main.cpp.obj'):link[i]=str(obj)
 elif s==str(kit/'relinked.elf'):link[i]=str(elf)
 elif s.startswith('-Wl,-Map,'):link[i]='-Wl,-Map,'+str(out/'jsc_pixel_surface.map')
 elif s.startswith('--dependency-file='):link[i]='--dependency-file='+str(out/'link.d')
record={'status':'BUILDING','executed':False,'stage3_complete':False,'kit_manifest_sha256':hashlib.sha256(manifest.read_bytes()).hexdigest(),'original_engine_objects':535,'sdk_manifest_sha256':hashlib.sha256((kit/'sdk/chos-sdk.json').read_bytes()).hexdigest(),'compiler_version':subprocess.check_output([o.cxx,'--version'],text=True).splitlines()[0],'GUI_overlay_sha256':{name:hashlib.sha256((out/'include/chos'/name).read_bytes()).hexdigest() for name in ['gui_protocol.h','gui_surface.h']},'application_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'commands':[compile_command,link],'limits':['Typed-array pixel transport prerequisite only;not WebCore,HTML/CSS,Canvas or modern browser','Original engine archives and SDK reused byte-for-byte;new client is compiled separately']}
(out/'build.json').write_text(json.dumps(record,indent=2)+'\n')
subprocess.run(compile_command,check=True);subprocess.run(link,check=True)
assert not subprocess.check_output(['nm','-u',str(elf)],text=True).strip(),'Unresolved target symbols'
record.update(status='LINKED_NOT_EXECUTED',application_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),application_bytes=elf.stat().st_size)
(out/'build.json').write_text(json.dumps(record,indent=2)+'\n');print(elf)
