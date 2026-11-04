#!/usr/bin/env python3
"""Launch actual native Wasm through physical GUI Console input,including argv."""
import argparse,pathlib,shutil,time,re,json,hashlib,subprocess,struct
from qemu_platform import Guest,wait
from guest_user_probe import GuestUserProbe
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
# CHFS applications precede ROMFS. Pin the current Console in this disposable
# copy rather than accidentally exercising an older test-disk application.
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/CONSOLE.CHX','build/userland/console.chcx'],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/engine-test/chos.elf'),uefi_kernel=pathlib.Path('build/engine-test/uefi-kernel.elf'),iso=pathlib.Path('build/engine-test/chos.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk);probe=GuestUserProbe(g);console=pathlib.Path('build/userland/console.elf')
marker='[ OK ] actual checked native Wasm arithmetic,memory growth,tables and bounds traps executed'
def pages():
 header='CHOS process table (foreground and scheduled EL0 processes)';end=time.monotonic()+15
 while True:
  before=g.text().count(header);g.serial('procinfo');wait(lambda:g.text().count(header)>before,4,'process table')
  if re.search(r'Active: 0;.*pending cleanup: 0',g.text().rsplit(header,1)[-1]):break
  assert time.monotonic()<end,'Console/engine cleanup did not quiesce'
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo');wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,4,'free pages')
 assert g.symbol('committed_frame_limits')==0 and g.symbol('engine_owner_slot')==0xffffffff
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
def key(code):g.hmp('sendkey '+code+' 50');time.sleep(.08)
def rows_text(raw,width):
 return b''.join(raw[at:at+width].split(b'\0',1)[0] for at in range(0,len(raw),width)).decode('ascii',errors='replace')
try:
 wait(lambda:'chos> ' in g.text(),60,'scheduled runtime');wait(lambda:g.symbol('splash_active')==0,10,'splash');cold=pages()
 key('c');parent=None
 if o.mode=='uefi':wait(lambda:g.symbol('view')==1,6,'trusted GUI Console')
 else:
  wait(lambda:(client:=probe.foreground()) and client['title'].startswith('CHOS Console'),6,'isolated GUI Console')
  parent=probe.foreground()['pid'];probe.app_symbols(console)
  # Independently verify the actual loaded executable text through owned EL0
  # page tables;symbol-file freshness alone cannot establish this identity.
  raw=console.read_bytes();ph=struct.unpack_from('<Q',raw,32)[0];matched=0
  for i in range(struct.unpack_from('<H',raw,56)[0]):
   kind,flags,offset,va,pa,n,memory,alignment=struct.unpack_from('<IIQQQQQQ',raw,ph+i*56)
   if kind==1 and flags&1 and n:
    address=va if va>=0x20000000 else va+0x20000000
    assert probe.read(parent,address,n)==raw[offset:offset+n],'Loaded Console text differs from current build'
    matched+=1
  assert matched
 def output():
  return rows_text(probe.block(parent,console,'output_lines',11*73),73) if parent else rows_text(g.block('terminal_lines',size=69*29),69)
 for character in 'run /bin/jscvm.elf wasm':key({'/':'slash','.':'dot',' ':'spc'}.get(character,character))
 key('ret');started=time.monotonic()
 def completed():
  text=output();assert 'Launch failed:' not in text and 'run: launch failed' not in text,text
  return marker in text
 wait(completed,60,'actual native Wasm stdout in GUI Console')
 g.screen('native-wasm-from-gui');header='CHOS process table (foreground and scheduled EL0 processes)'
 def child_done():
  before=g.text().count(header);g.serial('procinfo');wait(lambda:g.text().count(header)>before,4,'native child status')
  section=g.text().rsplit(header,1)[-1]
  return re.search(r'pid=\d+ name=jscvm\.elf state=exited[^\r\n]*exit=0[^\r\n]*cleanup=complete',section,re.I)
 wait(child_done,15,'native child normal exit and terminal cleanup')
 elapsed=time.monotonic()-started
 key('esc')
 if parent:wait(lambda:all(c['pid']!=parent for c in probe.clients()),6,'isolated Console closes')
 else:wait(lambda:g.symbol('view')==0,4,'trusted Console closes')
 assert pages()==cold and g.symbol('futex_queued_waiters')==0
 assert '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text()
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'console_frontend':'isolated EL0' if parent else 'trusted UEFI','physical_command':'run /bin/jscvm.elf wasm','console_parent_pid':parent,'launch_to_visible_output_seconds':elapsed,'cold_free_pages':cold,'final_free_pages':pages(),'application_sha256':hashlib.sha256(pathlib.Path('build/engine-test/native-engine.elf').read_bytes()).hexdigest(),'checks':['Actual emulated keyboard input;no GUI/API injection','Path and argv reach the real native Wasm executable;its positive stdout is visible in Console','Child normal exit,Console close and exact cold physical/commit/engine-pool/futex baseline'],'limits':['Diagnostic engine fixture only;not browser integration or new Fusion qualification']},indent=2)+'\n')
except Exception as error:
 try:g.serial('procinfo');g.serial('taskinfo');time.sleep(.3);(out/'failure-registers.txt').write_text(g.hmp('info registers'));g.screen('failure')
 except Exception:pass
 (out/'failure.txt').write_text(g.text());(out/'result.json').write_text(json.dumps({'status':'FAIL','error':str(error)},indent=2)+'\n');raise
finally:g.close()
