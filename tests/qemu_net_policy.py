#!/usr/bin/env python3
"""Real EL0 network status and profile-write boundary in a disposable direct guest."""
import argparse,pathlib,shutil,subprocess,sys,json,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=pathlib.Path,required=True);o=ap.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/NETPOLICY.CHX','build/userland/network_policy_test.chcx'],check=True)
a=argparse.Namespace(mode='direct',kernel=pathlib.Path('build/chos.elf'),memory='512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,no_rng=False)
application_sha256=hashlib.sha256(pathlib.Path('build/userland/network_policy_test.chcx').read_bytes()).hexdigest()
g=Guest(a,out,disk)
try:
 wait(lambda:'chos> ' in g.text(),15,'direct shell')
 g.serial('exec /BIN/NETPOLICY.CHX')
 wait(lambda:'[ OK ] EL0 read-only network status and protected profile passed' in g.text(),8,'real EL0 status/syscall access checks')
 (out/'result.json').write_text(json.dumps({'status':'PASS','application_sha256':application_sha256,'checks':['actual EL0 read-only network status ABI','null/readonly/short status buffers rejected','ordinary process cannot write, unlink, mkdir, rmdir or rename service profile/parent']},indent=2))
finally:g.close()
