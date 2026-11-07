#!/usr/bin/env python3
"""Package the complete original engine closure plus the qualified pixel client.

Preserves the a89db07 SDK/535 engine objects and source/notices. Requires the
companion exact CHOS source ZIP, and retains all inputs/evidence on failure.
"""
import argparse,hashlib,json,pathlib,shutil,subprocess,zipfile
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
for arg in ['kit','application-build','images','source-zip','out']:p.add_argument('--'+arg,type=pathlib.Path,required=True)
p.add_argument('--source-ref',required=True)
o=p.parse_args();kit=o.kit.resolve();client=o.application_build.resolve();images=o.images.resolve();out=o.out.resolve()
source=subprocess.check_output(['git','rev-parse','--verify','--end-of-options',o.source_ref+'^{commit}'],cwd=root,text=True).strip();short=source[:7]
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
 return h.hexdigest()
assert len(source)==40 and all(c in '0123456789abcdef' for c in source)
assert sha(kit/'MANIFEST.json')=='c5f2aabcf448415a9567cb7f0d2d1ad11ace0c2ef44731f1c8ff0a810b1b4757'
original=json.loads((kit/'MANIFEST.json').read_text());stage=json.loads((images/'native-engine.json').read_text())
assert sha(client/'jsc_pixel_surface.elf')==stage['source_sha256'] and sha(images/'native-engine.elf')==stage['staged_sha256']
assert o.source_zip.is_file()
client_record=json.loads((client/'build.json').read_text())
assert sha(root/'tests/jsc_pixel_surface/main.cpp')==client_record['application_source_sha256']
for name,wanted in client_record.get('GUI_overlay_sha256',{}).items():assert sha(root/'include/chos'/name)==wanted
for name in ['chos.elf','uefi-kernel.elf']:
 data=(images/name).read_bytes()
 assert ('[BUILD] '+source[:12]+'\n\0').encode() in data and (source[:12]+'-dirty').encode() not in data,'Diagnostic kernel build ID mismatch'
 assert (images/'native-engine.elf').read_bytes() in data,'Diagnostic ELF not embedded'
assert (images/'uefi-kernel.elf').read_bytes() in (images/'chos.iso').read_bytes(),'Diagnostic ISO kernel mismatch'
# Require the source archive the normal publisher produces, not a file merely
# named for this source. git archive is deterministic for a given commit.
out.mkdir(parents=True,exist_ok=False);canonical=out/('CHOS-'+short+'-source.zip')
subprocess.run(['git','archive','--worktree-attributes','--format=zip','--prefix=CHOS-Second-Variant/','--output='+str(canonical),source],cwd=root,check=True)
assert sha(canonical)==sha(o.source_zip),'Wrong companion CHOS source archive'
payload=out/('CHOS-Fusion-'+short+'-Handoff');payload.mkdir()
for name,want in original['files'].items():
 old=kit/name
 assert not pathlib.Path(name).is_absolute() and '..' not in pathlib.Path(name).parts
 assert old.is_file() and not old.is_symlink() and old.resolve().is_relative_to(kit)
 assert old.stat().st_size==want['bytes'] and sha(old)==want['sha256'],name
 target=payload/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(old,target)
