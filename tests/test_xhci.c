#define CHOS_HOST_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/platform/xhci.c"
static uint32_t regs[0x4000/4];
static unsigned mock_event_tail;
static int reset_stuck, no_response, bad_completion;
static int simulate_transfers,data_fault,prefix_read,packet_updated;
static unsigned delivered_keys;
static void emulate_control(unsigned slot);
static chos_boot_info_t boot;
static chos_acpi_info_t acpi;
static chos_pci_device_t pci={.class_code=12,.subclass=3,.programming_interface=0x30};
uint32_t chos_xhci_mock_read(uintptr_t a) { assert(a>=0x10000 && a<0x14000);return regs[(a-0x10000)/4]; }
void chos_xhci_mock_write(uintptr_t a,uint32_t v) {
    assert(a>=0x10000 && a<0x14000);regs[(a-0x10000)/4]=v;
    if(a==0x10020) {
        if(v==2 && !reset_stuck) regs[0x20/4]=0;
        regs[0x24/4]=(v&1)?0:1;
    }
    if(a==0x10000+(regs[5]&~3u) && !no_response) {
        unsigned i=(commands.enqueue+CHOS_XHCI_RING_TRBS-2)%(CHOS_XHCI_RING_TRBS-1);
        unsigned type=(commands.trbs[i].control>>10)&63;
        unsigned slot=type==9?1:commands.trbs[i].control>>24;
        if(simulate_transfers && (type==11 || type==12 || type==13)) {
            usb_device_t *d=&devices[0];uint32_t *input=(void *)(uintptr_t)commands.trbs[i].parameter;
            if(type==13 && (input[1]&2)) {
                assert((ctx(d,1)[0]&7u)==0); /* Input EP context has no endpoint state. */
                if((ctx(d,1)[1]>>16)==64)packet_updated=1;
            }
            if(type==12)assert(ctx(d,0)[3]==0); /* Output slot state/address is reserved in input. */
            for(unsigned c=0;c<32;++c)if(input[1]&(1u<<c))memcpy(d->output+c*context_bytes,ctx(d,c),context_bytes);
            if(type==11 || type==13) {
                ((uint32_t *)(void *)d->output)[3]=0x10000001;
                ((uint32_t *)(void *)(d->output+context_bytes))[0]|=1;
            }
        }
        events[mock_event_tail++]=(chos_xhci_trb_t){(uintptr_t)&commands.trbs[i],(bad_completion?5u:1u)<<24,TRB(33)|(slot<<24)|1};
    }
    if(simulate_transfers && a==0x10000+(regs[5]&~3u)+4 && v==1)emulate_control(1);
}
int chos_pci_map_bar(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *p,unsigned i,uint64_t *base,uint64_t *size) {
    (void)b;(void)a;(void)p;(void)i;*base=0x10000;*size=0x4000;return 1;
}
int chos_pci_enable_memory_busmaster(const chos_boot_info_t *b,const chos_acpi_info_t *a,const chos_pci_device_t *p) {(void)b;(void)a;(void)p;return 1;}
int chos_pci_read_config32(const chos_boot_info_t *b,const chos_acpi_info_t *a,uint16_t seg,uint8_t bus,uint8_t dev,uint8_t f,uint16_t o,uint32_t *v) {
    (void)b;(void)a;(void)seg;(void)bus;(void)dev;(void)f;(void)o;*v=6;return 1;
}
int chos_pci_write_config32(const chos_boot_info_t *b,const chos_acpi_info_t *a,uint16_t seg,uint8_t bus,uint8_t dev,uint8_t f,uint16_t o,uint32_t v) {
    (void)b;(void)a;(void)seg;(void)bus;(void)dev;(void)f;(void)o;(void)v;return 1;
}
void chos_input_key(uint16_t c,int d){assert(c);if(d)++delivered_keys;}
void chos_input_button(uint16_t c,int d){(void)c;(void)d;}
void chos_input_relative(int32_t x,int32_t y){(void)x;(void)y;}
void chos_input_absolute(int32_t x,int32_t y){(void)x;(void)y;}
void chos_input_scroll(int32_t w){(void)w;}
static void fixture(void) {
    memset(regs,0,sizeof(regs)); regs[0]=0x01000020;regs[1]=(2u<<24)|8u;
    regs[4]=1;regs[5]=0x1000;regs[6]=0x2000;regs[0x28/4]=1;
    mmio=0;running=poisoned=0;mock_event_tail=0;mock_time=0;
    reset_stuck=no_response=bad_completion=0;test_control=0;
    simulate_transfers=data_fault=prefix_read=packet_updated=0;delivered_keys=0;
}
static const uint8_t keyboard_report[]={
0x05,1,0x09,6,0xa1,1,0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
0x95,1,0x75,8,0x81,1,0x95,6,0x75,8,0x15,0,0x25,0x65,0x19,0,0x29,0x65,0x81,0,0xc0};
static const uint8_t vendor_report[]={0x06,0,0xff,0x09,1,0xa1,1,0x15,0,0x25,1,0x75,8,0x95,1,0x81,2,0xc0};
static void emulate_control(unsigned slot) {
    usb_device_t *d=&devices[0];unsigned length=control_requested;
    unsigned last=(d->control.enqueue+CHOS_XHCI_RING_TRBS-2)%(CHOS_XHCI_RING_TRBS-1);
    unsigned first=(last+CHOS_XHCI_RING_TRBS-1-(length?2u:1u))%(CHOS_XHCI_RING_TRBS-1);
    uint64_t setup=d->control.trbs[first].parameter;
    unsigned request=(setup>>8)&255,value=(setup>>16)&65535,index=(setup>>32)&65535;
    if(request==6) {
        const uint8_t descriptor[]={18,1,0,2,0,0,0,64,0x0f,0x0e,3,0,0,1,0,0,0,1};
        uint8_t config[]={9,2,59,0,2,1,0,0x80,50,
            9,4,0,0,1,3,0,0,0,9,0x21,0x11,1,0,1,0x22,sizeof(vendor_report),0,7,5,0x81,3,8,0,1,
            9,4,1,0,1,3,0,0,0,9,0x21,0x11,1,0,1,0x22,sizeof(keyboard_report),0,7,5,0x82,3,8,0,1};
        const uint8_t *data=0;size_t available=0;
        if(value==0x100) {
            if(length==8)prefix_read=1;
            if(length==18)assert(prefix_read && packet_updated);
            data=descriptor;available=sizeof(descriptor);
        } else if(value==0x200) {data=config;available=sizeof(config);}
        else if(value==0x2200) {data=index?keyboard_report:vendor_report;available=index?sizeof(keyboard_report):sizeof(vendor_report);}
        assert(data && length<=available);memcpy(d->data,data,length);
    }
    if(length && data_fault!=1) {
        chos_xhci_trb_t *data=(void *)(uintptr_t)control_data_trb;
        assert(data->control&(1u<<5)); /* actual Data Stage completion is required */
        unsigned residual=data_fault==2?1:data_fault==3?length+1:0;
        events[mock_event_tail++]=(chos_xhci_trb_t){control_data_trb,((data_fault==2?13u:1u)<<24)|residual,TRB(32)|(slot<<24)|(1u<<16)|1};
    }
    events[mock_event_tail++]=(chos_xhci_trb_t){(uintptr_t)&d->control.trbs[last],1u<<24,TRB(32)|(slot<<24)|(1u<<16)|1};
}
static void composite_transfer_tests(void) {
    fixture();assert(chos_xhci_init(&boot,&acpi,&pci));simulate_transfers=1;
    assert(attach_device(1,1,0,0) && online==1 && prefix_read && packet_updated);
    usb_device_t *d=&devices[0];assert(d->endpoint_count==1 && d->endpoints[0].iface.interface==1);
    endpoint_t *e=&d->endpoints[0];
    for(unsigned i=0;i<INPUT_RECEIVES;++i){assert(e->pending[i]);for(unsigned j=0;j<i;++j)assert(e->pending[i]!=e->pending[j]);}
    e->buffer[0][0]=2;e->buffer[0][2]=4;
    events[mock_event_tail++]=(chos_xhci_trb_t){e->pending[0],1u<<24,TRB(32)|(1u<<24)|((unsigned)e->dci<<16)|1};
    /* The synthetic root is connected so polling does not remove its slot. */
    regs[(0x20+0x400)/4]=PORT_CCS|PORT_PED|(1u<<10);chos_xhci_poll();assert(delivered_keys==2);
    for(data_fault=1;data_fault<=3;++data_fault) {
        assert(!control(d,0x80,6,0x100,0,8));assert(strstr(error,"length/completion"));
    }
    data_fault=0;assert(remove_device(d) && !online);
}
static unsigned port_status,port_changes,cleared,reset_requests;
static int class_request(usb_device_t *d,uint8_t type,uint8_t request,uint16_t value,uint16_t index,uint16_t length) {
    (void)index;(void)length;
    if(type==0xa3 && request==0) {
        d->data[0]=(uint8_t)port_status;d->data[1]=(uint8_t)(port_status>>8);
        d->data[2]=(uint8_t)port_changes;d->data[3]=(uint8_t)(port_changes>>8);return 1;
    }
    if(type==0x23 && request==1 && value>=16) {cleared|=1u<<(value-16);port_changes&=~(1u<<(value-16));return 1;}
    if(type==0x23 && request==3 && value==4) {++reset_requests;port_status|=16;return 1;}
    if(type==0x23 && request==1 && value==8) return 1;
    return 0;
}
static void hub_fault_tests(void) {
    fixture();assert(chos_xhci_init(&boot,&acpi,&pci));
    usb_device_t *h=&devices[0];h->active=1;h->hub=1;h->slot=1;h->hub_ports=1;h->hub_changes=1;
    test_control=class_request;port_status=1;port_changes=1;cleared=reset_requests=0;
    poll_hubs();assert(h->port_phase[0]==1 && cleared==1);
    mock_time+=30;poll_hubs();assert(reset_requests==1 && h->port_phase[0]==2);
    mock_time+=600;poll_hubs();assert(h->port_phase[0]==3 && running); /* bounded stuck reset */
    /* Disconnecting a hub releases all descendants before disabling the parent. */
    usb_device_t *child=&devices[1];child->active=1;child->slot=2;child->parent=1;online=1;
    assert(remove_device(h));assert(!child->slot && !h->slot && !online && running);
    fixture();assert(chos_xhci_init(&boot,&acpi,&pci));
    h=&devices[0];h->active=1;h->hub=1;h->slot=1;h->hub_ports=1;h->hub_changes=1;
    child=&devices[1];child->active=1;child->slot=2;child->parent=1;child->parent_port=1;online=1;
    port_status=9;port_changes=0;test_control=class_request;poll_hubs();
    assert(!child->slot && !online && h->port_phase[0]==3 && running); /* overcurrent */
    /* Lost control TD cancels its slot; unrelated controllers/ports stay alive. */
    fixture();assert(chos_xhci_init(&boot,&acpi,&pci));
    h=&devices[0];h->slot=1;h->active=1;chos_xhci_ring_init(&h->control);no_response=1;
    assert(!control(h,0x80,6,0x100,0,18));assert(running && !poisoned);
    no_response=0;assert(remove_device(h));assert(running && !h->slot);
}
int main(void) {
    assert(chos_xhci_version(0x15ad,0x077a,0x12)==0x120);
    assert(!chos_xhci_version(0x1234,0x077a,0x12));
    assert(!chos_xhci_version(0x15ad,0x077a,0xffff));
    fixture();assert(chos_xhci_init(&boot,&acpi,&pci));assert(!online);
    assert(chos_xhci_init(&boot,&acpi,&pci)); /* idempotent */
    for(unsigned i=0;i<500;++i) chos_xhci_poll(); /* absent ports, no input */
    chos_xhci_stop();assert(!running && !online);
    fixture();acpi.dma_translation_unverified=1;assert(!chos_xhci_init(&boot,&acpi,&pci));acpi.dma_translation_unverified=0;
    fixture();regs[0]=0xffffffff;assert(!chos_xhci_init(&boot,&acpi,&pci));assert(poisoned);
    fixture();reset_stuck=1;assert(!chos_xhci_init(&boot,&acpi,&pci));assert(strstr(error,"reset"));
    fixture();no_response=1;assert(!chos_xhci_init(&boot,&acpi,&pci));assert(!running && poisoned);
    assert(mock_time<2000);assert(!chos_xhci_init(&boot,&acpi,&pci)); /* no DMA reuse after uncertain stop */
    fixture();bad_completion=1;assert(!chos_xhci_init(&boot,&acpi,&pci));assert(!running);
    fixture();assert(chos_xhci_init(&boot,&acpi,&pci));
    /* An interrupt endpoint stall releases input and disables only its slot. */
    usb_device_t *d=&devices[0]; d->slot=1;d->port=1;d->active=1;d->endpoint_count=1;online=1;
    d->endpoints[0].dci=3;d->endpoints[0].pending[0]=0x1230;
    events[mock_event_tail++]=(chos_xhci_trb_t){0x1230,6u<<24,TRB(32)|(1u<<24)|(3u<<16)|1};
    chos_xhci_poll();assert(!d->slot && !online && running);
    regs[0x24/4]=4;chos_xhci_poll();assert(!running && poisoned);
    fixture();assert(chos_xhci_init(&boot,&acpi,&pci));commands.enqueue=CHOS_XHCI_RING_TRBS;
    assert(!command(23,0,0,0) && poisoned && !running);
    fixture();
    /* Actual Fusion M7G register values from the separate Test-CHOS records.
     * Assigned MMIO base remains a mock; the VMware physical base is not used. */
    pci.vendor_id=0x15ad;pci.device_id=0x077a;
    regs[0]=0x00120020;regs[1]=0x08001f7f;regs[2]=0x7f;
    regs[4]=0x0038f081;regs[5]=0x820;regs[6]=0xa20;
    assert(chos_xhci_init(&boot,&acpi,&pci) && port_count==8 && slot_limit==16);
    composite_transfer_tests();
    hub_fault_tests();
    puts("xHCI mock: absent ports, idempotence, malformed capabilities, reset timeout, lost/error completion, fatal status and quarantine passed");
}
