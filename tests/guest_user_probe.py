"""Read-only QEMU probes for isolated EL0 app regression tests.

Use the kernel ELF copied by Guest and its actual DWARF member offsets. Resolve
the process-owned root, walk real AArch64 page tables and require EL0 permission.
This helper never writes guest memory, replaces input APIs or enables test hooks.
"""
import pathlib,re,shutil,struct,subprocess

class GuestUserProbe:
    def __init__(self,guest):
        self.g=guest;self.kernel=guest.out/'kernel.elf';self.layouts={};self.roots={};self.pages={};self.apps={}
        self.dwarf=shutil.which('llvm-dwarfdump')
        if not self.dwarf:raise RuntimeError('llvm-dwarfdump is required to validate actual guest layout')
        self.symbol_sizes={}
        for line in subprocess.check_output(['nm','-S',str(self.kernel)],text=True).splitlines():
            p=line.split()
            if len(p)==4:self.symbol_sizes[p[3]]=int(p[1],16)
    def layout(self,name):
        if name in self.layouts:return self.layouts[name]
        typedef=subprocess.check_output([self.dwarf,'--name='+name,str(self.kernel)],text=True)
        match=re.search(r'DW_AT_type\s+\((0x[0-9a-f]+) "structure ',typedef)
        if not match:raise AssertionError('missing DWARF structure '+name)
        data=subprocess.check_output([self.dwarf,'--debug-info='+match[1],'--show-children','--recurse-depth=1',str(self.kernel)],text=True)
        size_match=re.search(r'DW_AT_byte_size\s+\((0x[0-9a-f]+|[0-9]+)\)',data)
        if not size_match:raise AssertionError('missing DWARF size '+name)
        members={}
        for member in data.split('DW_TAG_member')[1:]:
            field=re.search(r'DW_AT_name\s+\("([^"]+)"\)',member)
            offset=re.search(r'DW_AT_data_member_location\s+\((0x[0-9a-f]+|[0-9]+)\)',member)
            if field and offset:members[field[1]]=int(offset[1],0)
        result=(int(size_match[1],0),members);self.layouts[name]=result;return result
    def physical(self,address,size):
        target=self.g.out/'user-probe.bin'
        result=self.g.hmp(f'pmemsave {address:#x} {size} "{target}"')
        if result:raise AssertionError(result)
        data=target.read_bytes();assert len(data)==size;return data
    @staticmethod
    def u32(data,offset):return struct.unpack_from('<I',data,offset)[0]
    @staticmethod
    def u64(data,offset):return struct.unpack_from('<Q',data,offset)[0]
    def process(self,pid):
        stride,slot=self.layout('process_slot_t');_,info=self.layout('chos_process_info_t')
        size=self.symbol_sizes['process_slots'];assert size%stride==0
        data=self.g.block('process_slots',size=size)
        for off in range(0,size,stride):
            p=data[off+slot['info']:off+stride]
            if self.u32(p,info['pid'])==pid:
                return {'pid':pid,'state':self.u32(p,info['state']),'address_space_id':self.u32(p,info['address_space_id']),
                        'name':p[info['name']:info['name']+16].split(b'\0')[0].decode('ascii')}
        raise AssertionError('No process owns PID '+str(pid))
    def root(self,pid):
        if pid in self.roots:return self.roots[pid]
        process=self.process(pid);assert process['state'] in (2,3),process
        stride,members=self.layout('address_space_slot_t');size=self.symbol_sizes['spaces'];assert size%stride==0
        header=max(members['live']+4,members['id']+4,members['root_pa']+8)
        for off in range(0,size,stride):
            data=self.g.block('spaces',off,size=header)
            if self.u32(data,members['live']) and self.u32(data,members['id'])==process['address_space_id']:
                root=self.u64(data,members['root_pa']);assert root and not(root%4096)
                self.roots[pid]=root;return root
        raise AssertionError('No live address space for '+str(process))
    def translation(self,pid,address):
        page=address&~4095;key=(pid,page)
        if key in self.pages:return self.pages[key]+address%4096
        assert 0x20000000<=address<0x30000000
        table=self.root(pid);shifts=[30,21,12] if self.g.args.mode=='direct' else [39,30,21,12]
        for shift in shifts:
            descriptor=self.u64(self.physical(table+((address>>shift)&511)*8,8),0)
            assert descriptor&1,('unmapped EL0 probe',pid,hex(address),shift)
            if shift==12:
                assert descriptor&3==3
                assert (descriptor>>6)&3 in (1,3),('not EL0 accessible',hex(descriptor))
                base=descriptor&0x0000fffffffff000;self.pages[key]=base;return base+address%4096
            assert descriptor&3==3,('EL0 probe expects private page tables',shift,hex(descriptor))
            table=descriptor&0x0000fffffffff000
        raise AssertionError('incomplete EL0 page walk')
    def read(self,pid,address,size):
        assert size>=0 and address>=0x20000000 and address+size<=0x30000000
        result=bytearray()
        while size:
            amount=min(size,4096-address%4096);result+=self.physical(self.translation(pid,address),amount)
            address+=amount;size-=amount
        return bytes(result)
    def app_symbols(self,app):
        name=str(pathlib.Path(app).resolve())
        if name not in self.apps:
            target=self.g.out/(pathlib.Path(app).stem+'.app.elf');shutil.copyfile(app,target)
            symbols={}
            for line in subprocess.check_output(['nm',str(target)],text=True).splitlines():
                p=line.split()
                if len(p)==3:symbols[p[2]]=int(p[0],16)
            self.apps[name]=symbols
        return self.apps[name]
    def block(self,pid,app,name,size):
        address=self.app_symbols(app)[name]
        if address<0x20000000:address+=0x20000000 # CHCX link-relative symbols.
        return self.read(pid,address,size)
    def text(self,pid,app,name,size=128):return self.block(pid,app,name,size).split(b'\0')[0]
    def value(self,pid,app,name,size=4):return int.from_bytes(self.block(pid,app,name,size),'little')
    def clients(self):
        stride,members=self.layout('gui_app_client_t');_,caret=self.layout('chos_gui_caret_t')
        size=self.symbol_sizes['gui_app_clients'];assert size%stride==0;result=[]
        for off in range(0,size,stride):
            prefix=max(members['active']+4,members['window_id']+4,members['owner_pid']+4,members['title']+128)
            data=self.g.block('gui_app_clients',off,size=prefix)
            if not self.u32(data,members['active']):continue
            raw=self.g.block('gui_app_clients',off+members['caret'],size=self.layout('chos_gui_caret_t')[0])
            result.append({'window_id':self.u32(data,members['window_id']),'pid':self.u32(data,members['owner_pid']),
                'title':data[members['title']:members['title']+128].split(b'\0')[0].decode('ascii'),
                'caret':{field:self.u32(raw,caret[field]) for field in ['x','y','width','height','enabled','text_length']}})
        return result
    def foreground(self):
        view=self.g.symbol('view')
        return next((c for c in self.clients() if c['window_id']==view),None)
    def queued_events(self,pid):
        stride,members=self.layout('gui_app_client_t')
        for off in range(0,self.symbol_sizes['gui_app_clients'],stride):
            size=max(members['active']+4,members['owner_pid']+4)
            data=self.g.block('gui_app_clients',off,size=size)
            if self.u32(data,members['active']) and self.u32(data,members['owner_pid'])==pid:
                return int.from_bytes(self.g.block('gui_app_clients',off+members['event_count'],size=8),'little')
        raise AssertionError('No active GUI client for process '+str(pid))
