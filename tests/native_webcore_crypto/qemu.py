#!/usr/bin/env python3
"""Execute the original PAL digest API in disposable direct/UEFI CHOS guests."""
import argparse,hashlib,json,pathlib,re,shutil,subprocess,sys,time
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tests'))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__)
for n in ['out','application','image-root']:p.add_argument('--'+n,type=pathlib.Path,required=True)
p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
app=out/'application.elf';shutil.copyfile(o.application,app)
assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
headers=subprocess.check_output(['llvm-readelf','-lW',str(app)],text=True)
assert not re.search(r'^\s+TLS\s',headers,re.M) and not re.search(r'^\s+LOAD[^\n]*\bRWE\b',headers,re.M)
disk=out/'disposable.img'
subprocess.run(['python3',str(ROOT/'tools/mkchfs.py'),'create',str(disk),'--size-mib','16'],check=True)
subprocess.run(['python3',str(ROOT/'tools/mkchfs.py'),'put',str(disk),'/DIGEST.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=o.image_root/'chos.elf',uefi_kernel=o.image_root/'uefi-kernel.elf',iso=o.image_root/'chos.iso',memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=None;runs=[]
def cold():
    header='CHOS process table (foreground and scheduled EL0 processes)';end='Loader sources: packaged ROMFS and checked CHFS executables.';deadline=time.monotonic()+15
    while True:
        count=g.text().count(header);g.serial('procinfo');wait(lambda:g.text().count(header)>count and end in g.text().rsplit(header,1)[-1],4,'complete process report')
        section=g.text().rsplit(header,1)[-1]
        if re.search(r'Active: 0;.*pending cleanup: 0',section):break
        assert time.monotonic()<deadline,'process cleanup failed';time.sleep(.03)
    page=r'Free pages: (\d+) \(approximately \d+ MiB\)\r?\n';count=len(re.findall(page,g.text()));g.serial('meminfo');wait(lambda:len(re.findall(page,g.text()))>count,4,'complete physical page report')
    assert g.symbol('committed_frame_limits')==0 and g.symbol('engine_owner_slot')==0xffffffff and g.symbol('futex_queued_waiters')==0
    return int(re.findall(page,g.text())[-1]),section
try:
    g=Guest(a,out,disk);wait(lambda:'chos> ' in g.text(),60,'scheduled runtime');wait(lambda:g.symbol('splash_active')==0,10,'desktop splash')
    if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
    baseline,_=cold()
    for cycle in range(3):
        offset=len(g.text());g.serial('exec /DIGEST.ELF');marker='[ OK ] actual PAL digest application completed';failures=['[FAIL]','[FATAL]','[USER FAULT]']
        wait(lambda:marker in g.text()[offset:] or any(f in g.text()[offset:] for f in failures),180,'actual original PAL digest fixture')
        text=g.text()[offset:];assert marker in text and not any(f in text for f in failures),text
        assert text.count('[ OK ] original PAL CryptoDigest five algorithms,80 cases,errors and three threads passed')==3,text
        heaps=[tuple(map(int,m)) for m in re.findall(r'\[PAL DIGEST HEAP\] round=(\d+) allocations=(\d+) mapped=(\d+)',text)]
        assert len(heaps)==3 and [v[0] for v in heaps]==[0,1,2] and len({v[1:] for v in heaps})==1,heaps
        pages,section=cold();assert pages==baseline,(baseline,pages)
        pid=int(re.search(r'pid=(\d+) path=/DIGEST.ELF',g.text()[offset:])[1]);assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
        runs.append({'cycle':cycle,'pid':pid,'cold_pages_restored':pages,'warm_heap_per_round':heaps})
    (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':sha(app),'harness_sha256':sha(pathlib.Path(__file__).resolve()),'cold_free_pages':baseline,'runs':runs,'checks':['Unchanged original PAL CryptoDigest interface,actual native WTF and all five MbedTLS digests','80 independent empty/abc/million-a/padding/binary reference cases,fragmented input,repeat finalization,hex string and three concurrent workers','Injected allocator/start/update/final failures return null or empty results and release resources','Three complete process lifetimes with exact cold PMM,engine ownership,commit and futex restoration'],'limits':['Selected PAL component; full generated WebCore ABI/page/browser not qualified','SHA1 available only in separate namespaced content-digest target; TLS configuration unchanged','No physical Fusion/Pi5/laptop run;Stage3 incomplete']},indent=2)+'\n')
except Exception as error:
    if g:
        try:g.serial('procinfo');g.serial('meminfo');time.sleep(.2);g.screen('failure');(out/'registers.txt').write_text(g.hmp('info registers'))
        except Exception:pass
    (out/'result.json').write_text(json.dumps({'status':'FAIL','error':str(error)},indent=2)+'\n');raise
finally:
    if g:g.close()
