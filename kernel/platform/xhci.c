/* Polled xHCI 1.x, identity DMA with MMU/caches off after EFI handoff.
 * Bounded USB root/hub topology with HID endpoints. No firmware services or fixed PCI IDs.
 * DMA storage is permanent: on controller failure never free memory still
 * owned by hardware. Single bootstrap/GUI task is the sole producer. */
#include "chos/xhci.h"
#include "chos/usb.h"
#include "chos/xhci_compat.h"
#define MAX_SLOTS 16u
#define MAX_PORTS 255u
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
#define INPUT_RECEIVES 8u
#define EVENT_TRBS 1024u

typedef struct {
    chos_xhci_ring_t ring;
    uint8_t buffer[INPUT_RECEIVES][CHOS_USB_REPORT_BYTES] __attribute__((aligned(1024)));
    chos_hid_report_layout_t layout;
    chos_usb_input_state_t input;
    chos_usb_hid_interface_t iface;
    uint64_t pending[INPUT_RECEIVES];
    unsigned malformed;
    uint8_t dci;
} endpoint_t;
typedef struct {
    uint8_t output[SLOT_BYTES] __attribute__((aligned(4096)));
    uint8_t input[SLOT_BYTES+64] __attribute__((aligned(4096)));
    chos_xhci_ring_t control;
    uint8_t data[4096] __attribute__((aligned(4096)));
    endpoint_t endpoints[CHOS_USB_MAX_HID];
    uint8_t port, slot, speed, endpoint_count;
    uint8_t parent, parent_port, depth, hub, hub_ports, tt_think;
    uint16_t vendor,product,received;
    uint32_t route;
    uint64_t last_absolute;
    unsigned absolute_valid;
    uint64_t hub_changes, hub_ready, port_ready[32], port_deadline[32];
    uint8_t port_phase[32], hub_status_pending;
    int active;
} usb_device_t;
static usb_device_t devices[MAX_SLOTS] __attribute__((aligned(4096)));
static chos_xhci_ring_t commands __attribute__((aligned(4096)));
static chos_xhci_trb_t events[EVENT_TRBS] __attribute__((aligned(16384)));
static uint64_t dcbaa[256] __attribute__((aligned(4096)));
static uint64_t erst[2] __attribute__((aligned(64)));
static uint64_t scratch_array[16] __attribute__((aligned(4096)));
static uint8_t scratch[16][4096] __attribute__((aligned(4096)));
static uintptr_t mmio, op, runtime, doorbells;
static uint32_t context_bytes, port_count, slot_limit;
static unsigned event_head, event_cycle, online, port_cursor;
static uint64_t awaited, completed, completion_parameter, control_data_trb;
static int control_short,control_data_done;
static uint32_t control_requested,control_received;
static uint8_t control_slot;
static uint32_t completion_status, completion_control;
static int running, poisoned;
static uint8_t port_attempted[MAX_PORTS];
static unsigned hub_cursor;
static void (*logger)(const char *);
void chos_xhci_set_logger(void (*write)(const char *)) {logger=write;}
static void log_number(uint64_t v) {
    if(!logger)return;
    static const char hex[]="0123456789abcdef";char s[19]={'0','x'};
    for(unsigned i=0;i<16;++i)s[2+i]=hex[(v>>((15-i)*4))&15];
    s[18]=0;logger(s);
}
static void log_device(usb_device_t *d,const char *status) {
    if(!logger)return;
    logger("[USB] root=");log_number(d->port);logger(" route=");log_number(d->route);
    logger(" slot=");log_number(d->slot);logger(" VID=");log_number(d->vendor);
    logger(" PID=");log_number(d->product);logger(" ");logger(status);logger("\n");
}
static const char *error="xHCI not probed";
static chos_xhci_diagnostic_t diagnostic;
const chos_xhci_diagnostic_t *chos_xhci_diagnostics(void){return &diagnostic;}
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
    error=reason; diagnostic.failed_stage=diagnostic.stage;diagnostic.last_failure=reason;poisoned=1;
    chos_xhci_stop();
}
static uint32_t *ctx(usb_device_t *d,unsigned index) {
    return (uint32_t *)(void *)(d->input+(index+1u)*context_bytes);
}
static void context_pointer(uint32_t *c,unsigned off,uint64_t p) { c[off]=(uint32_t)p; c[off+1]=(uint32_t)(p>>32); }
static void ring_doorbell(unsigned slot,unsigned target) { publish(); wr(doorbells+4u*slot,target); }
static uint64_t push(chos_xhci_ring_t *r,uint64_t p,uint32_t s,uint32_t c) { return chos_xhci_ring_push(r,p,s,c); }
static void post_input(usb_device_t *d,endpoint_t *e,unsigned receive) {
    zero(e->buffer[receive],sizeof(e->buffer[receive]));
    e->pending[receive]=push(&e->ring,(uintptr_t)e->buffer[receive],e->iface.packet_bytes,TRB(1)|(1u<<5)|(1u<<2)); /* IOC/ISP */
    if(!e->pending[receive]){fail("xHCI invalid input ring state");return;}
    ring_doorbell(d->slot,e->dci);
}
static void drain(void) {
    for (unsigned budget=0;budget<EVENT_TRBS;++budget) {
        chos_xhci_trb_t v=events[event_head];
        unsigned type=(v.control>>10)&63, code=v.status>>24;
        publish();
        if ((v.control&1)!=event_cycle) break;
        event_head++;
        if(event_head==EVENT_TRBS) { event_head=0; event_cycle^=1; }
        wr64(runtime+0x38,(uintptr_t)&events[event_head]|8u); /* ERDP + EHB */
        if(type==32 && control_data_trb && v.control>>24==control_slot &&
           ((v.control>>16)&31)==1 && (v.parameter&~UINT64_C(15))==control_data_trb) {
            uint32_t residual=v.status&0xffffff;
            diagnostic.completion=(uint16_t)code;
            control_data_done=1;
            if((code!=SUCCESS && code!=SHORT_PACKET) || residual>control_requested) {
                control_short=1;completed=awaited;completion_status=v.status;
            } else control_received=control_requested-residual;
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
                    if(e->dci!=dci)continue;
                    unsigned receive=0;
                    while(receive<INPUT_RECEIVES && e->pending[receive]!=(v.parameter&~UINT64_C(15)))++receive;
                    if(receive==INPUT_RECEIVES)continue;
                    e->pending[receive]=0;
                    uint32_t residual=v.status&0xffffff;
                    if ((code==SUCCESS || code==SHORT_PACKET) && residual<=e->iface.packet_bytes) {
                        if(d->hub) {
                            unsigned bytes=e->iface.packet_bytes-residual;
                            if(bytes<(unsigned)(d->hub_ports+1+7)/8) ++e->malformed;
                            else {
                                uint64_t bitmap=0;
                                for(unsigned k=0;k<bytes && k<8;++k)bitmap|=(uint64_t)e->buffer[receive][k]<<(8*k);
                                d->hub_changes|=bitmap>>1;
                                if(bitmap&1)d->hub_status_pending=1;
                                e->malformed=0;
                            }
                        } else {
                            e->input.suppress_relative=d->absolute_valid && counter()-d->last_absolute<frequency()/4u;
                            if(chos_usb_decode_report(&e->layout,&e->input,e->buffer[receive],e->iface.packet_bytes-residual)) {
                                e->malformed=0;
                                if(e->input.absolute_seen){d->absolute_valid=1;d->last_absolute=counter();}
                            } else ++e->malformed;
                        }
                        if(e->malformed<8) post_input(d,e,receive);
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
static int await(uint64_t address,unsigned ms,int controller_fatal) {
    uint64_t start=counter(); awaited=address; completed=0;
    for(unsigned spin=0;spin<10000000u;++spin) {
        drain();
        if(completed==address) { awaited=0; return 1; }
        if(!running || expired(start,ms)) break;
        XHCI_YIELD();
    }
    awaited=0;
    if(controller_fatal)fail("xHCI command timeout; controller quarantined");
    else error="USB control timeout; cancelling slot";
    return 0;
}
static int command(unsigned type,uint64_t p,uint32_t flags,uint32_t *result) {
    uint64_t a=push(&commands,p,0,TRB(type)|flags);
    if(!a){fail("xHCI invalid command ring state");return 0;}
    ring_doorbell(0,0);
    if(!await(a,TIMEOUT_MS,1)) return 0;
    if((completion_status>>24)!=SUCCESS) { error="xHCI command completion error"; return 0; }
    if(result) *result=completion_control>>24;
    return 1;
}
#ifdef CHOS_HOST_TEST
static int (*test_control)(usb_device_t *,uint8_t,uint8_t,uint16_t,uint16_t,uint16_t);
#endif
static int control(usb_device_t *d,uint8_t request_type,uint8_t request,uint16_t value,
                   uint16_t index,uint16_t length) {
#ifdef CHOS_HOST_TEST
    if(test_control){d->received=length;return test_control(d,request_type,request,value,index,length);}
#endif
    uint64_t setup=request_type|((uint64_t)request<<8)|((uint64_t)value<<16)|((uint64_t)index<<32)|((uint64_t)length<<48);
    uint32_t in=(request_type&0x80)!=0;
    uint64_t status_trb;
    if(length>sizeof(d->data)) return 0;
    zero(d->data,sizeof(d->data));
    diagnostic.descriptor=value;diagnostic.requested=length;diagnostic.received=0;
    d->received=0;control_data_trb=0;control_short=0;control_data_done=!length;
    control_requested=length;control_received=0;control_slot=d->slot;
    if(!push(&d->control,setup,8,TRB(2)|(1u<<6)|((length?(in?3u:2u):0u)<<16))) {error="USB invalid control ring state";return 0;}
    if(length) control_data_trb=push(&d->control,(uintptr_t)d->data,length,TRB(3)|(in<<16)|(1u<<2)|(1u<<5));
    status_trb=push(&d->control,0,0,TRB(4)|(1u<<5)|((length?!in:1u)<<16));
    if(!status_trb || (length && !control_data_trb)) {error="USB invalid control ring state";return 0;}
    ring_doorbell(d->slot,1);
    int done=await(status_trb,TIMEOUT_MS,0);
    control_data_trb=0;
    if(!done) return 0;
    if(control_short || !control_data_done || (length && control_received!=length)) {
        error="USB control data length/completion mismatch";return 0;
    }
    if((completion_status>>24)!=SUCCESS) { error="USB control transfer failed"; return 0; }
    d->received=(uint16_t)control_received;diagnostic.received=d->received;
    (void)completion_parameter;
    return 1;
}
static int remove_device(usb_device_t *d) {
    if(!d->slot) return 1;
    int was_active=d->active!=0;
    d->active=0;
    /* Disable children first: a hub must not disappear while its downstream
     * slots still own DMA or held keyboard/button state. */
    unsigned parent=(unsigned)(d-devices)+1;
    for(unsigned i=0;i<MAX_SLOTS;++i) if(devices[i].slot && devices[i].parent==parent)
        if(!remove_device(&devices[i]))return 0;
    for(unsigned i=0;i<d->endpoint_count;++i) chos_usb_release_input(&d->endpoints[i].input);
    if(running && !command(10,0,(uint32_t)d->slot<<24,0)) {
        fail("xHCI Disable Slot failed; DMA storage quarantined"); return 0;
    }
    dcbaa[d->slot]=0; d->active=0; d->slot=0;
    if(was_active && !d->hub && online) --online;
    return 1;
}
static int attach_device(unsigned port,unsigned speed,unsigned parent,unsigned downstream) {
    usb_device_t *d=0;
    uint32_t slot;
    size_t count=0;
    chos_usb_hid_interface_t interfaces[CHOS_USB_MAX_HID];
    for(unsigned i=0;i<slot_limit;++i)if(!devices[i].slot){d=&devices[i];break;}
    if(!d){error="USB device capacity reached";return 0;}
    if(speed<1 || speed>4 || (parent && (parent>MAX_SLOTS || devices[parent-1].depth>=5))) {
        error="USB unsupported speed or topology depth";return 0;
    }
    diagnostic.stage=11;diagnostic.port=(uint16_t)port;
    zero(d,sizeof(*d));d->port=(uint8_t)port;d->speed=(uint8_t)speed;
    d->parent=(uint8_t)parent;d->parent_port=(uint8_t)downstream;
    if(parent) {
        usb_device_t *h=&devices[parent-1];d->depth=h->depth+1;
        d->route=h->route|((downstream>15?15u:downstream)<<(h->depth*4u));
    }
    diagnostic.route=d->route;
    if(!command(9,0,0,&slot)) return 0;
    if(!slot || slot>slot_limit) { fail("xHCI invalid allocated slot"); return 0; }
    d->slot=(uint8_t)slot;
    chos_xhci_ring_init(&d->control);
    dcbaa[slot]=(uintptr_t)d->output;
    uint32_t *icc=(uint32_t *)(void *)d->input, *sc=ctx(d,0), *ep=ctx(d,1);
    icc[1]=3;sc[0]=d->route|(speed<<20)|(1u<<27);sc[1]=port<<16;
    if(parent && speed<=2) {
        usb_device_t *h=&devices[parent-1];
        if(h->speed==3)sc[2]=h->slot|(downstream<<8);
        else sc[2]=((uint32_t *)(void *)h->output)[2]&0xffffu;
    }
    ep[1]=(3u<<1)|(4u<<3)|((speed==4?512u:(speed==3?64u:8u))<<16);
    context_pointer(ep,2,(uintptr_t)d->control.trbs|1); ep[4]=8;
    if(!command(11,(uintptr_t)d->input,slot<<24,0)) goto bad;
    /* Learn EP0's packet size from the standard eight-byte prefix before
     * requesting a descriptor spanning multiple packets. Fusion's virtual USB
     * devices exercise this sequence; QEMU previously tolerated the mismatch. */
    if(!control(d,0x80,6,0x100,0,8)) goto bad;
    if(d->data[0]!=18 || d->data[1]!=1) {error="USB malformed device descriptor prefix";goto bad;}
    unsigned packet=d->data[7];
    if(speed==4) packet=packet==9?512:0;
    if((speed==1 && packet!=8 && packet!=16 && packet!=32 && packet!=64) ||
       (speed==2 && packet!=8) || (speed==3 && packet!=64) || !packet || packet>512) { error="USB invalid EP0 packet size"; goto bad; }
    diagnostic.stage=12;
    if(speed==1 && packet!=8) {
        zero(d->input,sizeof(d->input)); icc[1]=2;
        ep=ctx(d,1);
        for(unsigned k=0;k<context_bytes/4;++k)ep[k]=((uint32_t *)(void *)(d->output+context_bytes))[k];
        ep[0]&=~7u; /* Endpoint State is output-only, reserved in an input context. */
        ep[1]=(ep[1]&0xffffu)|(packet<<16);
        if(!command(13,(uintptr_t)d->input,slot<<24,0)) goto bad;
    }
    diagnostic.stage=13;
    if(!control(d,0x80,6,0x100,0,18))goto bad;
    if(d->data[0]!=18 || d->data[1]!=1 || !d->data[17]) {error="USB malformed device descriptor";goto bad;}
    d->vendor=(uint16_t)(d->data[8]|(uint16_t)d->data[9]<<8);
    d->product=(uint16_t)(d->data[10]|(uint16_t)d->data[11]<<8);
    d->hub=d->data[4]==9;
    diagnostic.stage=14;
    if(!control(d,0x80,6,0x200,0,9)) goto bad;
    unsigned config_bytes=d->data[2]|((unsigned)d->data[3]<<8);
    if(config_bytes<9 || config_bytes>sizeof(d->data)) { error="USB configuration length out of bounds"; goto bad; }
    diagnostic.stage=15;
    if(!control(d,0x80,6,0x200,0,(uint16_t)config_bytes)) goto bad;
    unsigned configuration=d->data[5];
    if(d->hub || !chos_usb_parse_config(d->data,config_bytes,interfaces,&count)) {
        if(!chos_usb_parse_hub_config(d->data,config_bytes,&interfaces[0])) {
            error="USB unsupported or malformed HID/hub configuration";goto bad;
        }
        d->hub=1;count=1;
    }
    if(!control(d,0,9,(uint16_t)configuration,0,0)) goto bad;
    if(d->hub) {
        unsigned descriptor=speed==4?0x2a00:0x2900;
        if(!control(d,0xa0,6,(uint16_t)descriptor,0,7))goto bad;
        unsigned bytes=d->data[0];
        if(bytes<7 || bytes>64 || !d->data[2] || d->data[2]>32 || d->data[1]!=(descriptor>>8)) {
            error="USB malformed hub descriptor";goto bad;
        }
        if(!control(d,0xa0,6,(uint16_t)descriptor,0,(uint16_t)bytes))goto bad;
        d->hub_ports=d->data[2];d->tt_think=(d->data[3]>>5)&3;
        d->hub_ready=counter()+frequency()/1000u*((unsigned)d->data[5]*2u+20u);
        if(speed==4 && !control(d,0x20,12,d->depth,0,0))goto bad;
    }
    /* Build input context from the controller-owned output context only while
     * no endpoint TD is active. Add the slot and interrupt-IN endpoints. */
    zero(d->input,sizeof(d->input)); icc[1]=1;
    for(unsigned i=0;i<context_bytes/4;++i) ctx(d,0)[i]=((uint32_t *)(void *)d->output)[i];
    ctx(d,0)[3]=0; /* Output-only USB address/Slot State must never enter input contexts. */
    if(d->hub) {
        ctx(d,0)[0]|=1u<<26;
        ctx(d,0)[1]=(ctx(d,0)[1]&0xffffffu)|((uint32_t)d->hub_ports<<24);
        if(speed==3)ctx(d,0)[2]|=(uint32_t)d->tt_think<<16;
    }
    unsigned highest=1,accepted=0;
    for(unsigned i=0;i<count;++i) {
        endpoint_t *e=&d->endpoints[accepted]; e->iface=interfaces[i];
        if(!d->hub) {
            if(e->iface.boot_protocol && !control(d,0x21,11,1,e->iface.interface,0))goto bad;
            diagnostic.stage=18;
            if(!control(d,0x81,6,0x2200,e->iface.interface,e->iface.report_bytes))goto bad;
            if(chos_hid_parse_report_descriptor(d->data,e->iface.report_bytes,&e->layout)!=CHOS_HID_OK ||
               (!e->layout.has_keyboard && !e->layout.has_pointer && !e->layout.has_mouse)) {
                log_device(d,"unclaimed HID interface: report layout outside input policy");continue;
            }
            int fits=1;
            for(unsigned id=0;id<256;++id)if((e->layout.input_bits[id]+7u)/8u+(e->layout.uses_report_ids?1u:0u)>e->iface.packet_bytes)fits=0;
            if(!fits){log_device(d,"unclaimed HID interface: report exceeds endpoint packet");continue;}
        }
        e->dci=(uint8_t)(2u*(interfaces[i].endpoint&15u)+1u);
        if((speed==2 && interfaces[i].packet_bytes>8) || (speed==1 && interfaces[i].packet_bytes>64)) {
            error="USB endpoint packet exceeds port speed limit";goto bad;
        }
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
        ++accepted;
    }
    if(!accepted){error="USB no accepted HID interface";goto bad;}
    count=accepted;
    sc=ctx(d,0); sc[0]=(sc[0]&~(31u<<27))|(highest<<27);
    diagnostic.stage=19;
    if(!command(12,(uintptr_t)d->input,slot<<24,0)) goto bad;
    d->endpoint_count=(uint8_t)count;d->active=1;
    if(d->hub) {
        for(unsigned p=1;p<=d->hub_ports;++p)if(!control(d,0x23,3,8,(uint16_t)p,0))goto bad;
        d->hub_changes=d->hub_ports==32?UINT32_MAX:((UINT64_C(1)<<d->hub_ports)-1);
        log_device(d,"hub online");
    } else {++online;log_device(d,"HID online");}
    for(unsigned i=0;i<count;++i)for(unsigned receive=0;receive<INPUT_RECEIVES;++receive)post_input(d,&d->endpoints[i],receive);
    diagnostic.stage=20;error="xHCI USB topology running";return 1;
bad:
    diagnostic.failed_stage=diagnostic.stage;diagnostic.last_failure=error;
    log_device(d,error);(void)remove_device(d);return 0;
}
static int enumerate(unsigned port) {
    uintptr_t address=op+0x400+16u*(port-1);
    uint32_t ps=rd(address);
    if(!(ps&PORT_CCS))return 0;
    if(!(ps&PORT_PED)) {
        wr(address,(ps&PORT_PRESERVE)|PORT_PR);
        if(!wait_reg(address,PORT_PR,0,250) || !(rd(address)&PORT_PED)) {
            error="USB root port reset failed";return 0;
        }
    }
    return attach_device(port,(rd(address)>>10)&15,0,0);
}
static usb_device_t *hub_child(usb_device_t *h,unsigned port) {
    for(unsigned i=0;i<MAX_SLOTS;++i)if(devices[i].slot && devices[i].parent==(unsigned)(h-devices)+1 && devices[i].parent_port==port)return &devices[i];
    return 0;
}
static void poll_hubs(void) {
    for(unsigned tries=0;tries<MAX_SLOTS;++tries) {
        usb_device_t *h=&devices[hub_cursor++%MAX_SLOTS];
        if(!h->active || !h->hub || counter()<h->hub_ready)continue;
        if(h->hub_status_pending) {
            h->hub_status_pending=0;
            if(!control(h,0xa0,0,0,0,4)){h->active=-1;return;}
            uint16_t changes=(uint16_t)(h->data[2]|(uint16_t)h->data[3]<<8);
            for(unsigned bit=0;bit<2;++bit)if(changes&(1u<<bit))
                if(!control(h,0x20,1,(uint16_t)bit,0,0)){h->active=-1;return;}
            return;
        }
        for(unsigned port=1;port<=h->hub_ports;++port) {
            uint64_t bit=UINT64_C(1)<<(port-1);
            if(!(h->hub_changes&bit) || counter()<h->port_ready[port-1])continue;
            h->hub_changes&=~bit;
            if(!control(h,0xa3,0,0,(uint16_t)port,4)){h->active=-1;return;}
            unsigned status=h->data[0]|(unsigned)h->data[1]<<8;
            unsigned changes=h->data[2]|(unsigned)h->data[3]<<8;
            /* Acknowledge only advertised port change bits, never write back
             * the status word (which has separate feature semantics). */
            for(unsigned k=0;k<5;++k)if(changes&(1u<<k))
                if(!control(h,0x23,1,(uint16_t)(16+k),(uint16_t)port,0)){h->active=-1;return;}
            usb_device_t *child=hub_child(h,port);
            if(!(status&1u)) {
                h->port_phase[port-1]=0;
                if(child)(void)remove_device(child);
                return;
            }
            if(status&8u) { /* Overcurrent: remove DMA owners and power down. */
                if(child)(void)remove_device(child);
                (void)control(h,0x23,1,8,(uint16_t)port,0);
                h->port_phase[port-1]=3;error="USB hub port overcurrent";return;
            }
            if(child && (changes&1u)) {(void)remove_device(child);child=0;h->port_phase[port-1]=0;}
            if(child)return;
            unsigned phase=h->port_phase[port-1];
            if(phase==3 && !(changes&1))return;
            if(phase==0 || phase==3) {
                h->port_phase[port-1]=1;h->port_ready[port-1]=counter()+frequency()/50;
                h->hub_changes|=bit;return;
            }
            if(phase==1) {
                if(!control(h,0x23,3,4,(uint16_t)port,0)){h->active=-1;return;}
                h->port_phase[port-1]=2;h->port_deadline[port-1]=counter()+frequency()/2;
                h->port_ready[port-1]=counter()+frequency()/100;h->hub_changes|=bit;return;
            }
            if((status&2u) && !(status&16u)) {
                unsigned speed=h->speed==4?4u:((status&0x400)?3u:((status&0x200)?2u:1u));
                h->port_phase[port-1]=3; /* retry only after a connection change */
                (void)attach_device(h->port,speed,(unsigned)(h-devices)+1,port);return;
            }
            if(counter()>=h->port_deadline[port-1]) {h->port_phase[port-1]=3;error="USB downstream reset timeout";return;}
            h->port_ready[port-1]=counter()+frequency()/100;h->hub_changes|=bit;return;
        }
    }
}

void chos_xhci_stop(void) {
    if(!mmio) return;
    running=0;
    for(unsigned i=0;i<MAX_SLOTS;++i) {
        for(unsigned j=0;j<devices[i].endpoint_count;++j) chos_usb_release_input(&devices[i].endpoints[j].input);
        devices[i].active=0;
    }
    if(op) {wr(op,rd(op)&~1u);(void)wait_reg(op+4,1,1,100);}
    /* Disable bus mastering even if the controller refuses to halt. Never
     * recycle backing storage after an uncertain stop. */
    uint32_t value;
    if(chos_pci_read_config32(saved_boot,saved_acpi,saved_pci.segment,saved_pci.bus,saved_pci.device,saved_pci.function,4,&value))
        (void)chos_pci_write_config32(saved_boot,saved_acpi,saved_pci.segment,saved_pci.bus,saved_pci.device,saved_pci.function,4,(value&0xffffu)&~4u);
    online=0;
}
int chos_xhci_init(const chos_boot_info_t *boot,const chos_acpi_info_t *acpi,const chos_pci_device_t *pci) {
    uint64_t base,bar_bytes,sctlr;
    diagnostic.stage=1;
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
    diagnostic.cap=cap;diagnostic.hcs=hcs;diagnostic.hcc=hcc;
    diagnostic.doorbell=db;diagnostic.runtime=rt;diagnostic.stage=2;
    op=0;
    uint16_t version=chos_xhci_version(pci->vendor_id,pci->device_id,(uint16_t)(cap>>16));
    if((cap&255)<0x20 || (cap&3u) || !version ||
       db<0x100 || db>bar_bytes-4u*(MAX_SLOTS+1u) || rt<0x100 || rt>bar_bytes-0x40 ||
       !(hcs&255) || !(hcs>>24) || (cap&255)+0x400u+16u*(hcs>>24)>bar_bytes) {fail("xHCI invalid/unsupported capabilities");return 0;}
    if(version!=(cap>>16) && logger)logger("[XHCI] VMware compact HCIVERSION normalized by compatibility table.\n");
    op=mmio+(cap&255);
    if(!(rd(op+8)&1u)){fail("xHCI 4 KiB DMA pages unsupported");return 0;}
    if (!(hcc&1u) && ((uintptr_t)(devices+MAX_SLOTS)>UINT32_MAX ||
            (uintptr_t)(events+EVENT_TRBS)>UINT32_MAX || (uintptr_t)(scratch+16)>UINT32_MAX)) {
        fail("xHCI 32-bit DMA cannot address kernel buffers");return 0;
    }
    context_bytes=(hcc&4)?64:32; port_count=hcs>>24;
    slot_limit=(hcs&255)<MAX_SLOTS?(hcs&255):MAX_SLOTS;
    doorbells=mmio+db;runtime=mmio+rt;
    /* Bounded BIOS/OS ownership handoff and validation of extended capability
     * links. Unknown capabilities are skipped only inside the mapped BAR. */
    uint32_t ext=hcc>>16;
    for(unsigned budget=0;ext && budget<EVENT_TRBS;++budget) {
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
    diagnostic.stage=3;
    wr(op,rd(op)&~1u);
    if(!wait_reg(op+4,1,1,100)) {fail("xHCI halt timeout");return 0;}
    wr(op,2);
    if(!wait_reg(op,2,0,TIMEOUT_MS) || !wait_reg(op+4,1u<<11,0,TIMEOUT_MS)) {fail("xHCI reset/CNR timeout");return 0;}
    unsigned scratch_count=((hcs2>>27)&31)|((hcs2>>16)&0x3e0);
    if(scratch_count>16) {fail("xHCI scratchpad requirement exceeds bound");return 0;}
    diagnostic.stage=4;
    zero(devices,sizeof(devices));zero(events,sizeof(events));zero(dcbaa,sizeof(dcbaa));
    zero(scratch,sizeof(scratch));zero(scratch_array,sizeof(scratch_array));zero(port_attempted,sizeof(port_attempted));
    chos_xhci_ring_init(&commands);event_head=0;event_cycle=1;online=0;port_cursor=0;hub_cursor=0;
    for(unsigned i=0;i<scratch_count;++i) scratch_array[i]=(uintptr_t)scratch[i];
    if(scratch_count) dcbaa[0]=(uintptr_t)scratch_array;
    erst[0]=(uintptr_t)events;erst[1]=EVENT_TRBS;
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
    poll_hubs();
    if(!running)return;
    if(ps&PORT_CHANGE) wr(op+0x400+16u*(p-1),(ps&PORT_PRESERVE)|(ps&PORT_CHANGE));
}
const char *chos_xhci_error(void) {return error;}
unsigned chos_xhci_device_count(void) {return online;}

int chos_xhci_running(void){return running;}
unsigned chos_xhci_input_capabilities(void) {
    unsigned caps=0;
    for(unsigned i=0;i<MAX_SLOTS;++i)if(devices[i].active>0 && !devices[i].hub)
        for(unsigned j=0;j<devices[i].endpoint_count;++j) {
            chos_hid_report_layout_t *l=&devices[i].endpoints[j].layout;
            if(l->has_keyboard)caps|=1;
            if(l->has_pointer || l->has_mouse)caps|=2;
        }
    return caps;
}
