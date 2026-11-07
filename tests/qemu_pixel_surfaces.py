#!/usr/bin/env python3
"""Actual EL0 pixel uploads,legacy event ABI,physical input and engine coexistence."""
import argparse,pathlib,json,shutil,subprocess,hashlib,re,time,struct
from qemu_platform import Guest,wait
from guest_user_probe import GuestUserProbe
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct')
p.add_argument('--engine',action='store_true');p.add_argument('--javascript',type=pathlib.Path,help='Explicit actual JSC pixel ELF embedded in diagnostic images');p.add_argument('--image-root',type=pathlib.Path);p.add_argument('--runs',type=int,choices=[1,3],default=1)
p.add_argument('--legacy',type=pathlib.Path,required=True,help='Exact published a89db07 GUI CHCX,not a recompile')
o=p.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
legacy_sha=hashlib.sha256(o.legacy.read_bytes()).hexdigest();assert legacy_sha=='2feae4321604c0ab6e9f2b3ef1082a5b41dcf4c136be47b28824a0bd08dd0133'
for name,source in [('/BIN/PIXELS.CHX',pathlib.Path('build/userland/pixel_demo.chcx')),('/BIN/LEGACY.CHX',o.legacy)]:
 subprocess.run(['python3','tools/mkchfs.py','put',str(disk),name,str(source)],check=True)
assert not(o.engine and o.javascript),'Exclusive engine lease: choose coexistence OR the JavaScript pixel client'
engine_image=bool(o.engine or o.javascript)
image=str(o.image_root or pathlib.Path('build/engine-test' if engine_image else 'build'))
title='CHOS JavaScript Pixel Surface' if o.javascript else 'CHOS Native Pixel Surface'
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path(image+'/chos.elf'),uefi_kernel=pathlib.Path(image+'/uefi-kernel.elf' if engine_image else image+'/uefi-kernel/chos.elf'),iso=pathlib.Path(image+'/chos.iso' if engine_image else image+'/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk);probe=GuestUserProbe(g);app=o.javascript or pathlib.Path('build/userland/pixel_demo.elf');probe.app_symbols(app)
checks=[];runs=[]
def key(code):g.hmp('sendkey '+code+' 50');time.sleep(.08)
def active(title):
 c=probe.foreground();return c if c and c['title'].startswith(title) else None
def process_snapshot():
 header='CHOS process table (foreground and scheduled EL0 processes)';before=g.text().count(header);g.serial('procinfo')
 wait(lambda:g.text().count(header)>before,4,'process report');return g.text().rsplit(header,1)[-1]
def cold_pages():
 deadline=time.monotonic()+15
 while True:
  section=process_snapshot()
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  assert time.monotonic()<deadline,'process cleanup did not quiesce'
 count=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>count,4,'memory report')
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
def scalar(name):
 size=probe.symbol_sizes[name];assert size in [1,2,4,8]
 return int.from_bytes(g.block(name,size=size),'little')
def no_pins():
 stride,m=probe.layout('gui_surface_store_t');frame_stride,f=probe.layout('gui_surface_frame_t')
 assert probe.symbol_sizes['gui_app_surfaces']==stride*4
 def quiescent():
  for i in range(4):
   for member in ['front','pending']:
    if int.from_bytes(g.block('gui_app_surfaces',i*stride+m[member]+f['id'],size=8),'little'):return False
   if g.block('gui_app_surfaces',i*stride+m['readers'],size=8)!=bytes(8):return False
  return True
 wait(quiescent,4,'visible,pending and compositor-pin lifetime cleanup')
