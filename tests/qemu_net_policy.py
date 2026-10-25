#!/usr/bin/env python3
"""Real EL0 network status and profile-write boundary in a disposable direct guest."""
import argparse,pathlib,shutil,subprocess,sys,json,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=pathlib.Path,required=True);ap.add_argument('--ipv6',action='store_true');o=ap.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/NETPOLICY.CHX','build/userland/network_policy_test.chcx'],check=True)
a=argparse.Namespace(mode='direct',kernel=pathlib.Path('build/chos.elf'),memory='512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,no_rng=False)
application_sha256=hashlib.sha256(pathlib.Path('build/userland/network_policy_test.chcx').read_bytes()).hexdigest()
g=Guest(a,out,disk)
try:
 wait(lambda:'chos> ' in g.text(),15,'direct shell')
 g.serial('exec /BIN/NETPOLICY.CHX'+(' ipv6' if o.ipv6 else ''))
 wait(lambda:'[ OK ] EL0 read-only network status and protected profile passed' in g.text(),18 if o.ipv6 else 8,'real EL0 status/syscall access checks')
 if o.ipv6:assert '[ OK ] EL0 IPv6 status and actual router echo passed' in g.text()
 (out/'result.json').write_text(json.dumps({'status':'PASS','application_sha256':application_sha256,'ipv6_live_echo':'PASS' if o.ipv6 else 'NOT RUN','checks':['actual EL0 IPv4/IPv6 read-only network status ABIs','null/readonly/short status buffers and invalid IPv6 request rejected','ordinary process cannot write, unlink, mkdir, rmdir or rename service profile/parent']},indent=2))
finally:g.close()
