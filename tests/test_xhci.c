#define CHOS_HOST_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/platform/xhci.c"
static uint32_t regs[0x4000/4];
static unsigned mock_event_tail;
static int reset_stuck, no_response, bad_completion;
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
    if(a==0x11000 && !no_response) {
        unsigned i=(commands.enqueue+CHOS_XHCI_RING_TRBS-2)%(CHOS_XHCI_RING_TRBS-1);
        events[mock_event_tail++]=(chos_xhci_trb_t){(uintptr_t)&commands.trbs[i],(bad_completion?5u:1u)<<24,TRB(33)|1};
    }
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
void chos_input_key(uint16_t c,int d){(void)c;(void)d;}
void chos_input_button(uint16_t c,int d){(void)c;(void)d;}
void chos_input_relative(int32_t x,int32_t y){(void)x;(void)y;}
void chos_input_scroll(int32_t w){(void)w;}
static void fixture(void) {
    memset(regs,0,sizeof(regs)); regs[0]=0x01000020;regs[1]=(2u<<24)|8u;
    regs[4]=1;regs[5]=0x1000;regs[6]=0x2000;regs[0x28/4]=1;
    mmio=0;running=poisoned=0;mock_event_tail=0;mock_time=0;
    reset_stuck=no_response=bad_completion=0;
}
int main(void) {
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
    d->endpoints[0].dci=3;d->endpoints[0].pending=0x1230;
    events[mock_event_tail++]=(chos_xhci_trb_t){0x1230,6u<<24,TRB(32)|(1u<<24)|(3u<<16)|1};
    chos_xhci_poll();assert(!d->slot && !online && running);
    regs[0x24/4]=4;chos_xhci_poll();assert(!running && poisoned);
    puts("xHCI mock: absent ports, idempotence, malformed capabilities, reset timeout, lost/error completion, fatal status and quarantine passed");
}
