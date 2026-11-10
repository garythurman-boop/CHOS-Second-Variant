#!/usr/bin/env python3
"""Build original software Cairo/FreeType and pixman with a real native CRT.

Does not run target programs or claim WebCore/browser integration. Native package
lookup is confined to explicitly verified SDK/dependency prefixes.
"""
import argparse,pathlib,json,hashlib,subprocess,shutil,os,time
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--native-sdk',type=pathlib.Path)
p.add_argument('--font-build',type=pathlib.Path,required=True)
p.add_argument('--host',action='store_true');p.add_argument('--sanitize',action='store_true')
p.add_argument('--meson',default='meson');p.add_argument('--ninja',default='ninja')
p.add_argument('--cmake',default='cmake')
o=p.parse_args();assert o.host!=bool(o.native_sdk);assert not o.sanitize or o.host
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
subprocess.run(['python3',str(root/'tools/verify_native_graphics_sources.py')],check=True)
subprocess.run(['python3',str(root/'tools/verify_native_text_sources.py')],check=True)
font=o.font_build.resolve();f=json.loads((font/'build.json').read_text())
assert f['host_reference']==o.host and f['host_sanitizers']==o.sanitize
for name,wanted in f['installed_files_sha256'].items():assert sha(font/'prefix'/name)==wanted,name
sdk=o.native_sdk.resolve() if o.native_sdk else None
if sdk:subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);prefix=out/'prefix'
env=os.environ.copy();env.update(PKG_CONFIG_PATH='',PKG_CONFIG_SYSROOT_DIR='',PKG_CONFIG_LIBDIR=':'.join(str(p/'lib/pkgconfig') for p in [prefix,font/'prefix']))
meson=shutil.which(o.meson) or o.meson;ninja=shutil.which(o.ninja) or o.ninja
commands=[];report={'status':'BUILDING','executed':False,'stage3_complete':False,'host_reference':o.host,'host_sanitizers':o.sanitize,'source_manifest_sha256':sha(root/'docs/dependencies/native-graphics.json'),'font_build_report_sha256':sha(font/'build.json'),'font_compilation_SDK_manifest_sha256':f['SDK_manifest_sha256'],'SDK_manifest_sha256':sha(sdk/'chos-sdk.json') if sdk else None,'commands':commands,'limits':['Software Cairo/pixman dependency,not WebCore/DOM/HTML/CSS or a modern browser','Image/recording/observer and explicit FreeType face backend;Fontconfig,PNG,PDF/PS/SVG output,display servers and GPU not supplied by this profile','Full upstream rendering corpus and production graphics/font isolation remain separate gates']}
start=time.monotonic()
def run(name,args):
    commands.append(args);(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    with (out/(name+'.log')).open('wb') as log:
        try:code=subprocess.run(args,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT).returncode
        except OSError as error:log.write((str(error)+'\n').encode());code=127
    if code:
        report.update(status='BUILD_FAILED',failed_step=name,returncode=code,seconds=time.monotonic()-start)
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(code)
base=[meson,'setup','--buildtype','release','--wrap-mode','nofallback','--prefix',str(prefix),'--default-library','static','--libdir','lib','-Dauto_features=disabled']
if sdk:
    cc=shutil.which('clang');ar=shutil.which('llvm-ar');strip=shutil.which('llvm-strip');assert cc and ar and strip
    flags=['--target=aarch64-none-elf','--sysroot='+str(sdk),'-D__CHOS__=1','-fhosted','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie','-femulated-tls','-ffunction-sections','-fdata-sections','-mcpu=cortex-a72','-mstrict-align','-isystem',str(sdk/'include')]
    # Meson already groups static libraries. Nested --start-group would make
    # genuine link probes fail and silently misdetect mutex/atomic capabilities.
    link=['-nostdlib','-static','-fuse-ld=lld','-Wl,-T,'+str(sdk/'lib/app-engine.ld'),'-Wl,--gc-sections','-Wl,-z,max-page-size=4096',*[str(sdk/'lib'/n) for n in ['crt0.o','crtinit.o','chos-engine-abi.o']],*[str(sdk/'lib'/('libchos-'+n+'.a')) for n in ['c++','m','quad','complex','wide','stdio','format','floatparse','c']]]
    cross=out/'chos-cross.ini'
    cross.write_text('[binaries]\nc = '+repr(cc)+'\nar = '+repr(ar)+'\nstrip = '+repr(strip)+"\npkg-config = '/usr/bin/pkg-config'\n[host_machine]\nsystem = 'chos'\ncpu_family = 'aarch64'\ncpu = 'cortex-a72'\nendian = 'little'\n[properties]\nneeds_exe_wrapper = true\npkg_config_libdir = "+repr(env['PKG_CONFIG_LIBDIR'])+'\n[built-in options]\nc_args = '+repr(flags)+'\nc_link_args = '+repr(link)+"\nb_staticpic = false\nb_pie = false\n")
    base+=['--cross-file',str(cross)]
if o.sanitize:base+=['-Db_sanitize=address,undefined','-Dc_args=-fno-omit-frame-pointer']
pix=out/'pixman';cairo=out/'cairo'
run('pixman-setup',[*base,str(pix),str(root/'third_party/pixman'),'-Dtls=enabled',*(['-Da64-neon=enabled','-Dgnu-inline-asm=enabled'] if sdk else [])])
run('pixman-build',[ninja,'-C',str(pix),'-j4']);run('pixman-install',[meson,'install','-C',str(pix)])
cairo_source=out/'cairo-source';shutil.copytree(root/'third_party/cairo',cairo_source)
patches=[root/'ports/graphics'/name for name in ['cairo-postscript-spool.patch','cairo-bounded-debug.patch']]
report['port_patches_sha256']={p.name:sha(p) for p in patches}
with (out/'cairo-patch.log').open('wb') as log:
    for patch in patches:
        subprocess.run(['patch','--batch','--fuzz=0','--no-backup-if-mismatch','-p1','-i',str(patch)],cwd=cairo_source,stdout=log,stderr=subprocess.STDOUT,check=True)
run('cairo-setup',[*base,str(cairo),str(cairo_source),'-Dfreetype=enabled'])
configuration=(cairo/'config.h').read_text()
assert '#define CAIRO_HAS_REAL_PTHREAD 1' in configuration and '#define HAVE_C11_ATOMIC_PRIMITIVES 1' in configuration,'Real thread/atomic implementation required'
assert 'CAIRO_NO_MUTEX' not in configuration
run('cairo-build',[ninja,'-C',str(cairo),'-j4']);run('cairo-install',[meson,'install','-C',str(cairo)])
archives={}
for name in ['libcairo.a','libpixman-1.a']:
    path=prefix/'lib'/name;members=subprocess.check_output(['llvm-ar','t',str(path)],text=True).splitlines()
    if sdk:assert subprocess.check_output(['llvm-readelf','-h',str(path)],text=True).count('Machine:                           AArch64')==len(members)
    archives[name]={'bytes':path.stat().st_size,'sha256':sha(path),'members':len(members)}
client_sources={n:sha(root/n) for n in ['tests/native_cairo/main.c','tests/native_cairo/CMakeLists.txt']}
cmake=shutil.which(o.cmake) or o.cmake
application=out/'application'
args=[cmake,'-S',str(root/'tests/native_cairo'),'-B',str(application),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DGRAPHICS_PREFIX='+str(prefix),'-DFONT_PREFIX='+str(font/'prefix')]
if sdk:args+=['-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCHOS_APPLICATION_PROFILE=engine']
if o.sanitize:args+=['-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
run('application-setup',args);run('application-build',[cmake,'--build',str(application),'-j4'])
for name,wanted in client_sources.items():assert sha(root/name)==wanted,'Fixture source changed during build'
elf=application/'native_cairo'
if sdk:assert not subprocess.check_output(['llvm-nm','-u',str(elf)],text=True).strip(),'Unresolved target symbols'
report.update(status='LINKED_NOT_EXECUTED',returncode=0,seconds=time.monotonic()-start,archives=archives,fixture_sources=client_sources,application={'path':str(elf),'sha256':sha(elf),'bytes':elf.stat().st_size},installed_files_sha256={str(p.relative_to(prefix)):sha(p) for p in sorted(prefix.rglob('*')) if p.is_file()})
(out/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'archives':archives},indent=2))
