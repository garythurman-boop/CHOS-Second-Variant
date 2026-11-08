#!/usr/bin/env python3
"""Actual EL0 font loading/shaping/raster output and AArch64 nonlocal recovery."""
import argparse,pathlib,hashlib,json,subprocess,shutil,re,time,struct
from qemu_platform import Guest,wait
from guest_user_probe import GuestUserProbe
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct')
p.add_argument('--image-root',type=pathlib.Path,default=pathlib.Path('build/engine-test'))
p.add_argument('--application',type=pathlib.Path,required=True);p.add_argument('--reference',type=pathlib.Path,required=True)
p.add_argument('--with-javascript',action='store_true');p.add_argument('--nvme',action='store_true')
p.add_argument('--embedded',action='store_true',help='Use explicitly selected compiled diagnostic assets; put no font files on the disk')
p.add_argument('--runs',type=int,choices=[1,3],default=3)
o=p.parse_args();assert not o.nvme or o.mode=='uefi';out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
fixtures=json.loads(pathlib.Path('docs/dependencies/text-fixtures.json').read_text())
for font in ([] if o.embedded else fixtures['fonts']):
 source=pathlib.Path('third_party/text-fixtures')/font['path'];assert hashlib.sha256(source.read_bytes()).hexdigest()==font['sha256']
 subprocess.run(['python3','tools/mkchfs.py','put',str(disk),font['guest_path'],str(source)],check=True)
jump=pathlib.Path('build/userland/setjmp_test.elf');subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/JMPS.ELF',str(jump)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=o.image_root/'chos.elf',uefi_kernel=o.image_root/'uefi-kernel.elf',iso=o.image_root/'chos.iso',memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=o.nvme,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
disk_before=hashlib.sha256(disk.read_bytes()).hexdigest()
g=Guest(a,out,disk);probe=GuestUserProbe(g);probe.app_symbols(o.application)
runs=[];jumpRuns=[]
def scalar(name):return int.from_bytes(g.block(name,size=probe.symbol_sizes[name]),'little')
def key(k):g.hmp('sendkey '+k+' 50');time.sleep(.08)
def cold():
 end=time.monotonic()+15;header='CHOS process table (foreground and scheduled EL0 processes)'
 while True:
  n=g.text().count(header);g.serial('procinfo');wait(lambda:g.text().count(header)>n,4,'process status')
  if re.search(r'Active: 0;.*pending cleanup: 0',g.text().rsplit(header,1)[-1]):break
  assert time.monotonic()<end,'process cleanup failed'
 n=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo');wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>n,4,'physical-page report')
 assert scalar('committed_frame_limits')==0 and scalar('engine_owner_slot')==0xffffffff and scalar('futex_queued_waiters')==0
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
def foreground():
 c=probe.foreground();return c if c and c['title']=='CHOS Native Shaped Text' else None
