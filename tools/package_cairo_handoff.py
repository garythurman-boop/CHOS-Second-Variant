#!/usr/bin/env python3
"""Package a selected Cairo test milestone with actual source and relink inputs."""
import argparse,pathlib,json,hashlib,subprocess,shutil,shlex,zipfile
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--graphics-build',type=pathlib.Path,required=True)
p.add_argument('--font-build',type=pathlib.Path,required=True)
p.add_argument('--sdk',type=pathlib.Path,required=True)
p.add_argument('--images',type=pathlib.Path,required=True)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--ninja',default='ninja')
o=p.parse_args()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
source=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
assert not subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip(),'Commit the reviewed source before packaging'
g=o.graphics_build.resolve();f=o.font_build.resolve();sdk=o.sdk.resolve();images=o.images.resolve()
gr=json.loads((g/'build.json').read_text());fr=json.loads((f/'build.json').read_text())
assert not gr['host_reference'] and not fr['host_reference']
subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
assert sha(sdk/'chos-sdk.json')==gr['SDK_manifest_sha256']
for build,report in [(g,gr),(f,fr)]:
    for name,wanted in report['installed_files_sha256'].items():assert sha(build/'prefix'/name)==wanted,name
for name,wanted in gr['fixture_sources'].items():assert sha(root/name)==wanted,name
for name,wanted in gr['port_patches_sha256'].items():assert sha(root/'ports/graphics'/name)==wanted,name
app=g/'application/native_cairo';assert sha(app)==gr['application']['sha256']
validation=json.loads((root/'docs/validation/stage3-native-cairo.json').read_text())
assert validation['status']=='PASS_SELECTED_COMPONENT_AND_REGRESSIONS'
assert validation['native_build']['application']['sha256']==sha(app)
for name,wanted in validation['runtime_sources'].items():assert sha(root/name)==wanted,name
stage=json.loads((images/'native-engine.json').read_text());assert stage['source_sha256']==sha(app)
kernel=images/'uefi-kernel.elf';assert ('[BUILD] '+source[:12]+'\n\0').encode() in kernel.read_bytes()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);kit=out/('CHOS-Cairo-'+source[:7]);kit.mkdir()
shutil.copytree(sdk,kit/'sdk');shutil.copytree(g/'prefix',kit/'graphics-prefix');shutil.copytree(f/'prefix',kit/'font-prefix')
application=kit/'application';application.mkdir()
for name in ['main.c.obj','font.S.obj']:
    shutil.copyfile(g/'application/CMakeFiles/native_cairo.dir'/name,application/name)
shutil.copyfile(app,application/'native_cairo.elf')
subprocess.run(['git','archive','--worktree-attributes','--format=zip','--prefix=CHOS-Second-Variant/','--output='+str(kit/'SOURCE.zip'),source],cwd=root,check=True)
shutil.copyfile(images/'chos.iso',kit/('CHOS-Native-Cairo-'+source[:7]+'.iso'))
shutil.copyfile(root/'build/chos-0.30.0.iso',kit/('CHOS-Fusion-'+source[:7]+'.iso'))
for source_file,name in [(root/'docs/NATIVE_CAIRO_FUSION_TESTING.md','FUSION-TESTING.md'),(root/'docs/validation/stage3-native-cairo.json','VALIDATION.json'),(root/'docs/validation/stage3-native-cairo-uefi.png','PREVIEW.png')]:shutil.copyfile(source_file,kit/name)
shutil.copyfile(g/'build.json',kit/'GRAPHICS-BUILD.json');shutil.copyfile(f/'build.json',kit/'FONT-BUILD.json')
licenses=kit/'LICENSES';licenses.mkdir()
for original,name in [('third_party/cairo/COPYING','Cairo-COPYING'),('third_party/cairo/COPYING-LGPL-2.1','Cairo-LGPL-2.1'),('third_party/cairo/COPYING-MPL-1.1','Cairo-MPL-1.1'),('third_party/pixman/COPYING','pixman-COPYING'),('third_party/freetype/docs/FTL.TXT','FreeType-FTL.txt'),('third_party/text-fixtures/OFL.txt','Font-OFL.txt'),('third_party/text-fixtures/NOTO-COPYRIGHT','Noto-COPYRIGHT')]:shutil.copyfile(root/original,licenses/name)
raw=subprocess.check_output([o.ninja,'-C',str(g/'application'),'-t','commands','native_cairo'],text=True).splitlines()[-1]
tokens=shlex.split(raw);assert tokens[:2]==[':','&&'] and tokens[-2:]==['&&',':'];tokens=tokens[2:-2]
args=[];skip=False
for token in tokens:
    if skip:skip=False;continue
    if token=='-Xlinker':skip=True;continue
    if token.startswith('-Wl,-Map,'):continue
    if token=='native_cairo':token='@OUTPUT@'
    if token.startswith('CMakeFiles/native_cairo.dir/'):
        token='@KIT@/application/'+pathlib.Path(token).name
    token=token.replace(str(sdk),'@KIT@/sdk').replace(str(g/'prefix'),'@KIT@/graphics-prefix').replace(str(f/'prefix'),'@KIT@/font-prefix')
    args.append(token)
