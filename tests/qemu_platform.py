#!/usr/bin/env python3
"""Live QEMU acceptance checks. Always uses disposable disk/firmware copies.
Screenshots prove visible changes; QMP injects real emulated input devices.
No QMP event is injected directly into a CHOS API.
"""
import argparse,hashlib,json,math,os,pathlib,re,shutil,socket,struct,subprocess,tempfile,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
class Guest:
    def __init__(self,args,out,disk):
        self.out=out;self.log=out/'serial.log';self.logfile=self.log.open('wb')
        self.args=args
        original=args.kernel if args.mode=='direct' else args.uefi_kernel
        elf=out/'kernel.elf';shutil.copyfile(original,elf)
        iso=out/'boot.iso'
        if args.mode=='uefi':shutil.copyfile(args.iso,iso)
        artifacts={'kernel_sha256':hashlib.sha256(elf.read_bytes()).hexdigest()}
        if args.mode=='uefi':artifacts['iso_sha256']=hashlib.sha256(iso.read_bytes()).hexdigest()
        (out/'artifacts.json').write_text(json.dumps(artifacts,indent=2))
        self.symbols={}
        for line in subprocess.check_output(['nm',str(elf)],text=True).splitlines():
            parts=line.split()
            if len(parts)==3:self.symbols[parts[2]]=int(parts[0],16)
        self.sockpath=out/'qmp.sock'
        cmd=['qemu-system-aarch64','-M','virt,gic-version='+('2' if args.mode=='direct' else '3'),
             '-cpu','cortex-a72','-m',args.memory,'-display','none','-no-reboot',
             '-global','virtio-mmio.force-legacy=false','-serial','stdio',
             '-qmp',f'unix:{self.sockpath},server=on,wait=off']
        if args.qemu_data: cmd+=['-L',str(args.qemu_data)]
        if args.mode=='direct':
            cmd+=['-device',f'loader,file={elf},cpu-num=0','-device','virtio-gpu-device',
                  '-device','virtio-keyboard-device','-device','virtio-mouse-device',
                  '-drive',f'if=none,file={disk},format=raw,id=disk','-device','virtio-blk-device,drive=disk',
                  '-netdev','user,id=net','-device','virtio-net-device,netdev=net']
            if not args.no_rng: cmd+=['-object','rng-random,filename=/dev/urandom,id=rng','-device','virtio-rng-device,rng=rng']
        else:
            vars=out/'vars.fd';shutil.copyfile(args.firmware.parent/'AAVMF_VARS.fd',vars)
            cmd+=['-drive',f'if=pflash,format=raw,readonly=on,file={args.firmware}',
                  '-drive',f'if=pflash,format=raw,file={vars}','-device','ramfb','-net','none',
                  '-device','qemu-xhci,id=xhci','-device','usb-kbd,bus=xhci.0,id=kbd',
                  '-device',('usb-tablet' if args.tablet else 'usb-mouse')+',bus=xhci.0,id=mouse']
            if args.mode=='uefi': cmd+=['-device','virtio-scsi-device,id=scsi',
                  '-drive',f'if=none,file={iso},format=raw,media=cdrom,id=cd','-device','scsi-cd,drive=cd']
            else: cmd+=['-drive',f'if=none,file={disk},format=raw,id=disk','-device','virtio-blk-device,drive=disk']
        (out/'command.json').write_text(json.dumps(cmd,indent=2))
        self.started=time.monotonic()
        self.process=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=self.logfile,stderr=subprocess.STDOUT)
        self.sock=socket.socket(socket.AF_UNIX);self.sock.settimeout(3)
        wait(lambda:self.sockpath.exists(),5,'QMP startup')
        self.sock.connect(str(self.sockpath));self.f=self.sock.makefile('rwb',buffering=0);self.f.readline();self.qmp('qmp_capabilities')
    def text(self): return self.log.read_text(errors='replace')
    def qmp(self,execute,**args):
        self.f.write(json.dumps({'execute':execute,'arguments':args}).encode()+b'\n')
        while True:
            r=json.loads(self.f.readline())
            if 'error' in r: raise RuntimeError(r)
            if 'return' in r:return r['return']
    def hmp(self,c):return self.qmp('human-monitor-command',**{'command-line':c})
    def block(self,name,offset=0,size=4):
        address=self.symbols[name]+offset
        if self.args.mode!='direct':
            match=re.search(r'Kernel physical base: (0x[0-9a-f]+)',self.text())
            if not match: return 1
            address+=int(match[1],16)-self.symbols['__kernel_start']
        target=self.out/'probe.bin'
        result=self.hmp(f'pmemsave {address:#x} {size} "{target}"')
        if result:raise AssertionError(result)
        return target.read_bytes()
    def symbol(self,name,offset=0):return int.from_bytes(self.block(name,offset),'little')
    def window(self):
        view=self.symbol('view');data=self.block('window_manager',size=12*64)
        for offset in range(0,len(data),64):
            row=struct.unpack('<i4xQiiiiI7i',data[offset:offset+64])
            if row[0]==view and row[7]:
                return dict(id=view,x=row[2],y=row[3],width=row[4],height=row[5],minimized=row[8],maximized=row[9])
        raise AssertionError('No visible foreground window')
    def serial(self,c):
        self.process.stdin.write((c+'\n').encode());self.process.stdin.flush()
    def screen(self,name):
        p=self.out/(name+'.ppm');self.qmp('screendump',filename=str(p));data=p.read_bytes()
        # Compare only canvas, excluding the RTC taskbar clock.
        pixels=data.split(b'\n',3)[3];return pixels[:800*540*3]
    def cpu_ticks(self):
        fields=pathlib.Path(f'/proc/{self.process.pid}/stat').read_text().split()
        return int(fields[13])+int(fields[14])
    def close(self):
        self.process.terminate()
        try:self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:self.process.kill();self.process.wait(timeout=3)
        self.logfile.close();self.sock.close()
