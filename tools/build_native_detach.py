#!/usr/bin/env python3
"""Build native managed detach,TLS ordering and stack-ownership qualification."""
import argparse,pathlib,subprocess,json,hashlib,shutil
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native-sdk',type=pathlib.Path,required=True);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja');o=p.parse_args()
sdk=o.native_sdk.resolve();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
subprocess.run(['python3',str(root/'tools/verify_libcxx.py')],check=True)
subprocess.run(['python3',str(root/'tools/verify_musl_math.py')],check=True)
subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
commands=[[o.cmake,'-S',str(root/'tests/native_detach'),'-B',str(out/'cmake'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(pathlib.Path(shutil.which(o.ninja)).resolve()),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCMAKE_BUILD_TYPE=Release'],[o.cmake,'--build',str(out/'cmake'),'-j4']]
code=0
language_probes=[]
probe=out/'language-probe.cpp'
probe.write_text('#include <pthread.h>\n#include <thread>\n#include <chos/user.h>\nstatic_assert(CHOS_SYS_THREAD_DETACH==79);\nint probe(pthread_t t,std::thread& v){v.detach();return pthread_detach(t); }\n')
for mode in ['c++17','c++20','c++23']:
 cmd=['clang++','--target=aarch64-none-elf','--sysroot='+str(sdk),'-std='+mode,'-D__CHOS__=1','-fno-exceptions','-fno-rtti','-femulated-tls','-nostdinc++','-isystem',str(sdk/'include/c++/v1'),'-isystem',str(sdk/'include'),'-fsyntax-only','-Wall','-Wextra','-Werror',str(probe)]
 with (out/(mode+'.log')).open('wb') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 language_probes.append({'standard':mode,'command':cmd,'exit':r.returncode})
 code=r.returncode
 if code:break
for i,cmd in enumerate(commands):
 if code:break
 with (out/('configure.log' if not i else 'build.log')).open('wb') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 code=r.returncode
 if code:break
sha=lambda q:hashlib.sha256(q.read_bytes()).hexdigest()
report={'status':'BUILD_FAILED' if code else 'BUILT_NOT_EXECUTED','stage3_complete':False,'sdk_manifest_sha256':sha(sdk/'chos-sdk.json'),'commands':commands,'language_probes':language_probes,'returncode':code,'application_sha256':sha(out/'cmake/native_detach') if not code else None,'limits':['native managed detach/ownership component;not complete POSIX,foreign GC or browser completion','native guest qualification required;no hardware target claim']}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(code)