args[0]='clang';assert not any('/workspace/' in a or a in ['&&',':'] for a in args)
(kit/'relink-argv.json').write_text(json.dumps({'argv':args,'expected_elf_sha256':sha(app),'source_commit':source,'removed_nonsemantic_outputs':['CMake dependency file','linker map']},indent=2)+'\n')
relink='''#!/usr/bin/env python3
import argparse,pathlib,json,hashlib,subprocess
root=pathlib.Path(__file__).resolve().parent
p=argparse.ArgumentParser(description="Relink the actual isolated Cairo executable from supplied objects and libraries")
p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--cc',default='clang')
p.add_argument('--allow-modified',action='store_true',help='Intentionally relink user-modified libraries/objects; do not claim original qualification')
o=p.parse_args();out=o.output.resolve();assert not out.exists(),'Choose a new output file'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if not o.allow_modified:
    for name,wanted in json.loads((root/'MANIFEST.json').read_text())['files'].items():
        file=root/name;assert file.is_file() and not file.is_symlink() and file.resolve().is_relative_to(root)
        assert file.stat().st_size==wanted['bytes'] and sha(file)==wanted['sha256'],name
record=json.loads((root/'relink-argv.json').read_text())
args=[arg.replace('@KIT@',str(root)).replace('@OUTPUT@',str(out)) for arg in record['argv']];args[0]=o.cc
subprocess.run(args,cwd=root,check=True)
actual=sha(out);matches=actual==record['expected_elf_sha256']
print(json.dumps({'output':str(out),'sha256':actual,'matches_original':matches,'user_modified':o.allow_modified},indent=2))
assert matches or o.allow_modified,'Relink differs from the qualified original'
'''
(kit/'relink.py').write_text(relink)
(kit/'README.txt').write_text('CHOS Cairo development test milestone\nSource commit: '+source+'\n\nStage 3 is incomplete; version remains 0.30.0. This is a real software graphics diagnostic, not a modern browser.\nThis software uses FreeType. Cairo/pixman/FreeType/font and SDK notices are supplied, with original corresponding sources in SOURCE.zip.\n\nUse FUSION-TESTING.md. The separate Native-Cairo ISO has no installer. Normal Fusion installation media are included independently.\n\nThe complete original SDK, actual static graphics/font archives, application objects, native ELF and portable link arguments are supplied. With AArch64 Clang/LLD in PATH, run python3 relink.py --output /new/native_cairo.elf. The unmodified result must match exactly. --allow-modified permits intentionally changed libraries/objects without asserting original qualification.\n\nTo rebuild libraries, extract SOURCE.zip, use the supplied SDK with tools/build_native_text.py to build a fresh font prefix, then tools/build_native_graphics.py with that prefix. Existing exported .pc/CMake metadata retains original absolute build paths; full source rebuilds generate fresh paths. Relinking uses explicit portable archive paths and needs no host libraries. Kernel/ISO build and staging instructions are in docs/NATIVE_CAIRO_RENDERING.md.\n\nSDK archives not imported by this executable are retained as complete original SDK inputs; source/notice scope and preceding SDK compilation provenance remain explicit in the manifests. QEMU evidence does not certify new Fusion/Pi5/laptop behavior.\n')
manifest={'schema':1,'source_commit':source,'native_application_sha256':sha(app),'stage3_complete':False,'files':{str(path.relative_to(kit)):{'bytes':path.stat().st_size,'sha256':sha(path)} for path in sorted(kit.rglob('*')) if path.is_file()}}
(kit/'MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
subprocess.run(['python3',str(kit/'relink.py'),'--output',str(out/'roundtrip.elf')],check=True)
archive=out/('CHOS-Cairo-'+source[:7]+'-Handoff.zip')
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for path in sorted(kit.rglob('*')):
        if path.is_file():z.write(path,str(path.relative_to(out)))
assert archive.stat().st_size<95*1024*1024,'Split the corresponding sources/SDK before publication; never omit them'
(out/'package.json').write_text(json.dumps({'source_commit':source,'archive':str(archive),'sha256':sha(archive),'bytes':archive.stat().st_size,'relink_matches_qualified_elf':True,'payload_files':len(manifest['files'])},indent=2)+'\n')
print((out/'package.json').read_text())