def frame(name,pid,epoch):
 expected=(o.reference/('frame'+str(epoch)+'.rgb')).read_bytes();assert len(expected)==512*256*3
 def capture():
  g.qmp('stop')
  try:
   if g.symbol('presented_generation')!=g.symbol('dirty_generation') or probe.value(pid,o.application,'frames_submitted')!=epoch:return False
   w=g.window();g.screen(name,full=True);raw=(out/(name+'.ppm')).read_bytes().split(b'\n',3);width,height=map(int,raw[1].split());screen=raw[3];assert len(screen)==width*height*3
   scale=3 if width>=2400 and height>=1530 else 2 if width>=1600 and height>=1024 else 1
   x=w['x']+18*scale;y=w['y']+98*scale
   pixels=bytes(screen[((y+row*scale)*width+x+col*scale)*3+ch] for row in range(256) for col in range(512) for ch in range(3))
   if pixels!=expected:
    (out/(name+'-actual.rgb')).write_bytes(pixels)
    mismatch=next(i for i,(a,b) in enumerate(zip(pixels,expected)) if a!=b)
    raise AssertionError({'pixel':[mismatch//3%512,mismatch//3//512],'actual':pixels[mismatch//3*3:mismatch//3*3+3].hex(),'expected':expected[mismatch//3*3:mismatch//3*3+3].hex(),'scale':scale,'window':w})
   info={'epoch':epoch,'actual_RGB_sha256':hashlib.sha256(pixels).hexdigest(),'host_reference_RGB_sha256':hashlib.sha256(expected).hexdigest(),'complete_pixels_compared':512*256,'native_frame_hash':hex(probe.value(pid,o.application,'text_frame_hash',size=8))}
   (out/(name+'.json')).write_text(json.dumps(info,indent=2)+'\n');return True
  finally:g.qmp('cont')
 wait(capture,8,'actual full shaped-text framebuffer equals independent host raster result')
try:
 stage=json.loads((o.image_root/'native-engine.json').read_text());assert stage['source_sha256']==hashlib.sha256(o.application.read_bytes()).hexdigest()
 raw=(out/'kernel.elf').read_bytes();start=g.symbols['chos_romfs_engine_start'];end=g.symbols['chos_romfs_engine_end'];ph=struct.unpack_from('<Q',raw,32)[0];embedded=None
 for i in range(struct.unpack_from('<H',raw,56)[0]):
  kind,flags,f,va,pa,n,memory,alignment=struct.unpack_from('<IIQQQQQQ',raw,ph+i*56)
  if kind==1 and va<=start<end<=va+n:embedded=raw[f+start-va:f+end-va]
 assert embedded is not None and hashlib.sha256(embedded).hexdigest()==stage['staged_sha256']
 wait(lambda:'chos> ' in g.text(),60,'scheduled runtime');wait(lambda:g.symbol('splash_active')==0,10,'splash')
 if o.nvme:
  # Diagnostic media do not carry an installed-boot NVMe declaration. Use the
  # real trusted read-only admission control,never inject a kernel API call.
  key('f1');wait(lambda:g.symbol('view')==4,4,'real Diagnostics')
  w=g.window();scale=max(1,w['width']//595);g.screen('nvme-before-probe',full=True)
  width,height=map(int,(out/'nvme-before-probe.ppm').read_bytes().split(b'\n',3)[1].split())
  x=w['x']+(610-180)*scale;y=w['y']+(475-70)*scale
  g.qmp('input-send-event',events=[{'type':'abs','data':{'axis':'x','value':round(x*32767/(width-1))}},{'type':'abs','data':{'axis':'y','value':round(y*32767/(height-1))}}]);time.sleep(.15)
  for down in [True,False]:g.qmp('input-send-event',events=[{'type':'btn','data':{'down':down,'button':'left'}}]);time.sleep(.15)
  wait(lambda:scalar('fs_ready')==1,12,'actual read-only NVMe CHFS mount')
  assert scalar('native_storage_writable')==0
  assert hashlib.sha256(disk.read_bytes()).hexdigest()==disk_before
  g.screen('nvme-read-only-mounted');key('esc');wait(lambda:g.symbol('view')!=4,4,'Diagnostics closes')
 baseline=cold()
 for cycle in range(o.runs):
  offset=len(g.text());g.serial('exec /BIN/JMPS.ELF')
  marker='[ OK ] native AArch64 nonlocal jumps restore GPR,FP,SP,nested flow and independent threads'
  wait(lambda:marker in g.text()[offset:] or '[FAIL]' in g.text()[offset:] or '[USER FAULT]' in g.text()[offset:],30,'actual setjmp/longjmp recovery')
  assert marker in g.text()[offset:],g.text()[offset:];assert cold()==baseline
  pid=int(re.search(r'pid=(\d+) path=/BIN/JMPS.ELF',g.text()[offset:])[1]);section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section)
  jumpRuns.append({'cycle':cycle,'pid':pid,'cold_pages_restored':baseline})
  offset=len(g.text());g.serial('exec /BIN/JSCVM.ELF'+('' if o.embedded else ' --filesystem'))
  def ready():
   text=g.text()[offset:];assert not any(s in text for s in ['[FAIL]','[USER FAULT]','[FATAL]']),text
   return ('[INFO] diagnostic embedded original font assets loaded' if o.embedded else '[INFO] actual snapshot filesystem fonts loaded') in text and '[ OK ] native font shaping,rasterization,limits and concurrency passed' in text and foreground() and probe.value(foreground()['pid'],o.application,'frames_submitted')==1
  wait(ready,60 if o.with_javascript else 45,'actual upstream fonts,original64 vectors,bounds/concurrency and first full text image');pid=foreground()['pid']
  if o.with_javascript:assert '[ OK ] actual JavaScript text VM created' in g.text()[offset:]
  frame('cycle'+str(cycle)+'-frame1',pid,1);responses=[]
  for epoch in [2,3]:
   before=time.monotonic();key('spc');wait(lambda:probe.value(pid,o.application,'frames_submitted')==epoch,6,'physical Space changes actual font size and raster image')
   responses.append(time.monotonic()-before);frame('cycle'+str(cycle)+'-frame'+str(epoch),pid,epoch)
  key('esc');wait(lambda:not probe.clients(),6,'text window closes');assert cold()==baseline
  assert '[ OK ] native text handles and application cleaned up' in g.text()[offset:]
  if o.with_javascript:
   for epoch in [1,2,3]:assert '[TEXT ENGINE] actual JavaScript normalized frame '+str(epoch)+' at ' in g.text()[offset:]
   assert '[ OK ] actual JavaScript text VM released' in g.text()[offset:]
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section)
  runs.append({'cycle':cycle,'pid':pid,'physical_redraw_seconds':responses,'all_pixels_compared_each_frame':512*256,'cold_pages_restored':baseline})
 assert not any(s in g.text() for s in ['[FAIL]','[USER FAULT]','[FATAL]'])
 if o.nvme:assert scalar('native_storage_writable')==0 and hashlib.sha256(disk.read_bytes()).hexdigest()==disk_before,'Read-only font fixture modified the disk'
 result={'status':'PASS','mode':o.mode,'NVMe':o.nvme,'read_only_NVMe_disk_unchanged':bool(o.nvme),'actual_JavaScript_text':o.with_javascript,'font_source':'compiled_original_assets' if o.embedded else 'actual_snapshot_filesystem','application_sha256':hashlib.sha256(o.application.read_bytes()).hexdigest(),'jmp_application_sha256':hashlib.sha256(jump.read_bytes()).hexdigest(),'cold_free_pages':baseline,'font_runs':runs,'nonlocal_jump_runs':jumpRuns,'checks':['Actual original FreeType2.15/HarfBuzz15.1/ICU79,not host rendering','All64 unmodified original Arabic normalization cases with exact glyph names,clusters and positions','Real Latin ligatures,Arabic joining/RTL and Devanagari shaping/rasterization','Every framebuffer image pixel matches separately built host reference for24/32/16px','Font ownership and eight-font cap,malformed font and invalid dimensions/language/length,clipping and padded-stride canaries','Shared-font and independent-font native pthread work with actual mutexes','Original AArch64 setjmp/longjmp restores all required GPR/d8..d15/SP/deep frames,zero/negative/extreme returns,nested aliases and three independent threads','Real key input,normal exits,exact cold physical/commit/engine/futex baselines'],'limits':['Font backend/rendering dependency only;not WebCore/HTML/CSS/DOM/full browser or Stage3 completion','Host reference uses original HarfBuzz Unicode functions;native backend uses real ICU79 Unicode functions','Only scalable outline fonts/opaque software text;image/SVG/color/WOFF2 and production font sandbox remain separate','QEMU evidence,new Fusion/Pi5/laptop testing NOT RUN']}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
except Exception as e:
 try:g.serial('procinfo');g.serial('taskinfo');g.serial('meminfo');time.sleep(.3);g.screen('failure');(out/'registers.txt').write_text(g.hmp('info registers'))
 except Exception:pass
 (out/'result.json').write_text(json.dumps({'status':'FAIL','error':str(e),'qualified':False},indent=2)+'\n');raise
finally:g.close()
