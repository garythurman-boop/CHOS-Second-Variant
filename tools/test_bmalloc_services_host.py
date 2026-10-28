#!/usr/bin/env python3
"""Host transport/bounds model for actual patched sources, not hardware entropy."""
import argparse,pathlib,json,hashlib,subprocess,time
root=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default='g++');p.add_argument('--source',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);o=p.parse_args();source=o.source.resolve();out=o.out.resolve()
m=json.loads((root/'docs/dependencies/webkit-jsc-bringup.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=source)
assert git('rev-parse','HEAD').decode().strip()==m['commit'] and not git('diff','--cached','--name-only').strip()
assert set(git('diff','--name-only').decode().splitlines())==set(m['patched_files_sha256'])
for name,wanted in m['patched_files_sha256'].items():assert sha(source/name)==wanted
assert sha(root/m['patch'])==m['patch_sha256']
out.mkdir(parents=True,exist_ok=False);base=source/'Source/bmalloc/bmalloc';records=[]
for kind in ('normal','sanitize'):
 command=[o.compiler,'-std=c++23','-D__CHOS__=1','-DUSE_SYSTEM_MALLOC=1','-DNDEBUG=1','-I'+str(root/'tests/bmalloc_services_host'),'-I'+str(root/'include'),'-I'+str(source/'Source/bmalloc'),'-pthread','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-expansion-to-defined',str(root/'tests/bmalloc_services_host/main.cpp'),str(base/'CryptoRandom.cpp'),str(base/'Logging.cpp'),str(base/'Mutex.cpp'),'-o',str(out/kind)]
 if kind=='sanitize':command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 start=time.monotonic()
 with (out/(kind+'.log')).open('wb') as f:
  r=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT)
  if not r.returncode:r=subprocess.run([str(out/kind)],stdout=f,stderr=subprocess.STDOUT,timeout=30)
 records.append({'name':kind,'exit':r.returncode,'seconds':time.monotonic()-start,'command':command,'log_sha256':sha(out/(kind+'.log'))})
 if r.returncode:break
report={'status':'PASS' if len(records)==2 and all(not r['exit'] for r in records) else 'FAIL','stage3_complete':False,'upstream_commit':m['commit'],'patch_sha256':m['patch_sha256'],'runs':records,'scope':'actual ported source with modeled syscall transports and host formatter;partial writes,guard bytes,retry boundary,log truncation/errors,real host-thread mutex contention','limits':['not native hardware entropy or a cryptographic test;native formatter policy and fatal paths require guest checks','no browser,JavaScript or full POSIX/engine success']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(0 if report['status']=='PASS' else 1)
