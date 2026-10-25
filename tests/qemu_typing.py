#!/usr/bin/env python3
"""Hold/release actual emulated keys and inspect visible carets; disposable guests."""
import argparse, json, pathlib, shutil, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from qemu_platform import Guest, wait
import mkchfs

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out', required=True, type=pathlib.Path)
ap.add_argument('--mode', choices=['direct','uefi'], default='uefi')
o=ap.parse_args(); out=o.out.resolve(); out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
document=out/'input.txt';document.write_text('abcdefghijklmnopqrstuvwxyz0123456789')
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/REPEAT.TXT',str(document)],check=True)
entries=[e for e in mkchfs.active_dir(mkchfs.load_image(disk))[2] if e]
index=next(i for i,e in enumerate(entries) if e['path']=='/REPEAT.TXT')
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='virtio',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
def key(k):g.hmp('sendkey '+k+' 10');time.sleep(.04)
def state(k,down):g.qmp('input-send-event',events=[{'type':'key','data':{'down':down,'key':{'type':'qcode','data':k}}}])
def text(name,size=128):return g.block(name,size=size).split(b'\0')[0]
def blink(label):
 wait(lambda:g.symbol('text_caret_visible')==1,2,label+' caret on')
 time.sleep(.06) # state precedes presentation; let the guest finish that frame
 on=g.screen(label+'-on')
 wait(lambda:g.symbol('text_caret_visible')==0,2,label+' caret off')
 time.sleep(.06)
 off=g.screen(label+'-off')
 assert on!=off,label+' must visibly blink'
def deletion(name,size=128):
 before=len(text(name,size));assert before>12
 state('backspace',True);time.sleep(.25)
 early=len(text(name,size));assert early==before-1,('repeat before initial delay',before,early)
 time.sleep(.65);state('backspace',False);time.sleep(.1)
 after=len(text(name,size));assert after<early-4,(before,early,after)
 time.sleep(.25);assert len(text(name,size))==after,'release must stop deletion'
 return before,after
checks=[]
try:
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text() if o.mode=='uefi' else 'chos> ' in g.text(),45,'desktop')
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 key('c');wait(lambda:g.symbol('view')==1,4,'Console')
 for c in 'abcdefghijklmnopqrstuvwxyz':key(c)
 console_counts=deletion('terminal_command');blink('console')
 state('a',True);time.sleep(.75);state('a',False);time.sleep(.1)
 assert text('terminal_command').endswith(b'aaa'),'printable keys repeat too'
 checks+=['Console 500ms delay then repeated Backspace and letters; key-up stops', 'Console visible blinking caret']
 browser_counts=None
 if o.mode=='uefi':
  key('ctrl-c');g.type('run /bin/browser.chx')
  wait(lambda:g.symbol('kernel_browser_active')==1,5,'browser')
  for c in 'abcdefghijklmnopqrstuvwxyz0123456789':key(c)
  browser_counts=deletion('address',512);blink('browser')
  checks+=['Browser address bar held Backspace and visible blinking caret']
  key('esc');key('c')
 # Changing focus cancels a held key instead of applying it to the new document.
 state('backspace',True);time.sleep(.05);key('esc');key('f')
 wait(lambda:g.symbol('view')==2,4,'File Manager')
 for _ in range(index):key('down')
 key('ret');wait(lambda:g.symbol('view')==6 and g.symbol('viewer_editable')==1,4,'editable text document')
 time.sleep(.65);assert text('viewer_content',1024)==document.read_bytes(),'focus change must cancel held delete'
 state('backspace',False)
 document_counts=deletion('viewer_content',1024);blink('document')
 key('ret');key('a');blink('document-newline')
 checks+=['Text document held Backspace; no deletion across focus change', 'Text caret follows append/newline and visibly blinks']
 if o.mode=='uefi':
  # Disconnect must synthesize key-up and stop typematic without a physical release.
  state('backspace',True);time.sleep(.6)
  g.qmp('device_del',id='kbd');time.sleep(.5)
  stopped=text('viewer_content',1024);time.sleep(.6);assert text('viewer_content',1024)==stopped
  checks+=['USB keyboard removal releases held key']
 g.screen('final')
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'console_lengths':console_counts,'document_lengths':document_counts,'browser_lengths':browser_counts,'checks':checks},indent=2))
except Exception:
 if g.process.poll() is None:g.screen('failure')
 raise
finally:g.close()
