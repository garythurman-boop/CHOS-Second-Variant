/* Polled xHCI 1.x, identity DMA with MMU/caches off after EFI handoff.
 * Direct-attached USB2/USB3 HID only. No firmware services or fixed PCI IDs.
 * DMA storage is permanent: on controller failure never free memory still
 * owned by hardware. Single bootstrap/GUI task is the sole producer. */
#include "chos/xhci.h"
#include "chos/usb.h"
#define MAX_SLOTS 8u
#define MAX_PORTS 32u
#define TRB(t) ((uint32_t)(t)<<10)
#define SUCCESS 1u
#define SHORT_PACKET 13u
#define TIMEOUT_MS 1000u
#define PORT_CCS 1u
#define PORT_PED 2u
#define PORT_PR (1u<<4)
#define PORT_CHANGE (0x7fu<<17)
#define PORT_PRESERVE ((1u<<9)|(3u<<14)|(7u<<25))
#define SLOT_BYTES 2048u

typedef struct {
    chos_xhci_ring_t ring;
    uint8_t buffer[CHOS_USB_REPORT_BYTES] __attribute__((aligned(64)));
    chos_hid_report_layout_t layout;
    chos_usb_input_state_t input;
    chos_usb_hid_interface_t iface;
    uint64_t pending;
    unsigned malformed;
    uint8_t dci;
} endpoint_t;
typedef struct {
    uint8_t output[SLOT_BYTES] __attribute__((aligned(64)));
    uint8_t input[SLOT_BYTES+64] __attribute__((aligned(64)));
    chos_xhci_ring_t control;
    uint8_t data[4096] __attribute__((aligned(64)));
    endpoint_t endpoints[CHOS_USB_MAX_HID];
    uint8_t port, slot, speed, endpoint_count;
    int active;
} usb_device_t;
static usb_device_t devices[MAX_SLOTS] __attribute__((aligned(4096)));
static chos_xhci_ring_t commands __attribute__((aligned(4096)));
static chos_xhci_trb_t events[256] __attribute__((aligned(4096)));
static uint64_t dcbaa[256] __attribute__((aligned(4096)));
static uint64_t erst[2] __attribute__((aligned(64)));
static uint64_t scratch_array[16] __attribute__((aligned(4096)));
static uint8_t scratch[16][4096] __attribute__((aligned(4096)));
static uintptr_t mmio, op, runtime, doorbells;
static uint32_t context_bytes, port_count, slot_limit;
static unsigned event_head, event_cycle, online, port_cursor;
static uint64_t awaited, completed, completion_parameter, control_data_trb;
static int control_short;
static uint32_t completion_status, completion_control;
static int running, poisoned;
static uint8_t port_attempted[MAX_PORTS];
static const char *error="xHCI not probed";
static const chos_boot_info_t *saved_boot;
static const chos_acpi_info_t *saved_acpi;
static chos_pci_device_t saved_pci;
#ifdef CHOS_HOST_TEST
extern uint32_t chos_xhci_mock_read(uintptr_t);
extern void chos_xhci_mock_write(uintptr_t,uint32_t);
static uint32_t rd(uintptr_t a) { return chos_xhci_mock_read(a); }
static void wr(uintptr_t a,uint32_t v) { chos_xhci_mock_write(a,v); }
#define XHCI_BARRIER() __asm__ volatile("":::"memory")
#define XHCI_YIELD() __asm__ volatile("":::"memory")
#else
#define XHCI_BARRIER() __asm__ volatile("dsb sy":::"memory")
#define XHCI_YIELD() __asm__ volatile("yield":::"memory")
static uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static void wr(uintptr_t a,uint32_t v) { *(volatile uint32_t *)a=v; XHCI_BARRIER(); }
#endif
static void wr64(uintptr_t a,uint64_t v) { wr(a,(uint32_t)v); wr(a+4,(uint32_t)(v>>32)); }
static void zero(void *p,size_t n) { uint8_t *b=p; for(size_t i=0;i<n;++i) b[i]=0; }
#ifdef CHOS_HOST_TEST
static uint64_t mock_time;
static uint64_t counter(void) { return mock_time++; }
static uint64_t frequency(void) { return 1000; }
#else
static uint64_t counter(void) { uint64_t v; __asm__ volatile("mrs %0,cntpct_el0":"=r"(v)); return v; }
static uint64_t frequency(void) { uint64_t v; __asm__ volatile("mrs %0,cntfrq_el0":"=r"(v)); return v; }
#endif
static int expired(uint64_t start,unsigned ms) { return counter()-start >= frequency()/1000u*ms; }
static int wait_reg(uintptr_t a,uint32_t mask,uint32_t value,unsigned ms) {
    uint64_t start=counter();
    for (unsigned spins=0;spins<10000000u;++spins) {
        if ((rd(a)&mask)==value) return 1;
        if (expired(start,ms)) break;
        XHCI_YIELD();
    }
    return 0;
}
static void publish(void) { XHCI_BARRIER(); }
static void fail(const char *reason) {
    error=reason; poisoned=1;
    chos_xhci_stop();
}
static uint32_t *ctx(usb_device_t *d,unsigned index) {
    return (uint32_t *)(void *)(d->input+(index+1u)*context_bytes);
}
static void context_pointer(uint32_t *c,unsigned off,uint64_t p) { c[off]=(uint32_t)p; c[off+1]=(uint32_t)(p>>32); }
static void ring_doorbell(unsigned slot,unsigned target) { publish(); wr(doorbells+4u*slot,target); }
static uint64_t push(chos_xhci_ring_t *r,uint64_t p,uint32_t s,uint32_t c) { return chos_xhci_ring_push(r,p,s,c); }
static void post_input(usb_device_t *d,endpoint_t *e) {
    zero(e->buffer,sizeof(e->buffer));
    e->pending=push(&e->ring,(uintptr_t)e->buffer,e->iface.packet_bytes,TRB(1)|(1u<<5)|(1u<<2)); /* IOC/ISP */
    ring_doorbell(d->slot,e->dci);
}
static void drain(void) {
    for (unsigned budget=0;budget<256;++budget) {
        chos_xhci_trb_t v=events[event_head];
        unsigned type=(v.control>>10)&63, code=v.status>>24;
        publish();
        if ((v.control&1)!=event_cycle) break;
        event_head++;
        if(event_head==256) { event_head=0; event_cycle^=1; }
        wr64(runtime+0x38,(uintptr_t)&events[event_head]|8u); /* ERDP + EHB */
        if(type==32 && control_data_trb && (v.parameter&~UINT64_C(15))==control_data_trb) {
            if(code!=SUCCESS || (v.status&0xffffff)) control_short=1;
        } else if ((type==33 || type==32) && (v.parameter&~UINT64_C(15))==awaited && awaited) {
            completed=awaited; completion_status=v.status; completion_control=v.control;
            completion_parameter=v.parameter;
        } else if (type==32) {
            unsigned slot=v.control>>24, dci=(v.control>>16)&31;
            for(unsigned i=0;i<MAX_SLOTS;++i) {
                usb_device_t *d=&devices[i];
                if(!d->active || d->slot!=slot) continue;
                for(unsigned j=0;j<d->endpoint_count;++j) {
                    endpoint_t *e=&d->endpoints[j];
                    if(e->dci!=dci || !e->pending || e->pending!=(v.parameter&~UINT64_C(15))) continue;
                    e->pending=0;
                    uint32_t residual=v.status&0xffffff;
                    if ((code==SUCCESS || code==SHORT_PACKET) && residual<=e->iface.packet_bytes) {
                        if (chos_usb_decode_report(&e->layout,&e->input,e->buffer,e->iface.packet_bytes-residual)) e->malformed=0;
                        else ++e->malformed;
                        if(e->malformed<8) post_input(d,e);
                        else { chos_usb_release_input(&e->input); d->active=-1; error="USB repeated malformed reports; device quarantined"; }
                    } else {
                        chos_usb_release_input(&e->input); d->active=-1;
                        error=code==6?"USB endpoint stalled; disabling slot":"USB transfer error or invalid residual; disabling slot";
                    }
                }
            }
        } else if(type==37) { fail("xHCI host controller event error"); break; }
    }
}
static int await(uint64_t address,unsigned ms) {
    uint64_t start=counter(); awaited=address; completed=0;
    for(unsigned spin=0;spin<10000000u;++spin) {
        drain();
        if(completed==address) { awaited=0; return 1; }
        if(!running || expired(start,ms)) break;
        XHCI_YIELD();
    }
    awaited=0; fail("xHCI command/control timeout; controller quarantined"); return 0;
}
static int command(unsigned type,uint64_t p,uint32_t flags,uint32_t *result) {
    uint64_t a=push(&commands,p,0,TRB(type)|flags);
    ring_doorbell(0,0);
    if(!await(a,TIMEOUT_MS)) return 0;
    if((completion_status>>24)!=SUCCESS) { error="xHCI command completion error"; return 0; }
    if(result) *result=completion_control>>24;
    return 1;
}
static int control(usb_device_t *d,uint8_t request_type,uint8_t request,uint16_t value,
                   uint16_t index,uint16_t length) {
    uint64_t setup=request_type|((uint64_t)request<<8)|((uint64_t)value<<16)|((uint64_t)index<<32)|((uint64_t)length<<48);
    uint32_t in=(request_type&0x80)!=0;
    uint64_t status_trb;
    if(length>sizeof(d->data)) return 0;
    zero(d->data,sizeof(d->data));
    control_data_trb=0;control_short=0;
    push(&d->control,setup,8,TRB(2)|(1u<<6)|((length?(in?3u:2u):0u)<<16));
    if(length) control_data_trb=push(&d->control,(uintptr_t)d->data,length,TRB(3)|(in<<16)|(1u<<2));
    status_trb=push(&d->control,0,0,TRB(4)|(1u<<5)|((length?!in:1u)<<16));
    ring_doorbell(d->slot,1);
    int done=await(status_trb,TIMEOUT_MS);
    control_data_trb=0;
    if(!done) return 0;
    if(control_short) {error="USB short or failed control descriptor transfer";return 0;}
    if((completion_status>>24)!=SUCCESS) { error="USB control transfer failed"; return 0; }
    (void)completion_parameter;
    return 1;
}
static int remove_device(usb_device_t *d) {
    if(!d->slot) return 1;
    int was_active=d->active!=0;
    for(unsigned i=0;i<d->endpoint_count;++i) chos_usb_release_input(&d->endpoints[i].input);
    if(running && !command(10,0,(uint32_t)d->slot<<24,0)) {
        fail("xHCI Disable Slot failed; DMA storage quarantined"); return 0;
    }
    dcbaa[d->slot]=0; d->active=0; d->slot=0;
    if(was_active && online) --online;
    return 1;
}
static int enumerate(unsigned port) {
    usb_device_t *d=0;
    uint32_t portsc,slot;
    size_t count=0;
    chos_usb_hid_interface_t interfaces[CHOS_USB_MAX_HID];
    for(unsigned i=0;i<slot_limit;++i) if(!devices[i].slot) { d=&devices[i]; break; }
    if(!d) { error="USB device capacity reached"; return 0; }
    portsc=rd(op+0x400+16u*(port-1));
    if(!(portsc&PORT_CCS)) return 0;
    if(!(portsc&PORT_PED)) {
        wr(op+0x400+16u*(port-1),(portsc&PORT_PRESERVE)|PORT_PR);
        if(!wait_reg(op+0x400+16u*(port-1),PORT_PR,0,250) ||
           !(rd(op+0x400+16u*(port-1))&PORT_PED)) { error="USB port reset failed"; return 0; }
    }
    portsc=rd(op+0x400+16u*(port-1));
    unsigned speed=(portsc>>10)&15;
    if(speed<1 || speed>4) { error="USB unsupported port speed"; return 0; }
    zero(d,sizeof(*d)); d->port=(uint8_t)port; d->speed=(uint8_t)speed;
    if(!command(9,0,0,&slot)) return 0;
    if(!slot || slot>slot_limit) { fail("xHCI invalid allocated slot"); return 0; }
    d->slot=(uint8_t)slot;
    chos_xhci_ring_init(&d->control);
    dcbaa[slot]=(uintptr_t)d->output;
    uint32_t *icc=(uint32_t *)(void *)d->input, *sc=ctx(d,0), *ep=ctx(d,1);
    icc[1]=3; sc[0]=(speed<<20)|(1u<<27); sc[1]=port<<16;
    ep[1]=(3u<<1)|(4u<<3)|((speed==4?512u:(speed==3?64u:8u))<<16);
    context_pointer(ep,2,(uintptr_t)d->control.trbs|1); ep[4]=8;
    if(!command(11,(uintptr_t)d->input,slot<<24,0)) goto bad;
    if(!control(d,0x80,6,0x100,0,18)) goto bad;
    if(d->data[0]!=18 || d->data[1]!=1 || d->data[4]==9) { error="USB invalid device descriptor or unsupported hub"; goto bad; }
    unsigned packet=d->data[7];
    if(speed==4) packet=packet==9?512:0;
    if((speed==1 && packet!=8 && packet!=16 && packet!=32 && packet!=64) ||
       (speed==2 && packet!=8) || (speed==3 && packet!=64) || !packet || packet>512) { error="USB invalid EP0 packet size"; goto bad; }
    if(speed==1 && packet!=8) {
        zero(d->input,sizeof(d->input)); icc[1]=2;
        ep=ctx(d,1); ep[1]=(3u<<1)|(4u<<3)|(packet<<16);
        context_pointer(ep,2,(uintptr_t)d->control.trbs|1); ep[4]=8;
        if(!command(13,(uintptr_t)d->input,slot<<24,0)) goto bad;
    }
    if(!control(d,0x80,6,0x200,0,9)) goto bad;
    unsigned config_bytes=d->data[2]|((unsigned)d->data[3]<<8);
    if(config_bytes<9 || config_bytes>sizeof(d->data)) { error="USB configuration length out of bounds"; goto bad; }
    if(!control(d,0x80,6,0x200,0,(uint16_t)config_bytes)) goto bad;
    if(!chos_usb_parse_config(d->data,config_bytes,interfaces,&count)) { error="USB unsupported or malformed configuration (direct HID required)"; goto bad; }
    unsigned configuration=d->data[5];
    if(!control(d,0,9,(uint16_t)configuration,0,0)) goto bad;
    /* Build input context from the controller-owned output context only while
     * no endpoint TD is active. Add the slot and interrupt-IN endpoints. */
    zero(d->input,sizeof(d->input)); icc[1]=1;
    for(unsigned i=0;i<context_bytes/4;++i) ctx(d,0)[i]=((uint32_t *)(void *)d->output)[i];
    unsigned highest=1;
    for(unsigned i=0;i<count;++i) {
        endpoint_t *e=&d->endpoints[i]; e->iface=interfaces[i];
        e->dci=(uint8_t)(2u*(interfaces[i].endpoint&15u)+1u);
        if(speed>=3 && interfaces[i].interval>16) { error="USB interrupt interval invalid"; goto bad; }
        unsigned interval=interfaces[i].interval;
        if(speed<=2) { unsigned v=8u*interval; interval=0; while(v>1) {v>>=1; ++interval;} }
        else --interval;
        chos_xhci_ring_init(&e->ring);
        ep=ctx(d,e->dci); ep[0]=interval<<16;
        ep[1]=(3u<<1)|(7u<<3)|((uint32_t)e->iface.packet_bytes<<16);
        context_pointer(ep,2,(uintptr_t)e->ring.trbs|1);
        ep[4]=e->iface.packet_bytes|((uint32_t)e->iface.packet_bytes<<16);
        icc[1]|=1u<<e->dci; if(e->dci>highest) highest=e->dci;
        if(e->iface.boot_protocol && !control(d,0x21,11,1,e->iface.interface,0)) goto bad;
        if(!control(d,0x81,6,0x2200,e->iface.interface,e->iface.report_bytes)) goto bad;
        if(chos_hid_parse_report_descriptor(d->data,e->iface.report_bytes,&e->layout)!=CHOS_HID_OK ||
           (!e->layout.has_keyboard && !e->layout.has_pointer && !e->layout.has_mouse)) { error="USB unsupported HID report descriptor"; goto bad; }
        for(size_t field=0;field<e->layout.field_count;++field) {
            const chos_hid_field_t *f=&e->layout.fields[field];
            if(f->usage_page==1 && (f->usage==0x30 || f->usage==0x31) &&
               !(f->flags&CHOS_HID_FIELD_RELATIVE)) {
                error="USB absolute pointer reports unsupported";goto bad;
            }
        }
        /* Default protocol is report protocol. SET_IDLE is optional and often
         * stalls on otherwise valid devices: don't require it or switch to boot
         * protocol, which would invalidate the report descriptor's layout. */
        for(unsigned id=0;id<256;++id) if((e->layout.input_bits[id]+7u)/8u+(e->layout.uses_report_ids?1u:0u)>e->iface.packet_bytes) {
            error="USB HID report exceeds endpoint packet"; goto bad;
        }
    }
    sc=ctx(d,0); sc[0]=(sc[0]&~(31u<<27))|(highest<<27);
    if(!command(12,(uintptr_t)d->input,slot<<24,0)) goto bad;
    d->endpoint_count=(uint8_t)count; d->active=1; ++online;
    for(unsigned i=0;i<count;++i) post_input(d,&d->endpoints[i]);
    error="xHCI direct-attached HID input online"; return 1;
bad:
    (void)remove_device(d); return 0;
}
void chos_xhci_stop(void) {
    if(!mmio) return;
    running=0;
    for(unsigned i=0;i<MAX_SLOTS;++i) {
        for(unsigned j=0;j<devices[i].endpoint_count;++j) chos_usb_release_input(&devices[i].endpoints[j].input);
        devices[i].active=0;
    }
    wr(op,rd(op)&~1u);
    (void)wait_reg(op+4,1,1,100);
    /* Disable bus mastering even if the controller refuses to halt. Never
     * recycle backing storage after an uncertain stop. */
    uint32_t value;
    if(chos_pci_read_config32(saved_boot,saved_acpi,saved_pci.segment,saved_pci.bus,saved_pci.device,saved_pci.function,4,&value))
        (void)chos_pci_write_config32(saved_boot,saved_acpi,saved_pci.segment,saved_pci.bus,saved_pci.device,saved_pci.function,4,(value&0xffffu)&~4u);
    online=0;
}
int chos_xhci_init(const chos_boot_info_t *boot,const chos_acpi_info_t *acpi,const chos_pci_device_t *pci) {
    uint64_t base,bar_bytes,sctlr;
    if(running) return 1;
    if(!acpi || acpi->dma_translation_unverified) { error="xHCI DMA/IOMMU topology unsupported";return 0; }
    if(poisoned) {error="xHCI quarantined until reboot";return 0;}
#ifdef CHOS_HOST_TEST
    sctlr=0;
#else
    __asm__ volatile("mrs %0,sctlr_el1":"=r"(sctlr));
#endif
    if(sctlr&((1u<<0)|(1u<<2))) { error="xHCI requires EFI identity DMA with MMU and data cache disabled";return 0; }
    if(!pci || pci->class_code!=12 || pci->subclass!=3 || pci->programming_interface!=0x30 ||
       (pci->header_type&127)!=0 || !chos_pci_map_bar(boot,acpi,pci,0,&base,&bar_bytes) || bar_bytes<0x1000) { error="xHCI BAR lacks validated firmware resource coverage";return 0; }
    if(!chos_pci_enable_memory_busmaster(boot,acpi,pci)) {error="xHCI PCI enable failed";return 0;}
    saved_boot=boot; saved_acpi=acpi; saved_pci=*pci; mmio=(uintptr_t)base;
    uint32_t cap=rd(mmio),hcs=rd(mmio+4),hcs2=rd(mmio+8),hcc=rd(mmio+16);
    uint32_t db=rd(mmio+20)&~3u,rt=rd(mmio+24)&~31u;
    op=mmio+(cap&255);
    if((cap&255)<0x20 || (cap>>16)<0x100 || (cap>>16)>0x120 ||
       db<0x100 || db>bar_bytes-4u*(MAX_SLOTS+1u) || rt<0x100 || rt>bar_bytes-0x40 ||
       !(hcs&255) || !(hcs>>24) || (hcs>>24)>MAX_PORTS ||
       !(rd(op+8)&1u)) { fail("xHCI invalid/unsupported capabilities");return 0; }
    if (!(hcc&1u) && (uintptr_t)(devices+MAX_SLOTS)>UINT32_MAX) {
        fail("xHCI 32-bit DMA cannot address kernel buffers");return 0;
    }
    context_bytes=(hcc&4)?64:32; port_count=hcs>>24;
    slot_limit=(hcs&255)<MAX_SLOTS?(hcs&255):MAX_SLOTS;
    doorbells=mmio+db;runtime=mmio+rt;
    /* Bounded BIOS/OS ownership handoff and validation of extended capability
     * links. Unknown capabilities are skipped only inside the mapped BAR. */
    uint32_t ext=hcc>>16;
    for(unsigned budget=0;ext && budget<256;++budget) {
        if(ext>(bar_bytes-8)/4) {fail("xHCI extended capability outside BAR");return 0;}
        uintptr_t a=mmio+ext*4u; uint32_t v=rd(a);
        if((v&255)==2) {
            if(ext*4u>bar_bytes-16 || ((v>>24)!=2 && (v>>24)!=3) ||
               (rd(a+8)>>28)!=0) {
                fail("xHCI custom protocol speed IDs unsupported");return 0;
            }
        }
        if((v&255)==1) {
            wr(a,v|(1u<<24));
            if(!wait_reg(a,1u<<16,0,TIMEOUT_MS)) {fail("xHCI firmware ownership timeout");return 0;}
            wr(a+4,0); /* disable legacy SMI enables; no status W1C side effects */
        }
        unsigned next=(v>>8)&255;
        if(!next) {ext=0;break;}
        ext+=next;
    }
    if(ext) {fail("xHCI extended capability chain too long");return 0;}
    wr(op,rd(op)&~1u);
    if(!wait_reg(op+4,1,1,100)) {fail("xHCI halt timeout");return 0;}
    wr(op,2);
    if(!wait_reg(op,2,0,TIMEOUT_MS) || !wait_reg(op+4,1u<<11,0,TIMEOUT_MS)) {fail("xHCI reset/CNR timeout");return 0;}
    unsigned scratch_count=((hcs2>>27)&31)|((hcs2>>16)&0x3e0);
    if(scratch_count>16) {fail("xHCI scratchpad requirement exceeds bound");return 0;}
    zero(devices,sizeof(devices));zero(events,sizeof(events));zero(dcbaa,sizeof(dcbaa));
    zero(scratch,sizeof(scratch));zero(scratch_array,sizeof(scratch_array));zero(port_attempted,sizeof(port_attempted));
    chos_xhci_ring_init(&commands);event_head=0;event_cycle=1;online=0;port_cursor=0;
    for(unsigned i=0;i<scratch_count;++i) scratch_array[i]=(uintptr_t)scratch[i];
    if(scratch_count) dcbaa[0]=(uintptr_t)scratch_array;
    erst[0]=(uintptr_t)events;erst[1]=256;
    wr(op+0x38,slot_limit);wr64(op+0x30,(uintptr_t)dcbaa);wr64(op+0x18,(uintptr_t)commands.trbs|1);
    wr(runtime+0x20,0); /* interrupter disabled: polled event ring */
    wr(runtime+0x28,1);wr64(runtime+0x30,(uintptr_t)erst);wr64(runtime+0x38,(uintptr_t)events);
    publish();wr(op,1);running=1;
    if(!wait_reg(op+4,1,0,100) || !command(23,0,0,0)) {fail("xHCI run/no-op failed");return 0;}
    for(unsigned p=1;p<=port_count && running;++p) {
        port_attempted[p-1]=1;
        (void)enumerate(p);
    }
    return running;
}
void chos_xhci_poll(void) {
    if(!running) return;
    if(rd(op+4)&((1u<<2)|(1u<<12))) { fail("xHCI host system/controller error");return; }
    drain();
    if(!running) return;
    /* At most one root port transition per pump. NAK/no-input produces no
     * event and requires no spinning, allocation, retries or tracing. */
    unsigned p=port_cursor++%port_count+1;
    uint32_t ps=rd(op+0x400+16u*(p-1));
    for(unsigned i=0;i<MAX_SLOTS;++i) if(devices[i].slot && devices[i].port==p &&
            (!(ps&PORT_CCS) || devices[i].active<0)) { if(!remove_device(&devices[i])) return; }
    if(!(ps&PORT_CCS)) port_attempted[p-1]=0;
    else if(!port_attempted[p-1]) {port_attempted[p-1]=1;(void)enumerate(p);}
    if(ps&PORT_CHANGE) wr(op+0x400+16u*(p-1),(ps&PORT_PRESERVE)|(ps&PORT_CHANGE));
}
const char *chos_xhci_error(void) {return error;}
unsigned chos_xhci_device_count(void) {return online;}
