#!/usr/bin/env python3
"""Compile pinned generic math with host sanitizers and prefix its ELF symbols.
Host fenv remains the host OS implementation; ARM64 assembly is tested in QEMU.
"""
import argparse,pathlib,json,subprocess,concurrent.futures,hashlib
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--cc',default='cc');p.add_argument('--sanitize',action='store_true');o=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];base=root/'third_party/musl';out=o.out.resolve();out.mkdir(parents=True,exist_ok=True)
m=json.loads((root/'docs/dependencies/musl-math.json').read_text())
gen=out/'include/bits';gen.mkdir(parents=True,exist_ok=True)
with (gen/'alltypes.h').open('wb') as f:subprocess.run(['sed','-f',str(base/'tools/mkalltypes.sed'),str(base/'arch/x86_64/bits/alltypes.h.in'),str(base/'include/alltypes.h.in')],stdout=f,check=True)
flags=['-std=c11','-D_XOPEN_SOURCE=700','-ffreestanding','-fno-builtin','-fno-stack-protector','-ffunction-sections','-fdata-sections','-frounding-math','-Wno-parentheses', '-I'+str(base/'arch/x86_64'),'-I'+str(base/'arch/generic'),'-I'+str(base/'src/include'),'-I'+str(base/'src/internal'),'-I'+str(out/'include'),'-I'+str(base/'include')]
flags+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if o.sanitize else ['-O2']
def compile(name):
    src=base/name;obj=out/(src.stem+'.o');cmd=[o.cc,*flags,'-c',str(src),'-o',str(obj)]
    r=subprocess.run(cmd,capture_output=True,text=True)
    if r.returncode:raise RuntimeError(name+'\n'+r.stderr[-2500:])
    return obj
objects=list(concurrent.futures.ThreadPoolExecutor(max_workers=4).map(compile,m['host_sources']))
nm=subprocess.check_output(['llvm-nm','-g','--defined-only',*[str(p) for p in objects]],text=True)
symbols={line.split()[-1] for line in nm.splitlines() if line and not line.endswith(':') and not line.split()[-1].startswith(('__asan','___asan','__odr_asan'))}
mapping=out/'prefix.map';mapping.write_text(''.join(s+' chos_m_'+s+'\n' for s in sorted(symbols)))
for obj in objects:subprocess.run(['llvm-objcopy','--redefine-syms='+str(mapping),str(obj)],check=True)
archive=out/'libhost-checked-m.a'
if archive.exists():archive.unlink()
subprocess.run(['llvm-ar','rcs',str(archive),*[str(p) for p in objects]],check=True)
report={'profile':'generic musl math on x86_64 host, not ARM64 fenv','sanitize':o.sanitize,'objects':len(objects),'prefixed_symbols':len(symbols),'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest()}
(out/'build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
