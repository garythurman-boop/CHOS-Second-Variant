#!/usr/bin/env python3
"""Launch the embedded native I/O qualification via actual desktop USB input."""
from pathlib import Path
import argparse,sys,time,json,shutil
sys.path.insert(0,'tests')
from qemu_platform import Guest,wait
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',type=Path,required=True);options=parser.parse_args()
out=options.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
a=argparse.Namespace(mode='uefi',kernel=Path('build/chos.elf'),uefi_kernel=Path('build/uefi-kernel/chos.elf'),iso=Path('build/chos-0.30.0.iso'),memory='2G',qemu_data=Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
try:
 wait(lambda:'chos> ' in g.text(),45,'UEFI runtime');wait(lambda:g.symbol('splash_active')==0,10,'desktop')
 g.hmp('sendkey c');wait(lambda:g.symbol('view')==1,5,'desktop Console')
 g.type('run /bin/stdio.elf')
 wait(lambda:'boundaries passed' in g.terminal() or '[FAIL]' in g.terminal(),30,'visible runtime qualification')
 assert '[FAIL]' not in g.terminal(),g.terminal()
 g.screen('runtime-passed',full=True);(out/'console.txt').write_text(g.terminal())
 def quiescent():
  before=g.text().count('CHOS process table');g.serial('procinfo')
  wait(lambda:g.text().count('CHOS process table')>before,3,'process diagnostic')
  return 'Active: 0;' in g.text().rsplit('CHOS process table',1)[-1]
 wait(quiescent,8,'runtime process cleanup')
 (out/'result.json').write_text(json.dumps({'status':'PASS','checks':['actual USB Console command launches embedded native ELF','completed diagnostic output visible in desktop Console','three separate processes exit and clean up','screenshot retained'],'limits':['QEMU UEFI e1000e,not Fusion validation']},indent=2)+'\n')
except Exception:
 (out/'failure.txt').write_text(g.text()+'\n'+g.terminal());raise
finally:g.close()
