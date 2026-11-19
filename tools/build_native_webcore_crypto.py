#!/usr/bin/env python3
"""Build real PAL CryptoDigest with original MbedTLS; selected component only.

Host mode sanitizes the same C backend and original crypto units. Native mode
additionally executes the unchanged original PAL header and published real WTF.
Building does not execute either fixture or qualify the full WebCore port.
"""
import argparse, hashlib, json, pathlib, shutil, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
ENGINE_MANIFEST='c5f2aabcf448415a9567cb7f0d2d1ad11ace0c2ef44731f1c8ff0a810b1b4757'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['out','native-sdk','engine-kit','webcore-source']:p.add_argument('--'+n,type=pathlib.Path,required=n=='out')
    p.add_argument('--host',action='store_true');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
    if a.host==bool(a.native_sdk):p.error('Choose --host or --native-sdk')
    if not a.host and (not a.engine_kit or not a.webcore_source):p.error('Native requires actual --engine-kit and --webcore-source')
    if a.sanitize and not a.host:p.error('--sanitize requires --host')
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);objects=out/'objects';objects.mkdir()
    report={'status':'VALIDATING','executed':False,'stage3_complete':False,'host_reference':a.host,'host_sanitizers':a.sanitize,'commands':[]}
    start=time.monotonic()
    def save():(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(name,cmd):
        report['commands'].append(cmd);save()
        with (out/(name+'.log')).open('wb') as log:rc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT).returncode
        if rc:raise RuntimeError(name+' failed with status '+str(rc))
    inputs=['tools/build_native_webcore_crypto.py','ports/webcore-crypto/source-list.json']
    inputs += [str(x.relative_to(ROOT)) for x in sorted((ROOT/'ports/webcore-crypto').glob('*')) if x.suffix in ['.c','.h','.cpp']]
    inputs += [str(x.relative_to(ROOT)) for x in sorted((ROOT/'tests/native_webcore_crypto').glob('*')) if x.suffix in ['.c','.h','.cpp','.py']]
    snapshot={n:sha(ROOT/n) for n in inputs};report['sources_sha256']=snapshot
    manifest=ROOT/'docs/dependencies/mbedtls.json';report['mbedtls_manifest_sha256']=sha(manifest)
    tls_config=ROOT/'userland/tls/include/chos_tls_config.h'
    report['separate_TLS_config_sha256']=sha(tls_config)
    assert report['separate_TLS_config_sha256']==json.loads(manifest.read_text())['native_tls_profile']['configuration_sha256']
    try:
        run('verify-crypto',['python3',str(ROOT/'tools/verify_mbedtls.py')])
        run('cases',['python3',str(ROOT/'tests/native_webcore_crypto/generate_cases.py'),'--out',str(out/'cases')])
        report['cases_manifest_sha256']=sha(out/'cases/cases.json')
        source=out/'source';shutil.copytree(ROOT/'third_party/mbedtls',source/'mbedtls')
        port=ROOT/'ports/webcore-crypto';fixture=ROOT/'tests/native_webcore_crypto'
        include=[port,source/'mbedtls/include',source/'mbedtls/library',fixture,out/'cases']
        cflags=['-O2','-DNDEBUG','-ffunction-sections','-fdata-sections','-DMBEDTLS_CONFIG_FILE="chos_digest_config.h"']
        libraries=[]
        if a.host:
            cc=shutil.which('gcc');cxx=None
            if a.sanitize:cflags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-O1']
        else:
            sdk=a.native_sdk.resolve();kit=a.engine_kit.resolve()
            run('verify-sdk',['python3',str(ROOT/'tools/verify_native_sdk.py'),str(sdk)])
            report['SDK_manifest_sha256']=sha(sdk/'chos-sdk.json')
            assert sha(kit/'MANIFEST.json')==ENGINE_MANIFEST
            count=0
            for n,v in json.loads((kit/'MANIFEST.json').read_text())['files'].items():
                if n.startswith(('source/webkit/Source/WTF/','source/webkit/Source/bmalloc/')) or n in ['engine/lib/libWTF.a','engine/lib/libbmalloc.a']:
                    f=kit/n;assert f.resolve().is_relative_to(kit) and f.stat().st_size==v['bytes'] and sha(f)==v['sha256'],n;count+=1
            assert count==2118;report['engine_kit']={'manifest_sha256':ENGINE_MANIFEST,'verified_files':count}
            original=ROOT/'ports/webcore-crypto/source-list.json';headers=json.loads(original.read_text())
            wc=a.webcore_source.resolve();report['original_PAL_headers']=headers
            assert subprocess.check_output(['git','-C',str(wc),'rev-parse','HEAD'],text=True).strip()==headers['upstream_commit']
            for n,v in headers['original_headers'].items():
                f=wc/n;assert sha(f)==v['sha256'] and f.stat().st_size==v['bytes'],n
                assert subprocess.check_output(['git','-C',str(wc),'rev-parse',headers['upstream_commit']+':'+n],text=True).strip()==v['git_blob']
                dest=source/n;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(f,dest)
            include += [source/'Source/WebCore/PAL',kit/'source/webkit/Source/WTF',kit/'source/webkit/Source/bmalloc']
            cc=shutil.which('clang');cxx=shutil.which('clang++')
            cflags+=['--target=aarch64-none-elf','--sysroot='+str(sdk),'-fhosted','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie','-femulated-tls','-mcpu=cortex-a72','-mstrict-align','-D__CHOS__=1']
            libraries=[kit/'engine/lib/libWTF.a',kit/'engine/lib/libbmalloc.a']
            libraries += [sdk/'lib'/('libchos-'+n+'.a') for n in ['c++','m','quad','complex','wide','stdio','format','floatparse','c']]
            libraries += [sdk/'lib'/n for n in ['libicuuc.a','libicui18n.a','libicudata.a']]
        for path in include:cflags+=['-I',str(path)]
        units={n:source/'mbedtls/library'/(n+'.c') for n in ['sha1','sha256','sha512','platform_util']}
        units.update(backend=port/'DigestBackend.c',faults=fixture/'faults.c',backend_tests=fixture/'backend.c')
        if a.host:units['main']=fixture/'host.c'
        else:units.update(adapter=port/'CryptoDigestCHOS.cpp',main=fixture/'main.cpp')
        run('profile',[cc,*cflags,'-dM','-E','-x','c','-include','mbedtls/build_info.h','/dev/null'])
        macros=(out/'profile.log').read_text()
        for algorithm in ['SHA1','SHA224','SHA256','SHA384','SHA512']:assert '#define MBEDTLS_'+algorithm+'_C' in macros
        for excluded in ['SSL_PROTO_TLS1_2','SSL_PROTO_TLS1_3','X509_CRT_PARSE_C','PSA_CRYPTO_C']:assert '#define MBEDTLS_'+excluded not in macros
        report['digest_only_profile_sha256']=sha(out/'profile.log')
        for name,file in units.items():
            flags=cflags[:]
            compiler=cc
            if file.suffix=='.cpp':
                compiler=cxx;flags+=['-std=c++23','-fno-exceptions','-frtti','-nostdinc++','-isystem',str(sdk/'include/c++/v1')]
                flags+=['-D'+v for v in ['USE_SYSTEM_MALLOC=1','HAVE_MMAP=1','BUILDING_JSCONLY__=1','STATICALLY_LINKED_WITH_WTF=1','STATICALLY_LINKED_WITH_JavaScriptCore=1','STATICALLY_LINKED_WITH_PAL=1','ENABLE_JIT=0','ENABLE_DFG_JIT=0','ENABLE_FTL_JIT=0','ENABLE_WEBASSEMBLY=0']]
            else:flags+=['-std=c11']
            if not a.host:flags+=['-isystem',str(sdk/'include')]
            run('compile-'+name,[compiler,*flags,'-MD','-MF',str(objects/(name+'.d')),'-c',str(file),'-o',str(objects/(name+'.o'))])
        wraps=['mbedtls_sha'+n+'_'+op for n in ['1','256','512'] for op in ['starts','update','finish']]
        wraps=['chos_pal_'+n for n in wraps]+['calloc']
        app=out/'native_webcore_crypto';link=[cxx or cc]
        if a.host:
            if a.sanitize:link+=['-fsanitize=address,undefined']
            link+=['-pthread']
        else:
            link+=['--target=aarch64-none-elf','-nostdlib','-static','-fuse-ld=lld','-Wl,-T,'+str(sdk/'lib/app-engine.ld'),'-Wl,--gc-sections','-Wl,-z,max-page-size=4096','-Wl,--error-limit=0','-Wl,--strip-debug','-Wl,-Map,'+str(out/'application.map')]
            link += [str(sdk/'lib'/n) for n in ['crt0.o','crtinit.o','chos-engine-abi.o']]
        link += [str(objects/(n+'.o')) for n in units]
        link += ['-Wl,--wrap='+n for n in wraps]
        if libraries:link += ['-Wl,--start-group',*map(str,libraries),'-Wl,--end-group']
        run('link',[*link,'-o',str(app)])
        if not a.host:
            assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
            assert 'AArch64' in subprocess.check_output(['llvm-readelf','-h',str(app)],text=True)
        symbols=subprocess.check_output(['llvm-nm','-g','--defined-only',*[str(objects/(n+'.o')) for n in ['sha1','sha256','sha512','platform_util']]],text=True)
        assert 'chos_pal_mbedtls_sha256_starts' in symbols
        assert not any(line.split()[-1].startswith('mbedtls_') for line in symbols.splitlines() if len(line.split())>=3),'Digest target exports an unnamespaced TLS symbol'
        report['private_crypto_symbols']=symbols.splitlines()
        for n,v in snapshot.items():assert sha(ROOT/n)==v,'Input changed during build: '+n
        assert sha(manifest)==report['mbedtls_manifest_sha256']
        assert sha(tls_config)==report['separate_TLS_config_sha256']
        copied=json.loads(manifest.read_text())['files']
        for n,v in copied.items():assert sha(source/'mbedtls'/n)==v,'Copied original modified: '+n
        report['copied_mbedtls_files_verified']=len(copied)
        for n,v in report.get('original_PAL_headers',{}).get('original_headers',{}).items():assert sha(source/n)==v['sha256']
        run('reverify-crypto',['python3',str(ROOT/'tools/verify_mbedtls.py')])
        if not a.host:run('reverify-sdk',['python3',str(ROOT/'tools/verify_native_sdk.py'),str(sdk)])
        report.update(status='LINKED_NOT_EXECUTED',seconds=time.monotonic()-start,application={'path':str(app),'bytes':app.stat().st_size,'sha256':sha(app)},libraries_sha256={str(n):sha(n) for n in libraries},objects_sha256={n:sha(objects/(n+'.o')) for n in units})
        save();print(json.dumps({'status':report['status'],'application':report['application']},indent=2))
    except Exception as e:
        report.update(status='BUILD_FAILED',error=str(e),seconds=time.monotonic()-start);save();raise
if __name__=='__main__':main()
