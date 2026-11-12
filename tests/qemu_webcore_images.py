#!/usr/bin/env python3
"""Original WebCore image pipeline,owned Cairo pixels and exact native cleanup."""
import argparse,pathlib,hashlib,json,subprocess,shutil,re,time,struct
from qemu_platform import Guest,wait
from guest_user_probe import GuestUserProbe
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct')
p.add_argument('--image-root',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path,required=True)
p.add_argument('--reference',type=pathlib.Path,required=True)
p.add_argument('--runs',type=int,choices=[1,3],default=3)
p.add_argument('--large',action='store_true',help='Require the real large contiguous WebCore image case (qualified engine SDK only)')
o=p.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
source=o.application.resolve();frozen=out/'application.elf';shutil.copyfile(source,frozen);o.application=frozen
reference=out/'reference';reference.mkdir()
for epoch in [1,2,3]:shutil.copyfile(o.reference/('frame'+str(epoch)+'.rgb'),reference/('frame'+str(epoch)+'.rgb'))
o.reference=reference
disk=out/'disposable.img'
subprocess.run(['python3','tools/mkchfs.py','create',str(disk),'--size-mib','32'],check=True)
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/WEBCORE.ELF',str(o.application)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=o.image_root/'chos.elf',uefi_kernel=o.image_root/'uefi-kernel.elf',iso=o.image_root/'chos.iso',memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk);probe=GuestUserProbe(g);probe.app_symbols(o.application);runs=[]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def key(k):g.hmp('sendkey '+k+' 50');time.sleep(.08)
def foreground():
    c=probe.foreground();return c if c and c['title']=='CHOS WebCore Images' else None
def cold():
    deadline=time.monotonic()+15;header='CHOS process table (foreground and scheduled EL0 processes)'
    while True:
        n=g.text().count(header);g.serial('procinfo');wait(lambda:g.text().count(header)>n,4,'process status')
        if re.search(r'Active: 0;.*pending cleanup: 0',g.text().rsplit(header,1)[-1]):break
        assert time.monotonic()<deadline,'process cleanup failed'
    n=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo');wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>n,4,'physical page report')
    assert g.symbol('committed_frame_limits')==0 and g.symbol('engine_owner_slot')==0xffffffff and g.symbol('futex_queued_waiters')==0
    return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
def normal_exit(pid):
    section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
    assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
def frame(name,pid,epoch):
    expected=(o.reference/('frame'+str(epoch)+'.rgb')).read_bytes();assert len(expected)==512*256*3
    def capture():
        g.qmp('stop')
        try:
            if g.symbol('presented_generation')!=g.symbol('dirty_generation') or probe.value(pid,o.application,'frames_submitted')!=epoch:return False
            w=g.window();g.screen(name,full=True);raw=(out/(name+'.ppm')).read_bytes().split(b'\n',3);width,height=map(int,raw[1].split());screen=raw[3];assert len(screen)==width*height*3
            scale=3 if width>=2400 and height>=1530 else 2 if width>=1600 and height>=1024 else 1
            x=w['x']+18*scale;y=w['y']+98*scale
            actual=bytes(screen[((y+r*scale)*width+x+c*scale)*3+ch] for r in range(256) for c in range(512) for ch in range(3))
            if actual!=expected:
                (out/(name+'-actual.rgb')).write_bytes(actual)
                differing=[(i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b]
                (out/(name+'-differences.json')).write_text(json.dumps({'components':len(differing),'maximum_error':max(abs(a-b) for i,a,b in differing),'first':differing[:30]},indent=2))
                raise AssertionError('Actual WebCore image framebuffer differs from independent host reference')
            (out/(name+'.json')).write_text(json.dumps({'epoch':epoch,'actual_rgb_sha256':hashlib.sha256(actual).hexdigest(),'host_reference_rgb_sha256':hashlib.sha256(expected).hexdigest(),'complete_pixels_compared':512*256,'native_hash':hex(probe.value(pid,o.application,'webcore_frame_hash',size=8))},indent=2)+'\n')
            return True
        finally:g.qmp('cont')
    wait(capture,8,'actual WebCore image pixels match host reference')
try:
    wait(lambda:'chos> ' in g.text(),60,'scheduled runtime');wait(lambda:g.symbol('splash_active')==0,10,'splash');baseline=cold()
    for cycle in range(o.runs):
        offset=len(g.text());g.serial('exec /WEBCORE.ELF'+(' large' if o.large else ''))
        def ready():
            text=g.text()[offset:];assert not any(s in text for s in ['[FAIL]','[USER FAULT]','[FATAL]']),text
            return '[ OK ] actual WebCore image decoding passed' in text and foreground() and probe.value(foreground()['pid'],o.application,'frames_submitted')==1
        wait(ready,60,'actual WebCore image decoding and first frame');pid=foreground()['pid'];frame('cycle'+str(cycle)+'-frame1',pid,1);response=[]
        for epoch in [2,3]:
            started=time.monotonic();key('spc');wait(lambda:probe.value(pid,o.application,'frames_submitted')==epoch,8,'actual Space redraw')
            response.append(time.monotonic()-started);frame('cycle'+str(cycle)+'-frame'+str(epoch),pid,epoch)
        key('esc');wait(lambda:not probe.clients(),6,'WebCore image window closes');assert cold()==baseline;normal_exit(pid)
        assert '[ OK ] actual WebCore image application closed' in g.text()[offset:]
        if o.large:assert '[ OK ] actual WebCore large contiguous image passed' in g.text()[offset:]
        heaps=re.findall(r'\[WEBCORE HEAP\] allocations=(\d+) mapped=(\d+) warm_allocations=(\d+) warm_mapped=(\d+)',g.text()[offset:])
        assert len(heaps)==1 and heaps[0][:2]==heaps[0][2:],heaps
        runs.append({'cycle':cycle,'pid':pid,'physical_redraw_seconds':response,'cold_pages_restored':baseline,
                     'process_global_warm_allocations':int(heaps[0][0]),'process_global_warm_mapped_bytes':int(heaps[0][1])})
    assert not any(s in g.text() for s in ['[FAIL]','[USER FAULT]','[FATAL]'])
    (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'large_contiguous_image':o.large,'application_sha256':sha(o.application),'cold_free_pages':baseline,'runs':runs,'checks':['Actual original WebCore image decoders and Cairo backing store in isolated EL0','All131072 pixels per frame matched against reference images','Physical Space/Escape redraw/close and normal process exit','Exact physical-page,engine,commit and futex cleanup across repeated lifecycles'],'limits':['Image component,not page layout,HTML/CSS/DOM or a modern browser','Selected images and lifetime checks,not full hostile-content or engine qualification','QEMU only;new Fusion/Pi5/laptop execution NOT RUN;Stage3 incomplete']},indent=2)+'\n')
except Exception as error:
    try:g.serial('procinfo');g.serial('taskinfo');g.serial('meminfo');time.sleep(.2);g.screen('failure');(out/'registers.txt').write_text(g.hmp('info registers'))
    except Exception:pass
    (out/'result.json').write_text(json.dumps({'status':'FAIL','qualified':False,'error':str(error)},indent=2)+'\n');raise
finally:g.close()
