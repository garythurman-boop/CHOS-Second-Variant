#!/usr/bin/env python3
"""Report real CHOS-target compiler/runtime prerequisites; nonzero means incomplete.
This preflight does not build an engine or count as browser conformance evidence.
"""
import argparse,json,pathlib,subprocess,tempfile
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=pathlib.Path,required=True);ap.add_argument('--compiler',default='clang++');o=ap.parse_args()
units={
 'freestanding_cxx20':'struct A { virtual ~A(){}; }; extern "C" int probe(){ return sizeof(A); }',
 'cxx_standard_runtime':'#include <thread>\n#include <mutex>\n#include <condition_variable>\n#include <memory>\nint probe(){return sizeof(std::mutex);}',
 'native_pthread_subset':'#include <pthread.h>\nint probe(){pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;return pthread_mutex_lock(&m)||pthread_mutex_unlock(&m);}',
 'compiler_emulated_tls':'thread_local int value=7;int probe(){return ++value;}',
 'virtual_memory':'#include <sys/mman.h>\nint probe(){return MAP_ANONYMOUS;}',
 'file_descriptor_poll':'#include <poll.h>\nint probe(){return sizeof(struct pollfd);}',
 'wpe_embed_api':'#include <wpe/wpe.h>\nint probe(){return 0;}',
}
results=[]
with tempfile.TemporaryDirectory(prefix='chos-engine-probe-') as d:
 for name,text in units.items():
  source=pathlib.Path(d)/(name+'.cc');source.write_text(text)
  command=[o.compiler,'--target=aarch64-none-elf','-ffreestanding','-std=c++20','-fno-exceptions','-fno-rtti','-Iuserland/libc/include','-Iuserland/libchos/include','-Iinclude','-c',str(source),'-o',str(pathlib.Path(d)/(name+'.o'))]
  if name=='compiler_emulated_tls':command.append('-femulated-tls')
  r=subprocess.run(command,capture_output=True,text=True,timeout=30)
  results.append({'capability':name,'available':r.returncode==0,'diagnostic':r.stderr[:1200],'command':command})
missing=[r['capability'] for r in results if not r['available']]
report={'stage3_complete':False,'modern_browser_integrated':False,'scope':'CHOS-target compile-only prerequisite preflight; availability does not prove link/runtime/full POSIX support','missing':missing,'probes':results}
o.output.parent.mkdir(parents=True,exist_ok=True);o.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'missing':missing,'modern_browser_integrated':False},indent=2));raise SystemExit(1 if missing else 0)
