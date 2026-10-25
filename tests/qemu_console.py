#!/usr/bin/env python3
"""Scroll real GUI Console history using hardware keys/wheel; disposable guests."""
import argparse,pathlib,shutil,sys,time,json
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',required=True,type=pathlib.Path);ap.add_argument('--mode',choices=['direct','uefi'],default='uefi');a=ap.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
args=argparse.Namespace(mode=a.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(args,out,disk)
def key(k):g.hmp('sendkey '+k);time.sleep(.15)
def screenshot(name):
 if g.symbol('terminal_scrollback',40)==0: # pointer, width, capacity, count, start, offset
  wait(lambda:g.symbol('text_caret_visible')==0,2,'caret off for deterministic history comparison')
 # Tablet wheel reports can relocate the cursor along the top edge; compare
 # the Console below that edge and above the taskbar clock.
 return g.screen(name)[800*50*3:]
def changed(x,y):return sum(a!=b for a,b in zip(x,y))>300
try:
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text() if a.mode=='uefi' else 'chos> ' in g.text(),45,'desktop')
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 key('c');time.sleep(.5)
 for _ in range(5):g.type('help');time.sleep(.15)
 live=screenshot('live')
 key('pgup');older=screenshot('page-up');assert changed(live,older)
 key('home');oldest=screenshot('oldest');assert changed(live,oldest)
 key('end');tail=screenshot('end');assert live==tail,'End must restore the live tail exactly'
 g.qmp('input-send-event',events=[{'type':'btn','data':{'button':'wheel-up','down':True}},{'type':'btn','data':{'button':'wheel-up','down':False}}]);time.sleep(.3)
 assert changed(live,screenshot('wheel-up'))
 key('end');assert screenshot('wheel-end')==live
 key('pgup');g.type('echo return-to-prompt');time.sleep(.3)
 typed=screenshot('typing-tail');key('end');assert typed==screenshot('typing-end'),'Typing must return to the prompt'
 g.type('clear');time.sleep(.3);cleared=screenshot('clear');key('pgup');assert cleared==screenshot('clear-page-up')
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':a.mode,'checks':['Page Up and Home reveal retained output','End returns to the live tail','hardware wheel scrolls Console','typing returns to prompt','clear removes retained history']},indent=2))
except Exception:
 g.screen('failure');raise
finally:g.close()
