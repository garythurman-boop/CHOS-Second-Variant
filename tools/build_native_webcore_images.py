#!/usr/bin/env python3
"""Build the original concrete WebCore image component for isolated native CHOS.

The full format factory and shared-memory IPC are deliberately not supplied by
this component. A successful link alone does not qualify execution or WebCore.
"""
import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import shutil
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENGINE_MANIFEST = 'c5f2aabcf448415a9567cb7f0d2d1ad11ace0c2ef44731f1c8ff0a810b1b4757'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['out', 'native-sdk', 'engine-kit', 'graphics-prefix', 'font-prefix', 'png-prefix', 'jpeg-prefix', 'jpeg-reference-build']:
        parser.add_argument('--' + name, type=pathlib.Path, required=True)
    parser.add_argument('--fixture-path', type=pathlib.Path, default=ROOT/'tests/native_webcore_images')
    parser.add_argument('--cc', default='clang++')
    parser.add_argument('--jobs', type=int, choices=range(1,5), default=4)
    args = parser.parse_args()
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=False)
    sdk = args.native_sdk.resolve(); kit = args.engine_kit.resolve()
    fixture = args.fixture_path.resolve(); assets = out/'cases'
    profile_path = ROOT/'ports/webcore-images/source-list.json'
    profile = json.loads(profile_path.read_text())
    source_manifest_path = ROOT/'docs/dependencies/native-webcore-images.json'
    original = ROOT/'third_party/webcore-images'
    patches = [ROOT/'ports/webcore-images'/name for name in profile['patches']]
    compiler = shutil.which(args.cc)
    if not compiler: parser.error('Native compiler unavailable')
    report = {'status':'VALIDATING','linked':False,'executed':False,'stage3_complete':False,
              'commands':[], 'upstream_commit':profile['upstream_commit'],
              'builder_sha256':sha(pathlib.Path(__file__)), 'profile_sha256':sha(profile_path),
              'compiler_version':subprocess.check_output([compiler,'--version'],text=True),
              'source_manifest_sha256':sha(source_manifest_path),
              'patches_sha256':{p.name:sha(p) for p in patches},
              'limits':['Selected original in-process PNG/JPEG image component,not full WebCore or a browser',
                        'Format factory and shared-memory IPC entry points are not defined by this profile',
                        'Existing native WTF configuration plus explicit component flags;full generated WebCore configuration remains separate work',
                        'Linking is not execution,full ABI/conformance,security or physical platform qualification']}
    started = time.monotonic()
    def save(): (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(name, argv):
        report['commands'].append(argv); save()
        with (out/(name+'.log')).open('wb') as log:
            code = subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT).returncode
        if code:
            raise RuntimeError(name+' failed with status '+str(code))
    def verify():
        subprocess.run(['python3',str(ROOT/'tools/verify_webcore_image_sources.py')],check=True)
        subprocess.run(['python3',str(ROOT/'tools/verify_native_sdk.py'),str(sdk)],check=True)
        assert sha(kit/'MANIFEST.json') == ENGINE_MANIFEST, 'Wrong published engine kit manifest'
        manifest = json.loads((kit/'MANIFEST.json').read_text())
        count = 0
        for name, entry in manifest['files'].items():
            if name.startswith(('source/webkit/Source/WTF/','source/webkit/Source/bmalloc/')) or name in ['engine/lib/libWTF.a','engine/lib/libbmalloc.a']:
                path = kit/name
                assert path.resolve().is_relative_to(kit) and path.stat().st_size == entry['bytes'] and sha(path) == entry['sha256'],name
                count += 1
        assert count == 2118, 'Unexpected native WTF/bmalloc inventory'
        return {'manifest_sha256':ENGINE_MANIFEST,'files_verified':count}
    def verify_prefix(path):
        metadata_path = path.parent/'build.json'; metadata = json.loads(metadata_path.read_text())
        assert metadata['host_reference'] is False and metadata['status']=='LINKED_NOT_EXECUTED', 'Native finished prefix required: '+str(path)
        for name, expected in metadata['installed_files_sha256'].items():
            entry=path/name
            assert entry.resolve().is_relative_to(path) and sha(entry)==expected,str(entry)
        return {'path':str(path),'build_report_sha256':sha(metadata_path),'installed_files':len(metadata['installed_files_sha256'])}
    try:
        report['engine_kit']=verify(); report['SDK_manifest_sha256']=sha(sdk/'chos-sdk.json')
        prefixes={n:getattr(args,n+'_prefix').resolve() for n in ['graphics','font','png','jpeg']}
        report['prefixes']={n:verify_prefix(p) for n,p in prefixes.items()}
        snapshot={n:sha(fixture/n) for n in ['main.cpp']}
        generator=ROOT/'tools/generate_webcore_image_cases.py'
        report['case_generator_sha256']=sha(generator)
        run('cases',['python3',str(generator),'--out',str(assets),'--jpeg-reference-build',str(args.jpeg_reference_build.resolve())])
        asset_names=['assets.h','assets.json','generated.png','original.jpg','large.png']
        asset_snapshot={n:sha(assets/n) for n in asset_names}
        report['fixture_sources_sha256']=snapshot; report['fixture_assets_sha256']=asset_snapshot
        copied=out/'source'; shutil.copytree(original,copied)
        for patch in patches: run('patch-'+patch.stem,['patch','--batch','--fuzz=0','--no-backup-if-mismatch','-d',str(copied),'-p1','-i',str(patch)])
        copied_assets=out/'assets'; copied_assets.mkdir()
        for name in asset_names: shutil.copyfile(assets/name,copied_assets/name)
        assembly=['.section .rodata.webcore_fixtures,"a"']
        for stem,name in [('generated','generated.png'),('original','original.jpg'),('large','large.png')]:
            assembly += ['.balign 8','.global '+stem+'_start',stem+'_start:','.incbin '+json.dumps(str(copied_assets/name)),'.global '+stem+'_end',stem+'_end:']
        (copied_assets/'assets.S').write_text('\n'.join(assembly)+'\n')
        source_snapshot={str(p.relative_to(copied)):sha(p) for p in copied.rglob('*') if p.is_file()}
        report['patched_source_files_sha256']=source_snapshot
        includes=out/'include'
        for project in ['WebCore','JavaScriptCore']:
            forward=includes/project; forward.mkdir(parents=True)
            headers={}
            for path in (copied/'Source'/project).rglob('*.h'):headers.setdefault(path.name,[]).append(path)
            for name,paths in headers.items():
                if len(paths)==1:(forward/name).symlink_to(paths[0])
        wc=copied/'Source/WebCore'
        paths=[includes,copied_assets,wc,kit/'source/webkit/Source/WTF',kit/'source/webkit/Source/bmalloc',wc/'PAL']
        paths += [wc/name for name in ['platform','platform/text','platform/graphics','platform/graphics/cairo','platform/graphics/transforms','platform/graphics/filters','platform/graphics/freetype','platform/graphics/harfbuzz','platform/graphics/opentype','platform/image-decoders','platform/image-decoders/png','platform/image-decoders/jpeg','platform/network','rendering','rendering/style','page','dom','loader'] if (wc/name).exists()]
        paths += [prefixes['graphics']/'include/cairo',prefixes['graphics']/'include/pixman-1',prefixes['font']/'include/freetype2',prefixes['font']/'include/harfbuzz',prefixes['png']/'include',prefixes['jpeg']/'include']
        base=[compiler,'--target=aarch64-none-elf','--sysroot='+str(sdk),'-std=c++23','-O2','-DNDEBUG','-fhosted','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie','-femulated-tls','-ffunction-sections','-fdata-sections','-mcpu=cortex-a72','-mstrict-align','-fno-exceptions','-frtti','-nostdinc++','-isystem',str(sdk/'include/c++/v1'),'-isystem',str(sdk/'include'),'-D__CHOS__=1']
        base += ['-D'+name+'='+str(value) for name,value in profile['defines'].items()]
        for path in paths:base += ['-I',str(path)]
        unit_sources={name:copied/path for name,path in profile['units'].items()}
        unit_sources.update(main=fixture/'main.cpp',assets=copied_assets/'assets.S')
        objects=out/'objects';objects.mkdir();report['status']='COMPILING';save()
        def compile_one(item):
            name,source=item;obj=objects/(name+'.o');cmd=base+['-MD','-MF',str(objects/(name+'.d')),'-c',str(source),'-o',str(obj)]
            (objects/(name+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
            with (objects/(name+'.log')).open('wb') as log:code=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT).returncode
            if code:raise RuntimeError('Compile '+name+' failed;see '+str(objects/(name+'.log')))
            headers=subprocess.check_output(['llvm-readelf','-h',str(obj)],text=True);assert 'AArch64' in headers
            return name,{'source_sha256':sha(source),'object_sha256':sha(obj),'bytes':obj.stat().st_size,'command':cmd,'dependency_file_sha256':sha(objects/(name+'.d'))}
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:report['objects']=dict(pool.map(compile_one,unit_sources.items()))
        libraries=[kit/'engine/lib/libWTF.a',kit/'engine/lib/libbmalloc.a',prefixes['graphics']/'lib/libcairo.a',prefixes['graphics']/'lib/libpixman-1.a',prefixes['png']/'lib/liblibpng16_static.a',prefixes['png']/'lib/libz-memory.a',prefixes['jpeg']/'lib/libjpeg.a']
        libraries += [sdk/'lib'/('libchos-'+n+'.a') for n in ['c++','m','quad','complex','wide','stdio','format','floatparse','c']]
        libraries += [prefixes['font']/'lib'/n for n in ['libfreetype.a','libharfbuzz.a','libharfbuzz-icu.a','libchos-text.a']]
        assert all(p.is_file() for p in libraries),'Missing selected native archive'
        report['libraries_sha256']={str(p):sha(p) for p in libraries}
        elf=out/'native_webcore_images.elf'
        link=[compiler,'--target=aarch64-none-elf','-nostdlib','-static','-fuse-ld=lld','-Wl,-T,'+str(sdk/'lib/app-engine.ld'),'-Wl,--gc-sections','-Wl,-z,max-page-size=4096','-Wl,--error-limit=0','-Wl,-Map,'+str(out/'application.map'),'-Wl,--strip-debug']
        link += [str(sdk/'lib'/n) for n in ['crt0.o','crtinit.o','chos-engine-abi.o']]
        link += [str(objects/(n+'.o')) for n in unit_sources]
        link += ['-Wl,--start-group',*map(str,libraries),'-Wl,--end-group','-o',str(elf)]
        run('link',link)
        assert not subprocess.check_output(['llvm-nm','-u',str(elf)],text=True).strip(),'Unresolved native symbols'
        assert 'AArch64' in subprocess.check_output(['llvm-readelf','-h',str(elf)],text=True)
        verify()
        assert {str(p.relative_to(copied)):sha(p) for p in copied.rglob('*') if p.is_file()}==source_snapshot,'Build source copy changed during compilation'
        for name,path in prefixes.items():assert verify_prefix(path)==report['prefixes'][name]
        for n,h in snapshot.items():assert sha(fixture/n)==h,'Fixture changed during build'
        for n,h in asset_snapshot.items():assert sha(assets/n)==h,'Asset changed during build'
        assert sha(profile_path)==report['profile_sha256'] and sha(source_manifest_path)==report['source_manifest_sha256']
        assert sha(pathlib.Path(__file__))==report['builder_sha256'],'Builder changed during execution'
        assert sha(generator)==report['case_generator_sha256']
        assert all(sha(p)==report['patches_sha256'][p.name] for p in patches)
        report.update(status='LINKED_NOT_EXECUTED',linked=True,application={'path':str(elf),'bytes':elf.stat().st_size,'sha256':sha(elf)},seconds=time.monotonic()-started)
        save();print(json.dumps({'status':report['status'],'application':report['application']},indent=2))
    except Exception as error:
        report.update(status='BUILD_FAILED',error=str(error),seconds=time.monotonic()-started);save();raise

if __name__=='__main__':main()
