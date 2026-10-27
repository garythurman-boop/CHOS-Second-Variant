#!/usr/bin/env python3
"""Legacy profiles must reject global initializers rather than silently skip them."""
import argparse,pathlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--cc',default='clang');p.add_argument('--ld',default='ld.lld');o=p.parse_args()
with tempfile.TemporaryDirectory(prefix='chos-static-link-') as d:
 d=pathlib.Path(d);s=d/'init.c';obj=d/'init.o'
 s.write_text('volatile unsigned initialized;\n__attribute__((constructor)) static void init(void){initialized=1;}\nint main(void){return initialized==1?0:1;}\n')
 subprocess.run([o.cc,'--target=aarch64-none-elf','-ffreestanding','-fno-stack-protector','-fno-pic','-c',str(s),'-o',str(obj)],check=True)
 for script in ['userland/user.ld','userland/native.ld','userland/tls/native.ld']:
  r=subprocess.run([o.ld,'-T',script,'-nostdlib','-static','-z','max-page-size=4096','-o',str(d/'rejected.elf'),'build/userland/start.o','build/userland/native_abi.o',str(obj)],capture_output=True,text=True)
  assert r.returncode!=0 and 'Global constructors require the dense native static-startup profile' in r.stderr,(script,r.stderr)
print('Legacy CHCX/sparse native profiles reject constructor arrays explicitly')