def screenshot_pixels(name,epoch,pid):
 def capture():
  g.qmp('stop')
  try:
   if g.symbol('presented_generation')!=g.symbol('dirty_generation'):return False
   c=active(title)
   if not c or c['pid']!=pid or probe.value(pid,app,'frames_submitted')!=epoch:return False
   w=g.window();g.screen(name,full=True);raw=(out/(name+'.ppm')).read_bytes().split(b'\n',3)
   width,height=map(int,raw[1].split());pixels=raw[3];assert len(pixels)==width*height*3
   # Recover the actual GUI scale using the same geometry information,without
   # assuming that a high-resolution output has the same logical pixel size.
   scale=3 if width>=2400 and height>=1530 else 2 if width>=1600 and height>=1024 else 1;points=[]
   for x,y in [(0,0),(127,1),(128,17),(255,99),(511,255),(16,160),(500,100)]:
    px=w['x']+(18+x)*scale;py=w['y']+(98+y)*scale
    expected=bytes([(x+epoch*31)&255,(y+epoch*17)&255,0xe0 if ((x//16)^(y//16))&1 else 0x20])
    actual=pixels[(py*width+px)*3:(py*width+px)*3+3]
    assert actual==expected,{'point':[x,y],'screen':[px,py],'epoch':epoch,'actual':actual.hex(),'expected':expected.hex(),'window':w,'scale':scale}
    points.append({'source':[x,y],'screen':[px,py],'RGB':actual.hex()})
   (out/(name+'-samples.json')).write_text(json.dumps(points,indent=2)+'\n');return True
  finally:g.qmp('cont')
 wait(capture,6,'presented actual framebuffer colors')
def legacy_pointer_note(pid):
 stride,m=probe.layout('gui_app_client_t');opstride,op=probe.layout('gui_app_draw_op_t')
 for off in range(0,probe.symbol_sizes['gui_app_clients'],stride):
  if int.from_bytes(g.block('gui_app_clients',off+m['owner_pid'],size=4),'little')==pid:
   data=g.block('gui_app_clients',off+m['ops']+2*opstride+op['text'],size=128)
   return data.split(b'\0',1)[0].startswith(b'Pointer input was delivered')
 return False
try:
 if o.javascript:
  stage=json.loads(pathlib.Path(image+'/native-engine.json').read_text())
  assert stage['source_sha256']==hashlib.sha256(app.read_bytes()).hexdigest()
  raw=(out/'kernel.elf').read_bytes();start=g.symbols['chos_romfs_engine_start'];end=g.symbols['chos_romfs_engine_end'];ph=struct.unpack_from('<Q',raw,32)[0];embedded=None
  for i in range(struct.unpack_from('<H',raw,56)[0]):
   kind,flags,f,va,pa,n,memory,alignment=struct.unpack_from('<IIQQQQQQ',raw,ph+i*56)
   if kind==1 and va<=start<end<=va+n:embedded=raw[f+start-va:f+end-va]
  assert embedded is not None and hashlib.sha256(embedded).hexdigest()==stage['staged_sha256'],'Wrong embedded JavaScript pixel executable'
 wait(lambda:'chos> ' in g.text(),60,'scheduled runtime');wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=cold_pages();no_pins()
 g.serial('spawn /BIN/LEGACY.CHX');wait(lambda:active('CHOS User-Space GUI Demo'),8,'original published GUI application')
 legacy_pid=active('CHOS User-Space GUI Demo')['pid'];switches=g.symbol('preemption_switches')
 wait(lambda:g.symbol('preemption_switches')>=switches+12,4,'legacy event polling remains live')
 key('a');assert active('CHOS User-Space GUI Demo')
 w=g.window();px=w['x']+60;py=w['y']+110;screen=out/'legacy-before.ppm';g.screen('legacy-before',full=True);dims=screen.read_bytes().split(b'\n',3)[1];width,height=map(int,dims.split())
 if o.mode=='direct':
  # The real input driver clamps each relative report to 160 pixels. Move in
  # bounded steps and observe delivered coordinates before sending the click.
  for step in range(24):
   mx,my=scalar('mouse_x'),scalar('mouse_y')
   if (mx,my)==(px,py):break
   dx=max(-100,min(100,px-mx));dy=max(-100,min(100,py-my))
   g.qmp('input-send-event',events=[{'type':'rel','data':{'axis':'x','value':dx}},{'type':'rel','data':{'axis':'y','value':dy}}])
   wait(lambda:(scalar('mouse_x'),scalar('mouse_y'))==(mx+dx,my+dy),2,'relative pointer report delivery')
  assert (scalar('mouse_x'),scalar('mouse_y'))==(px,py)
 else:g.qmp('input-send-event',events=[{'type':'abs','data':{'axis':'x','value':px*32767//(width-1)}},{'type':'abs','data':{'axis':'y','value':py*32767//(height-1)}}])
 time.sleep(.1)
 (out/'legacy-pointer.json').write_text(json.dumps({'target':[px,py],'actual':[scalar('mouse_x'),scalar('mouse_y')],'window':w},indent=2)+'\n')
 g.qmp('input-send-event',events=[{'type':'btn','data':{'down':True,'button':'left'}}]);time.sleep(.1)
 g.qmp('input-send-event',events=[{'type':'btn','data':{'down':False,'button':'left'}}])
 wait(lambda:legacy_pointer_note(legacy_pid),4,'legacy184-byte event delivers a real pointer button')
 g.screen('legacy-pointer-after');key('esc');wait(lambda:not probe.clients(),6,'legacy application closes after actual Escape')
 assert cold_pages()==cold;checks.append('Exact original a89db07 CHCX survives keyboard/pointer polling,changes its real retained text and closes normally with unchanged184-byte event ABI')
 for cycle in range(o.runs):
  start=len(g.text());g.serial('exec /BIN/JSCVM.ELF' if o.javascript else 'spawn /BIN/PIXELS.CHX')
  def ready():
   text=g.text()[start:];assert '[FAIL]' not in text and '[USER FAULT]' not in text,text
   if o.javascript:return '[ OK ] actual JavaScript typed-array pixel presentation passed' in text and active(title)
   return all(s in text for s in ['[ OK ] native pixel copy guard and complete-frame presentation passed','[ OK ] native pixel foreign-window refusal passed','[ OK ] native pixel GUI permission refusal passed']) and active(title)
  wait(ready,60 if o.javascript else 20,'native guarded copy,foreign owner,denied GUI capability and first full image')
  pid=active(title)['pid'];screenshot_pixels('pixels-'+str(cycle)+'-frame1',1,pid)
  before=time.monotonic();key('spc');wait(lambda:probe.value(pid,app,'frames_submitted')==2,4,'physical Space submits another image');response=time.monotonic()-before
  screenshot_pixels('pixels-'+str(cycle)+'-frame2',2,pid)
  engine_response=None
  if o.engine:
   engine_start=len(g.text());g.serial('exec /BIN/JSCVM.ELF mixed')
   wait(lambda:'[INFO] actual native JSC long script begins' in g.text()[engine_start:],40,'actual JSC infinite script active alongside pixel client')
   before=time.monotonic();key('spc');wait(lambda:probe.value(pid,app,'frames_submitted')==3,2,'physical Space accepted and complete image submitted during real long script')
   engine_response=time.monotonic()-before
   assert '[ OK ] actual native Wasm,async main loop and CPU watchdog coexist' not in g.text()[engine_start:],'script must still be active during image submission'
   screenshot_pixels('pixels-'+str(cycle)+'-during-JSC',3,pid)
   wait(lambda:'[ OK ] actual native Wasm,async main loop and CPU watchdog coexist' in g.text()[engine_start:],120,'actual JSC mixed suite completes')
   assert not any(s in g.text()[engine_start:] for s in ['[FAIL]','[USER FAULT]','[FATAL]'])
  key('esc');wait(lambda:not probe.clients(),6,'pixel window closes');assert cold_pages()==cold;no_pins()
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  if o.javascript:assert '[ OK ] actual JavaScript pixel VM cleaned up' in g.text()[start:]
  assert not scalar('committed_frame_limits') and scalar('engine_owner_slot')==0xffffffff and not scalar('futex_queued_waiters')
  runs.append({'cycle':cycle,'physical_image_submission_seconds':response,'submission_during_JSC_seconds':engine_response,'cold_pages_restored':cold})
 if not o.javascript:checks+=['Real full552-byte submit envelope,owned frame IDs,all tiles required,stale present refused','Mapped header/first pixel followed by unmapped guard returns EFAULT;one-pixel present stays incomplete,no partial image effects','Default capabilities cannot target another process window;reduced GUI rights deny before submission','Exact rendered RGB samples across row/tile boundaries,second image and stable front-buffer presentation','Close/reuse clears visible and pending IDs/pins;exact physical/commit/engine-lease/futex baseline']
 if o.javascript:checks+=['Actual original JavaScriptCore computes every RGB value into its real Uint32Array;no native pixel-generation fallback','Typed-array backing remains protected through bounded copied uploads;original engine GC occurs after every image','Physical Space triggers another real JS evaluation,image upload and framebuffer sample;close restores exact ownership/pin/commit/engine/futex baseline']
 if o.engine:checks.append('Actual checked Wasm,async RunLoop and five JSC watchdog cases complete while another EL0 client receives physical input and uploads a complete image')
 assert all(v not in g.text() for v in ['[USER FAULT]','[FATAL]','[FAIL]'])
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'engine_image':engine_image,'JavaScript_pixel_client':bool(o.javascript),'runs':runs,'legacy_chcx_sha256':legacy_sha,'pixel_application_sha256':hashlib.sha256(app.read_bytes()).hexdigest(),'cold_free_pages':cold,'checks':checks,'limits':['Pixel service and real engine coexistence only;not WebCore/browser rendering','Fixed24MiB static image store,1024x768 XRGB ceiling,logical desktop scaling;no shared mappings/GPU acceleration','QEMU only;new Fusion/Pi5/laptop acceptance NOT RUN;Stage3 incomplete,version0.30.0']},indent=2)+'\n')
except Exception as error:
 try:g.serial('procinfo');g.serial('taskinfo');g.serial('meminfo');time.sleep(.3);(out/'failure-registers.txt').write_text(g.hmp('info registers'));g.screen('failure')
 except Exception:pass
 (out/'result.json').write_text(json.dumps({'status':'FAIL','error':str(error),'qualified':False},indent=2)+'\n');raise
finally:g.close()
