#!/usr/bin/env python3
"""Actual WTF native adapters with modeled transport/errors, not real entropy."""
import argparse,pathlib,subprocess,json,time,hashlib
root=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--compiler',default='g++');o=p.parse_args();source=o.source.resolve();out=o.out.resolve()
m=json.loads((root/'docs/dependencies/webkit-jsc-bringup.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()==m['commit']
assert set(subprocess.check_output(['git','diff','--name-only'],cwd=source,text=True).splitlines())==set(m['patched_files_sha256'])
for name,wanted in m['patched_files_sha256'].items():assert sha(source/name)==wanted
assert sha(root/m['patch'])==m['patch_sha256']
out.mkdir(parents=True,exist_ok=False);records=[]
for kind in ('normal','sanitize'):
 command=[o.compiler,'-std=c++23','-O1','-g','-DNDEBUG=1','-D__CHOS__=1','-DBUILDING_JSCONLY__','-DUSE_SYSTEM_MALLOC=1','-Dgetpid=chos_test_getpid','-Dsysconf=chos_test_sysconf','-I'+str(root/'tests/wtf_entropy_host'),'-I'+str(root/'include'),'-I'+str(source/'Source/WTF'),'-I'+str(source/'Source/bmalloc'),'-Wall','-Wextra','-Werror','-Wno-expansion-to-defined','-Wno-misleading-indentation',str(root/'tests/wtf_entropy_host/main.cpp'),str(source/'Source/WTF/wtf/RandomDevice.cpp'),str(source/'Source/WTF/wtf/PageBlock.cpp'),'-o',str(out/kind)]
 if kind=='sanitize':command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 started=time.monotonic();runs=[]
 with (out/(kind+'.log')).open('wb') as f:
  c=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT);code=c.returncode
  if not code:
   for case in ['normal',*map(str,range(1,9))]:
    c=subprocess.run([str(out/kind),*([case] if case!='normal' else [])],stdout=f,stderr=subprocess.STDOUT,timeout=15)
    expected=0 if case=='normal' else 42;runs.append({'case':case,'exit':c.returncode,'expected':expected})
    if c.returncode!=expected:code=1;break
 records.append({'kind':kind,'command':command,'exit':code,'runs':runs,'seconds':time.monotonic()-started,'log_sha256':sha(out/(kind+'.log'))})
 if code:break
report={'status':'PASS' if len(records)==2 and all(not r['exit'] for r in records) else 'FAIL','stage3_complete':False,'upstream_commit':m['commit'],'patch_sha256':m['patch_sha256'],'records':records,'scope':'actual adapter source,partial chunks/canaries,32transient retry boundary,zero/oversized/error results,sleep failure,PID/page refusal','limits':['host-modeled broker/PID/page services;fatal handler only checks model counters','not kernel/hardware entropy quality or a cryptographic evaluation','native guest tests required;no full WTF/JSC/browser success']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(0 if report['status']=='PASS' else 1)