oldiso=payload/'CHOS-Native-Engine-a89db07.iso';assert oldiso.is_file();oldiso.unlink()
iso=payload/('CHOS-Native-Pixels-'+short+'.iso');shutil.copyfile(images/'chos.iso',iso)
shutil.copyfile(client/'main.cpp.obj',payload/'fixture/main.cpp.obj')
shutil.copyfile(root/'tests/jsc_pixel_surface/main.cpp',payload/'fixture/main.cpp')
shutil.copyfile(client/'jsc_pixel_surface.map',payload/'fixture/jsc_native_suite.map')
shutil.copytree(client/'include',payload/'fixture/include')
commands=json.loads((client/'build.json').read_text())['commands']
compile_argv=[v.replace(str(client), '@ROOT@/fixture').replace(str(kit),'@ROOT@').replace(str(root/'tests/jsc_pixel_surface'),'@ROOT@/fixture') for v in commands[0]]
(payload/'fixture/compile-argv.json').write_text(json.dumps(compile_argv,indent=2)+'\n')
# The ordered link closure has the same input paths as the a89db07 handoff.
# Its main object was replaced above; all original engine/SDK bytes remain.
shutil.copyfile(kit/'relink-argv.json',payload/'relink-argv.json')
(payload/'compile.py').write_text('''#!/usr/bin/env python3
import pathlib,json,subprocess,sys
root=pathlib.Path(__file__).resolve().parent
command=json.loads((root/'fixture/compile-argv.json').read_text())
command=[v.replace('@ROOT@',str(root)) for v in command]
if len(sys.argv)>1:command[0]=sys.argv[1]
subprocess.run(command,cwd=root,check=True)
''')
shutil.copyfile(client/'build.json',payload/'fixture/pixel-build.json')
shutil.copyfile(root/'docs/NATIVE_PIXEL_SURFACES.md',payload/'ENGINE-DETAILS.md')
shutil.copyfile(root/'docs/NATIVE_PIXEL_FUSION_TESTING.md',payload/'TESTING.md')
distribution=payload/'DISTRIBUTION.md'
distribution.write_text(distribution.read_text().replace('NATIVE_JSC_STANDARDS.md','ENGINE-DETAILS.md'))
source_sha=sha(canonical)
(payload/'README.txt').write_text('CHOS0.30.0 actual JavaScript pixel development diagnostic\nExact source: '+source+'\n\nBoot '+iso.name+'. This diagnostic has NO installer. In the desktop Console run /bin/jscvm.elf, press Space to recompute colors, and Escape to close. See TESTING.md. It is not a modern browser or Stage3 completion.\n\nThis includes the original patched WebKit source/build closure and all original notices, full native SDK/ICU, regular archives,53 separate engine objects and the new client object/source/headers. Run python3 compile.py then python3 relink.py with Clang19/LLD19 after extraction to build/relink independently. Engine/SDK bytes are unchanged from a89db07. Source and object material permit modified library relinking. See DISTRIBUTION.md and per-file licenses.\n\nCompanion exact CHOS source archive SHA256: '+source_sha+'\nDownload directory: https://github.com/garythurman-boop/CHOS-Second-Variant/tree/main/artifacts/'+short+'\nThe companion CHOS-'+short+'-source.zip contains exact current native port/runtime/dependency sources,licenses and build recipes. Both source package and this engine-source/relinking package form the distribution.\n\nVerify MANIFEST.json and SHA256SUMS before testing. Fusion execution of this new diagnostic is NOT RUN. WebCore/full browser, native Wi-Fi and remaining security/platform gates stay open. Version0.50.0 is reserved for full Stage3 completion.\n')
files={str(f.relative_to(payload)):{'bytes':f.stat().st_size,'sha256':sha(f)} for f in sorted(payload.rglob('*')) if f.is_file()}
record={'source_commit':source,'stage3_complete':False,'version':'0.30.0','engine_source_and_objects_from':'a89db07f60345bae56676eacfbefc7cf08f5973e','original_kit_manifest_sha256':sha(kit/'MANIFEST.json'),'qualified_staged_elf_sha256':stage['staged_sha256'],'client_object_sha256':sha(client/'main.cpp.obj'),'files':files,'external_CHOS_source_archive':{'name':canonical.name,'sha256':source_sha},'omitted_uncompiled_upstream_test_media':original['omitted_uncompiled_upstream_test_media']}
(payload/'MANIFEST.json').write_text(json.dumps(record,indent=2)+'\n')
(payload/'SHA256SUMS').write_text(''.join(sha(f)+'  '+str(f.relative_to(payload))+'\n' for f in sorted(payload.rglob('*')) if f.is_file()))
archive=out/(payload.name+'.zip')
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for f in sorted(payload.rglob('*')):
  if f.is_file():z.write(f,str(f.relative_to(out)))
assert archive.stat().st_size<95*1024*1024,'Handoff exceeds repository publication budget'
print(json.dumps({'archive':str(archive),'sha256':sha(archive),'bytes':archive.stat().st_size,'payload':str(payload),'companion_source_sha256':source_sha,'relink_validation_required':True},indent=2))
