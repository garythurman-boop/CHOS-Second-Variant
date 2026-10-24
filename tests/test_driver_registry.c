#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chos/driver_registry.h"
static unsigned calls,polls,stops;
static int decline,fault;
static int match(const chos_driver_device_t *d) {return chos_driver_match_pci(d,CHOS_DRIVER_ANY,CHOS_DRIVER_ANY,12,3,0x30);}
static int fdt_match(const chos_driver_device_t *d) {return chos_driver_match_fdt(d,"example,input");}
static chos_driver_state_t start(const chos_driver_context_t *ctx,chos_driver_binding_t *b) {
    (void)ctx;++calls;b->reason="mock driver";b->owner=123;b->capabilities=1;
    return fault?CHOS_DEVICE_FAILED:decline?CHOS_DEVICE_UNSUPPORTED:CHOS_DEVICE_READY;
}
static chos_driver_state_t fallback(const chos_driver_context_t *ctx,chos_driver_binding_t *b) {
    (void)ctx;++calls;b->reason="fallback";b->owner=456;return CHOS_DEVICE_READY;
}
static void poll(chos_driver_binding_t *b) {assert(b->owner);++polls;}
static void stop(chos_driver_binding_t *b) {(void)b;++stops;}
static const chos_driver_ops_t primary={"native",1,100,match,start,poll,stop};
static const chos_driver_ops_t secondary={"fallback",1,10,match,fallback,poll,stop};
int main(void) {
    chos_driver_registry_t r={0};
    chos_driver_device_t device={.bus=CHOS_BUS_PCI,.id.pci={.bus=10,.vendor_id=0x15ad,.device_id=0x077a,.class_code=12,.subclass=3,.programming_interface=0x30}};
    assert(chos_driver_register(&r,&secondary) && chos_driver_register(&r,&primary));
    assert(!chos_driver_register(&r,&primary));
    chos_driver_binding_t *b=chos_driver_add(&r,&device);assert(b && chos_driver_add(&r,&device)==b);
    chos_driver_probe(&r,0);assert(!calls && b->state==CHOS_DEVICE_DEFERRED);
    chos_driver_probe(&r,1);assert(calls==1 && b->driver==&primary && b->state==CHOS_DEVICE_READY);
    chos_driver_device_t replaced=device;replaced.id.pci.vendor_id=0x1234;assert(!chos_driver_add(&r,&replaced));
    chos_driver_probe(&r,1);assert(calls==1);chos_driver_poll(&r);assert(polls==1);
    assert(!chos_driver_register(&r,&secondary));chos_driver_stop(&r);assert(stops==1 && b->state==CHOS_DEVICE_STOPPED);
    chos_driver_stop(&r);assert(stops==1);
    r=(chos_driver_registry_t){0};calls=0;decline=1;
    assert(chos_driver_register(&r,&secondary) && chos_driver_register(&r,&primary));b=chos_driver_add(&r,&device);
    chos_driver_probe(&r,1);assert(calls==2 && b->driver==&secondary && b->owner==456);
    r=(chos_driver_registry_t){0};calls=0;fault=1;
    assert(chos_driver_register(&r,&secondary) && chos_driver_register(&r,&primary));b=chos_driver_add(&r,&device);
    chos_driver_probe(&r,1);assert(calls==1 && b->state==CHOS_DEVICE_FAILED);
    chos_driver_probe(&r,1);assert(calls==1);fault=decline=0;
    r=(chos_driver_registry_t){0};chos_driver_ops_t fdt={"future-FDT",0,1,fdt_match,start,poll,stop};
    assert(chos_driver_register(&r,&fdt));device=(chos_driver_device_t){.bus=CHOS_BUS_FDT,.id.fdt={0x10000,4096,"example,input"}};
    b=chos_driver_add(&r,&device);chos_driver_probe(&r,0);assert(b->state==CHOS_DEVICE_READY);
    r=(chos_driver_registry_t){0};device=(chos_driver_device_t){.bus=CHOS_BUS_PCI};
    for(unsigned i=0;i<CHOS_DRIVER_DEVICE_MAX;++i){device.id.pci.bus=(uint8_t)i;assert(chos_driver_add(&r,&device));}
    device.id.pci.bus=99;assert(!chos_driver_add(&r,&device));chos_driver_probe(&r,1);
    assert(r.devices[0].state==CHOS_DEVICE_UNSUPPORTED);
    puts("Driver registry: PCI/FDT matching, phases, priority, clean fallback, failed-owner quarantine, idempotence, polling, stop and capacity passed");
}
