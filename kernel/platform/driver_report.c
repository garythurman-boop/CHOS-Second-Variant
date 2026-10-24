#include "chos/driver_report.h"
#include "chos/drivers.h"
#include "chos/clock.h"
#include "chos/timer.h"
#include "chos/input.h"
#include "chos/virtio_input.h"
#include "chos/xhci.h"
typedef struct {char *s;unsigned pos;} line_t;
static void append(line_t *l,const char *s) {
    if(!s)return;
    while(*s && l->pos<CHOS_DRIVER_REPORT_COLS)l->s[l->pos++]=*s++;
    l->s[l->pos]=0;
}
static void number(line_t *l,uint64_t n,unsigned hex,unsigned width) {
    char buf[24];unsigned size=0;
    do{unsigned d=(unsigned)(n%(hex?16:10));buf[size++]="0123456789abcdef"[d];n/=hex?16:10;}while(n && size<sizeof(buf));
    while(size<width && size<sizeof(buf))buf[size++]='0';
    while(size && l->pos<CHOS_DRIVER_REPORT_COLS)l->s[l->pos++]=buf[--size];
    l->s[l->pos]=0;
}
int chos_platform_needs_input_recovery(void) {
    return chos_input_device_count()==0 && chos_platform_input_capabilities()!=3;
}
static const chos_driver_binding_t *item(const chos_driver_registry_t *r,unsigned index) {
    for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<r->device_count;++i) {
        const chos_driver_binding_t *b=&r->devices[i];
        if((b->driver!=0)!=(pass==0))continue;
        if(!index--)return b;
    }
    return 0;
}
unsigned chos_platform_driver_report(unsigned page,char rows[CHOS_DRIVER_REPORT_ROWS][CHOS_DRIVER_REPORT_COLS+1]) {
    for(unsigned row=0;row<CHOS_DRIVER_REPORT_ROWS;++row)rows[row][0]=0;
    const chos_driver_registry_t *r=chos_platform_driver_registry();
    unsigned pages=(r->device_count+1)/2;if(!pages)pages=1;page%=pages;
    line_t l={rows[0],0};append(&l,"Driver admission report ");number(&l,page+1,0,0);append(&l," / ");number(&l,pages,0,0);
    l=(line_t){rows[1],0};append(&l,"Clock: ");append(&l,chos_clock_source());
    l=(line_t){rows[2],0};append(&l,"Timer: ");append(&l,timer_is_ready()?"IRQ source active; ticks ":"counter-only fallback; ticks ");number(&l,timer_ticks(),0,0);
    unsigned caps=chos_platform_input_capabilities();
    l=(line_t){rows[3],0};append(&l,"USB keyboard: ");append(&l,caps&1?"admitted":"missing");append(&l,"; pointer: ");append(&l,caps&2?"admitted":"missing");
    l=(line_t){rows[4],0};append(&l,"Event totals keys/buttons/motion: ");number(&l,chos_input_diagnostics.keys,0,0);append(&l,"/");number(&l,chos_input_diagnostics.buttons,0,0);append(&l,"/");number(&l,chos_input_diagnostics.relative+chos_input_diagnostics.absolute,0,0);
    const chos_xhci_diagnostic_t *x=chos_xhci_diagnostics();
    l=(line_t){rows[5],0};append(&l,"xHCI: ");append(&l,chos_xhci_error());
    l=(line_t){rows[6],0};append(&l,"Enum stage/failure/root/route: ");number(&l,x->stage,0,0);append(&l,"/");number(&l,x->failed_stage,0,0);append(&l,"/");number(&l,x->port,0,0);append(&l,"/");number(&l,x->route,1,0);
    l=(line_t){rows[7],0};append(&l,"CAP/HCS/HCC: ");number(&l,x->cap,1,8);append(&l,"/");number(&l,x->hcs,1,8);append(&l,"/");number(&l,x->hcc,1,8);
    l=(line_t){rows[8],0};append(&l,"DB/RT; control value requested/received/CC: ");number(&l,x->doorbell,1,0);append(&l,"/");number(&l,x->runtime,1,0);append(&l,"; ");number(&l,x->descriptor,1,0);append(&l," ");number(&l,x->requested,0,0);append(&l,"/");number(&l,x->received,0,0);append(&l,"/");number(&l,x->completion,0,0);
    for(unsigned n=0;n<2;++n) {
        const chos_driver_binding_t *b=item(r,page*2+n);if(!b)break;
        unsigned row=9+n*4;const chos_pci_device_t *p=&b->device.id.pci;
        l=(line_t){rows[row],0};
        if(b->device.bus==CHOS_BUS_FDT) {
            append(&l,"FDT ");append(&l,b->device.id.fdt.compatible);append(&l," @ ");number(&l,b->device.id.fdt.base,1,0);
        } else {
            append(&l,"PCI ");number(&l,p->segment,1,4);append(&l,":");number(&l,p->bus,1,2);append(&l,":");number(&l,p->device,1,2);append(&l,".");number(&l,p->function,1,1);append(&l,"  ");number(&l,p->vendor_id,1,4);append(&l,":");number(&l,p->device_id,1,4);append(&l," class ");number(&l,p->class_code,1,2);number(&l,p->subclass,1,2);number(&l,p->programming_interface,1,2);
        }
        l=(line_t){rows[row+1],0};append(&l,b->driver?b->driver->name:"no native driver");append(&l,": ");append(&l,chos_driver_state_name(b->state));
        l=(line_t){rows[row+2],0};append(&l,b->reason);
        if(n==1 && x->last_failure) {l=(line_t){rows[row+3],0};append(&l,"Last USB refusal: ");append(&l,x->last_failure);}
    }
    l=(line_t){rows[17],0};append(&l,"Auto page every 3s. C: Console; D: Diagnostics.");return pages;
}
