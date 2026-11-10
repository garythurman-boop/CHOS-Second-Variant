#!/usr/bin/env python3
"""Actual isolated Cairo pixels, native stdlib boundaries and cold cleanup."""
import argparse,pathlib,hashlib,json,subprocess,shutil,re,time,struct
from qemu_platform import Guest,wait
from guest_user_probe import GuestUserProbe
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct')
p.add_argument('--image-root',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path,required=True)
p.add_argument('--stdlib',type=pathlib.Path,required=True);p.add_argument('--reference',type=pathlib.Path,required=True)
p.add_argument('--runs',type=int,choices=[1,3],default=3)
o=p.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/EXT.ELF',str(o.stdlib)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=o.image_root/'chos.elf',uefi_kernel=o.image_root/'uefi-kernel.elf',iso=o.image_root/'chos.iso',memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk);probe=GuestUserProbe(g);probe.app_symbols(o.application);runs=[]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def key(k):g.hmp('sendkey '+k+' 50');time.sleep(.08)
def foreground():
    c=probe.foreground();return c if c and c['title']=='CHOS Native Cairo' else None
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
                raise AssertionError('Actual Cairo framebuffer differs from independent host reference')
            (out/(name+'.json')).write_text(json.dumps({'epoch':epoch,'actual_rgb_sha256':hashlib.sha256(actual).hexdigest(),'host_reference_rgb_sha256':hashlib.sha256(expected).hexdigest(),'complete_pixels_compared':512*256,'native_hash':hex(probe.value(pid,o.application,'cairo_frame_hash',size=8))},indent=2)+'\n')
            return True
        finally:g.qmp('cont')
    wait(capture,8,'actual Cairo pixels match host reference')
try:
    stage=json.loads((o.image_root/'native-engine.json').read_text());assert stage['source_sha256']==sha(o.application)
    raw=(out/'kernel.elf').read_bytes();start=g.symbols['chos_romfs_engine_start'];end=g.symbols['chos_romfs_engine_end'];ph=struct.unpack_from('<Q',raw,32)[0];embedded=None
    for i in range(struct.unpack_from('<H',raw,56)[0]):
        kind,flags,f,va,pa,n,memory,alignment=struct.unpack_from('<IIQQQQQQ',raw,ph+i*56)
        if kind==1 and va<=start<end<=va+n:embedded=raw[f+start-va:f+end-va]
    assert embedded is not None and hashlib.sha256(embedded).hexdigest()==stage['staged_sha256']
    wait(lambda:'chos> ' in g.text(),60,'scheduled runtime');wait(lambda:g.symbol('splash_active')==0,10,'splash');baseline=cold()
    for cycle in range(o.runs):
        offset=len(g.text());g.serial('exec /BIN/EXT.ELF');marker='[ OK ] native bounded string copies,atomic noncrypto rand and static recursive mutex passed'
        wait(lambda:marker in g.text()[offset:] or '[FAIL]' in g.text()[offset:] or '[USER FAULT]' in g.text()[offset:],40,'real stdlib native threads and inaccessible-page boundary')
        assert marker in g.text()[offset:],g.text()[offset:];assert cold()==baseline
        extpid=int(re.search(r'pid=(\d+) path=/BIN/EXT.ELF',g.text()[offset:])[1]);normal_exit(extpid)
        offset=len(g.text());g.serial('exec /BIN/JSCVM.ELF')
        def ready():
            text=g.text()[offset:];assert not any(s in text for s in ['[FAIL]','[USER FAULT]','[FATAL]']),text
            return '[ OK ] actual Cairo,pixman and FreeType native workers passed' in text and foreground() and probe.value(foreground()['pid'],o.application,'frames_submitted')==1
        wait(ready,60,'actual Cairo conformance,workers and first frame');pid=foreground()['pid'];frame('cycle'+str(cycle)+'-frame1',pid,1);response=[]
        for epoch in [2,3]:
            started=time.monotonic();key('spc');wait(lambda:probe.value(pid,o.application,'frames_submitted')==epoch,8,'actual Space redraw')
            response.append(time.monotonic()-started);frame('cycle'+str(cycle)+'-frame'+str(epoch),pid,epoch)
        key('esc');wait(lambda:not probe.clients(),6,'Cairo window closes');assert cold()==baseline;normal_exit(pid)
        assert '[ OK ] actual Cairo application and font caches cleaned up' in g.text()[offset:]
        runs.append({'cycle':cycle,'stdlib_pid':extpid,'cairo_pid':pid,'physical_redraw_seconds':response,'cold_pages_restored':baseline})
    assert not any(s in g.text() for s in ['[FAIL]','[USER FAULT]','[FATAL]'])
    (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':sha(o.application),'stdlib_sha256':sha(o.stdlib),'cold_free_pages':baseline,'runs':runs,'checks':['Actual Cairo1.18.4 with explicit port patches,pixman0.44 and FreeType dependencies in EL0','Analytic clip/alpha/stride/recording/matrix/error cases and three concurrent native rendering workers','All131072 pixels for three frames compared with independently built host reference','Actual physical key redraw,normal exit and exact physical/commit/engine/futex cleanup','Real native guard-page bounded string copies,recursive static mutex and deterministic atomic rand under three threads'],'limits':['Selected software rendering prerequisite;not WebCore/HTML/CSS/DOM or a modern browser','Cairo low-level glyph API does not supply text shaping;HarfBuzz shaping is separately qualified','rand is deterministic/noncryptographic;security uses existing real OS entropy','QEMU only;new Fusion/Pi5/laptop execution NOT RUN;Stage3 incomplete']},indent=2)+'\n')
except Exception as error:
    try:g.serial('procinfo');g.serial('taskinfo');g.serial('meminfo');time.sleep(.2);g.screen('failure');(out/'registers.txt').write_text(g.hmp('info registers'))
    except Exception:pass
    (out/'result.json').write_text(json.dumps({'status':'FAIL','qualified':False,'error':str(error)},indent=2)+'\n');raise
finally:g.close()
