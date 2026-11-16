#!/usr/bin/env python3
"""Generate Fontconfig headers with the original Python and gperf generators."""
import argparse,pathlib,re,shutil,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=pathlib.Path,required=True)
p.add_argument('--original-source',type=pathlib.Path,required=True)
p.add_argument('--gperf',required=True)
p.add_argument('--host-cc',default='clang')
o=p.parse_args();s=o.source.resolve()
(s/'fcstdint.h').write_bytes((s/'src/fcstdint.h.in').read_bytes())
(s/'fontconfig/fontconfig.h').write_text((s/'fontconfig/fontconfig.h.in').read_text().replace('@CACHE_VERSION@','9'))
subprocess.run(['python3',str(s/'fc-case/fc-case.py'),str(s/'fc-case/CaseFolding.txt'),'--template',str(s/'fc-case/fccase.tmpl.h'),'--output',str(s/'fc-case/fccase.h')],check=True)
names=re.findall(r"'([^']+\.orth)'",(s/'fc-lang/meson.build').read_text())
assert names
subprocess.run(['python3',str(s/'fc-lang/fc-lang.py'),*names,'--template',str(s/'fc-lang/fclang.tmpl.h'),'--output',str(s/'fc-lang/fclang.h'),'--directory',str(s/'fc-lang')],check=True)
for stem,headers in [('fc',['fontconfig/fontconfig.h','src/fcdeprecate.h','fontconfig/fcprivate.h']),('fcft',['fontconfig/fcfreetype.h'])]:
 subprocess.run(['python3',str(s/'src/makealias.py'),str(o.original_source.resolve()/'src'),str(s/(stem+'alias.h')),str(s/(stem+'aliastail.h')),*[str(s/h) for h in headers]],check=True)
 # Omitted directory/cache APIs have no implementation or alias in this profile.
 defined=set()
 for c in s.glob('src/*.c'):defined.update(re.findall(r'^\b(Fc\w+)\s*\(',c.read_text(),re.M))
 q=s/(stem+'aliastail.h');q.write_text('\n'.join(l for l in q.read_text().splitlines() if 'alias(' not in l or re.search(r'IA__(Fc\w+)',l)[1] in defined)+'\n')
pre=subprocess.check_output([o.host_cc,'-E','-P','-x','c','-I'+str(s),'-I'+str(s/'src'),str(s/'src/fcobjshash.gperf.h')]);(s/'src/fcobjshash-pre.h').write_bytes(pre)
subprocess.run(['python3',str(s/'src/cutout.py'),str(s/'src/fcobjshash-pre.h'),str(s/'src/fcobjshash.gperf')],check=True)
subprocess.run([o.gperf,'--pic','-m','100',str(s/'src/fcobjshash.gperf'),'--output-file',str(s/'src/fcobjshash.h')],check=True)
