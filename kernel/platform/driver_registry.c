#include "chos/driver_registry.h"
static int equal(const char *a,const char *b) {
    if(!a || !b)return 0;
    for(unsigned i=0;i<96;++i){if(a[i]!=b[i])return 0;if(!a[i])return 1;}return 0;
}
const char *chos_driver_state_name(chos_driver_state_t s) {
    switch(s) {
    case CHOS_DEVICE_DISCOVERED:return "discovered";
    case CHOS_DEVICE_DEFERRED:return "deferred";
    case CHOS_DEVICE_UNSUPPORTED:return "unclaimed";
    case CHOS_DEVICE_READY:return "transport ready";
    case CHOS_DEVICE_FAILED:return "failed/quarantined";
    case CHOS_DEVICE_STOPPED:return "stopped";
    default:return "invalid";
    }
}
int chos_driver_match_pci(const chos_driver_device_t *d,uint16_t vendor,uint16_t product,
                          uint16_t cls,uint16_t sub,uint16_t interface) {
    if(!d || d->bus!=CHOS_BUS_PCI)return 0;
    const chos_pci_device_t *p=&d->id.pci;
    return (vendor==CHOS_DRIVER_ANY || vendor==p->vendor_id) &&
           (product==CHOS_DRIVER_ANY || product==p->device_id) &&
           (cls==CHOS_DRIVER_ANY || cls==p->class_code) &&
           (sub==CHOS_DRIVER_ANY || sub==p->subclass) &&
           (interface==CHOS_DRIVER_ANY || interface==p->programming_interface);
}
int chos_driver_match_fdt(const chos_driver_device_t *d,const char *compatible) {
    return d && d->bus==CHOS_BUS_FDT && equal(d->id.fdt.compatible,compatible);
}
int chos_driver_register(chos_driver_registry_t *r,const chos_driver_ops_t *ops) {
    if(!r || r->started || !ops || !ops->name || !ops->name[0] || !ops->matches || !ops->start ||
       r->driver_count>=CHOS_DRIVER_MAX)return 0;
    for(unsigned i=0;i<r->driver_count;++i)if(equal(r->drivers[i]->name,ops->name))return 0;
    unsigned i=r->driver_count++;
    while(i && r->drivers[i-1]->priority<ops->priority){r->drivers[i]=r->drivers[i-1];--i;}
    r->drivers[i]=ops;return 1;
}
static int same(const chos_driver_device_t *a,const chos_driver_device_t *b) {
    if(a->bus!=b->bus)return 0;
    if(a->bus==CHOS_BUS_FDT)return a->id.fdt.base==b->id.fdt.base && equal(a->id.fdt.compatible,b->id.fdt.compatible);
    return a->id.pci.segment==b->id.pci.segment && a->id.pci.bus==b->id.pci.bus &&
           a->id.pci.device==b->id.pci.device && a->id.pci.function==b->id.pci.function;
}
chos_driver_binding_t *chos_driver_add(chos_driver_registry_t *r,const chos_driver_device_t *device) {
    if(!r || !device || (device->bus!=CHOS_BUS_PCI && device->bus!=CHOS_BUS_FDT))return 0;
    for(unsigned i=0;i<r->device_count;++i)if(same(&r->devices[i].device,device)) {
        const chos_driver_device_t *old=&r->devices[i].device;
        if(device->bus==CHOS_BUS_PCI && (old->id.pci.vendor_id!=device->id.pci.vendor_id || old->id.pci.device_id!=device->id.pci.device_id))return 0;
        if(device->bus==CHOS_BUS_FDT && old->id.fdt.size!=device->id.fdt.size)return 0;
        return &r->devices[i];
    }
    if(r->started || r->device_count>=CHOS_DRIVER_DEVICE_MAX)return 0;
    chos_driver_binding_t *b=&r->devices[r->device_count++];
    *b=(chos_driver_binding_t){.device=*device,.state=CHOS_DEVICE_DISCOVERED,.reason="awaiting probe"};return b;
}
void chos_driver_probe(chos_driver_registry_t *r,unsigned phase) {
    if(!r)return;
    r->started=1;
    for(unsigned i=0;i<r->device_count;++i) {
        chos_driver_binding_t *b=&r->devices[i];
        if(b->state!=CHOS_DEVICE_DISCOVERED && b->state!=CHOS_DEVICE_DEFERRED)continue;
        b->state=CHOS_DEVICE_UNSUPPORTED;b->reason="no registered driver accepts this device";
        for(unsigned j=0;j<r->driver_count;++j) {
            const chos_driver_ops_t *ops=r->drivers[j];
            if(!ops->matches(&b->device))continue;
            b->driver=ops;
            if(ops->phase>phase){b->state=CHOS_DEVICE_DEFERRED;b->reason="waiting for initialization phase";break;}
            chos_driver_state_t state=ops->start(&r->context,b);
            if(state==CHOS_DEVICE_UNSUPPORTED){b->driver=0;b->owner=0;b->capabilities=0;continue;}
            b->state=state==CHOS_DEVICE_READY?state:CHOS_DEVICE_FAILED;
            if(!b->reason)b->reason="driver did not provide a reason";
            break; /* Failed ownership is never offered to a fallback driver. */
        }
    }
}
void chos_driver_poll(chos_driver_registry_t *r) {
    if(!r)return;
    for(unsigned i=0;i<r->device_count;++i) {
        chos_driver_binding_t *b=&r->devices[i];
        if(b->state==CHOS_DEVICE_READY && b->driver && b->driver->poll)b->driver->poll(b);
    }
}
void chos_driver_stop(chos_driver_registry_t *r) {
    if(!r)return;
    for(unsigned i=r->device_count;i>0;--i) {
        chos_driver_binding_t *b=&r->devices[i-1];
        if((b->state==CHOS_DEVICE_READY || b->state==CHOS_DEVICE_FAILED) && b->driver) {
            if(b->driver->stop)b->driver->stop(b);
            b->state=CHOS_DEVICE_STOPPED;b->capabilities=0;
        }
    }
}