def wait(predicate,seconds,description):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if predicate():return
        time.sleep(.02)
    raise AssertionError('timeout: '+description)
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mode',choices=['direct','uefi','installed'],default='direct')
    ap.add_argument('--kernel',type=pathlib.Path,default=ROOT/'build/chos.elf')
    ap.add_argument('--iso',type=pathlib.Path,default=ROOT/'build/chos-0.30.0.iso')
    ap.add_argument('--firmware',type=pathlib.Path)
    ap.add_argument('--disk',type=pathlib.Path,default=ROOT/'build/qemu-smoke-disk.img')
    ap.add_argument('--out',type=pathlib.Path,required=True)
    ap.add_argument('--qemu-data',type=pathlib.Path)
    ap.add_argument('--tablet',action='store_true')
    ap.add_argument('--memory',default='512M')
    ap.add_argument('--uefi-kernel',type=pathlib.Path,default=ROOT/'build/uefi-kernel/chos.elf')
    ap.add_argument('--timer-fault',action='store_true')
    ap.add_argument('--baseline',action='store_true');ap.add_argument('--no-rng',action='store_true')
    args=ap.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    # Refuse to overwrite prior evidence or a user disk.
    if (out/'serial.log').exists():raise SystemExit('Use a new --out directory to preserve evidence')
    disk=out/'disposable.img'
    if args.mode!='uefi':shutil.copyfile(args.disk,disk)
    if args.mode=='direct' and not args.baseline:
        subprocess.run(['python3',str(ROOT/'tools/mkchfs.py'),'put',str(disk),'/BIN/RNGTEST.CHX',str(ROOT/'build/userland/rng_test.chcx')],check=True)
    g=None;result={'mode':args.mode,'checks':[]}
    try:
        g=Guest(args,out,disk)
        marker='chos> ' if args.mode=='direct' else '[OK] Initial CHOS GUI frame rendered.'
        wait(lambda:marker in g.text(),45,'guest desktop and console')
        result['kernel_ready_seconds']=round(time.monotonic()-g.started,3)
        wait(lambda:g.symbol('splash_active')==0,10,'minimum splash finished')
        time.sleep(.1)
        result['desktop_ready_seconds']=round(time.monotonic()-g.started,3)
        if args.mode!='direct':
            assert 'HID devices=2' in g.text(),g.text()
            assert 'IRQ delivery enabled after initial desktop frame' in g.text()
            if args.timer_fault: assert 'Timer IRQ delivery did not progress' in g.text()
            else: assert 'Timer IRQ delivery verified' in g.text()
        else:
            if not args.baseline:
                g.serial('rng-test')
                expected='[FAIL] RNG hardware bytes unavailable' if args.no_rng else '[ OK ] RNG received 32 hardware bytes'
                wait(lambda:expected in g.text(),5,'actual RNG receive / missing-device failure')
                g.serial('exec /BIN/RNGTEST.CHX'+(' missing' if args.no_rng else ''))
                expected='[ OK ] EL0 RNG validation and '+('missing-device failure' if args.no_rng else 'hardware receive')+' passed'
                wait(lambda:expected in g.text(),5,'EL0 RNG pointer/length/guard and entropy contract')
                result['checks']+=['EL0 RNG invalid pointers, length bound, guard bytes and '+('missing device' if args.no_rng else 'hardware receipt')]
            g.serial('fswrite /STAGE1.TXT stage1-persistence')
            wait(lambda:'file written (directory committed' in g.text(),5,'CHFS write/flush')
            g.serial('fsread /STAGE1.TXT')
            wait(lambda:g.text().count('stage1-persistence')>=2,5,'CHFS readback')
            g.serial('ping 10.0.2.2')
            next_query=[0.0]
            def ping_replied():
                if time.monotonic()>next_query[0]:
                    g.serial('netstatus');next_query[0]=time.monotonic()+.5
                return 'Ping: reply received' in g.text()
            wait(ping_replied,10,'actual DHCP/ARP/ICMP exchange')
            result['checks']+=['CHFS write/flush/read','DHCP/ARP/ICMP']
        if args.mode=='direct' and not args.baseline:
            assert 'Post-enable translation, dynamic map/unmap and RAM alias probe passed' in g.text()
            result['checks']+=['MMU permissions, guard pages and dynamic aliases with 512 MiB RAM']
        def motion(x,y):
            if args.tablet:
                xx=max(0,min(799,g.symbol('mouse_x')+x));yy=max(0,min(599,g.symbol('mouse_y')+y))
                g.qmp('input-send-event',events=[{'type':'abs','data':{'axis':'x','value':round(xx*32767/799)}},
                                                {'type':'abs','data':{'axis':'y','value':round(yy*32767/599)}}])
            else:
                g.qmp('input-send-event',events=[{'type':'rel','data':{'axis':'x','value':x}},{'type':'rel','data':{'axis':'y','value':y}}])
            time.sleep(.04)
        before=g.screen('desktop');t=time.monotonic();g.hmp('sendkey c')
        wait(lambda:g.screen('console')!=before,2,'keyboard shortcut visible response')
        result['keyboard_visible_ms']=round((time.monotonic()-t)*1000,2)
        before=g.screen('pointer-before');t=time.monotonic()
        motion(-200,150)
        wait(lambda:g.screen('pointer-after')!=before,2,'pointer visible response')
        result['pointer_visible_ms']=round((time.monotonic()-t)*1000,2)
        result['checks']+=['keyboard shortcut','absolute tablet' if args.tablet else 'relative pointer']
        if not args.baseline:
            wheel=g.symbol('chos_input_diagnostics',24)
            for button_name in ['wheel-up','wheel-down']:
                g.qmp('input-send-event',events=[{'type':'btn','data':{'button':button_name,'down':True}},
                                                {'type':'btn','data':{'button':button_name,'down':False}}])
                time.sleep(.1)
            wait(lambda:g.symbol('chos_input_diagnostics',24)>=wheel+2,2,'actual wheel reports reach shared input')
            result['checks']+=['wheel reports through hardware transport to shared input']
        def move_to(x,y):
            if args.tablet:
                g.qmp('input-send-event',events=[{'type':'abs','data':{'axis':'x','value':math.ceil(x*32767/799)}},
                                                {'type':'abs','data':{'axis':'y','value':math.ceil(y*32767/599)}}])
                time.sleep(.06);return
            # Saturate to the top-left through real relative reports, then move
            # in chunks below both USB's signed 8-bit and GUI delta bounds.
            for _ in range(9): motion(-100,-100)
            while x or y:
                dx=min(100,x);dy=min(100,y);motion(dx,dy);x-=dx;y-=dy
        def button(down):
            g.qmp('input-send-event',events=[{'type':'btn','data':{'button':'left','down':down}}]);time.sleep(.06)
        # Maximize/restore is the desktop's implemented resize operation.
        original=g.window()
        assert original['id']!=0,'keyboard shortcut must open Console, not merely change splash/clock pixels'
        move_to(original['x']+original['width']-38,original['y']+13)
        before=g.screen('maximize-before');t=time.monotonic()
        button(True);button(False)
        wait(lambda:g.window()['id']==original['id'] and g.window()['maximized'],2,'maximize foreground window')
        after=g.screen('maximize-after')
        assert sum(a!=b for a,b in zip(before,after))>20000
        result['maximize_visible_ms']=round((time.monotonic()-t)*1000,2)
        maximized=g.window()
        move_to(maximized['x']+maximized['width']-38,maximized['y']+13)
        button(True);button(False)
        wait(lambda:g.window()==original,2,'restore exact foreground window geometry')
        move_to(original['x']+100,original['y']+13)
        before=g.screen('drag-before');t=time.monotonic()
        button(True);motion(50,40);button(False)
        wait(lambda:g.window()['id']==original['id'] and (g.window()['x'],g.window()['y'])!=(original['x'],original['y']),2,'move same window by title bar')
        after=g.screen('drag-after')
        assert sum(a!=b for a,b in zip(before,after))>20000
        result['drag_visible_ms']=round((time.monotonic()-t)*1000,2)
        result['checks']+=['window maximize/restore','title-bar drag']
        if args.mode!='direct':
            # More than one producer/event-ring wrap; no controller reset
            # between these actual slot disable/address/configure operations.
            current='mouse'
            for i in range(20):
                g.qmp('device_del',id=current);time.sleep(.12)
                current='mouse'+str(i)
                g.qmp('device_add',driver='usb-tablet' if args.tablet else 'usb-mouse',bus='xhci.0',id=current);time.sleep(.25)
            before=g.screen('reconnect-before');motion(100,0)
            wait(lambda:g.screen('reconnect-after')!=before,2,'hot-reconnected mouse after 20 cycles')
            result['checks']+=['20 USB hot-unplug/reconnect cycles with ring wraps']
        ticks=g.cpu_ticks();start=time.monotonic();time.sleep(2)
        result['idle_cpu_percent']=round(100*(g.cpu_ticks()-ticks)/os.sysconf('SC_CLK_TCK')/(time.monotonic()-start),2)
        result['checks']+=['2-second idle observation']
        if args.mode=='direct':
            g.close();g=None
            # Second fresh machine on the same disposable disk checks persistence.
            reboot=out/'reboot';reboot.mkdir();g=Guest(args,reboot,disk)
            wait(lambda:'chos> ' in g.text(),45,'second boot');g.serial('fsread /STAGE1.TXT')
            wait(lambda:'stage1-persistence' in g.text(),5,'CHFS persisted after restart')
            result['checks']+=['storage persistence after fresh guest boot']
        result['status']='PASS'
    except Exception as e:
        result['status']='FAIL';result['error']=str(e)
        if g and not args.baseline:
            result['input_counters']={k:g.symbol('chos_input_diagnostics',offset) for k,offset in [('keys',0),('buttons',8),('relative',16),('wheel',24)]}
            result['guest_view']=g.symbol('view')
    finally:
        if g:g.close()
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    return 0 if result['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
