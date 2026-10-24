#include "chos/drivers.h"
#include "chos/virtio_transport.h"
#include "chos/xhci.h"
static chos_driver_registry_t registry;
static int initialized,xhci_owned;
static int match_xhci(const chos_driver_device_t *d) {
    return chos_driver_match_pci(d,CHOS_DRIVER_ANY,CHOS_DRIVER_ANY,12,3,0x30);
}
static chos_driver_state_t start_xhci(const chos_driver_context_t *ctx,chos_driver_binding_t *b) {
    if(xhci_owned){b->reason="single-controller USB ownership limit";return CHOS_DEVICE_UNSUPPORTED;}
    if(!chos_xhci_init(ctx->boot,ctx->acpi,&b->device.id.pci)) {
        b->reason=chos_xhci_error();return CHOS_DEVICE_FAILED;
    }
    xhci_owned=1;b->owner=1;b->reason="controller running; HID readiness reported separately";
    b->capabilities=CHOS_DRIVER_CAP_TRANSPORT;return CHOS_DEVICE_READY;
}
static void poll_xhci(chos_driver_binding_t *b) {
    chos_xhci_poll();
    if(!chos_xhci_running()){b->state=CHOS_DEVICE_FAILED;b->capabilities=0;b->reason=chos_xhci_error();return;}
    unsigned caps=chos_xhci_input_capabilities();
    b->capabilities=CHOS_DRIVER_CAP_TRANSPORT|((caps&1)?CHOS_DRIVER_CAP_KEYBOARD:0)|((caps&2)?CHOS_DRIVER_CAP_POINTER:0);
    b->reason=chos_xhci_error();
}
static void stop_xhci(chos_driver_binding_t *b) {(void)b;chos_xhci_stop();}
static int match_virtio(const chos_driver_device_t *d) {
    if(!chos_driver_match_pci(d,0x1af4,CHOS_DRIVER_ANY,CHOS_DRIVER_ANY,CHOS_DRIVER_ANY,CHOS_DRIVER_ANY))return 0;
    unsigned id=d->id.pci.device_id;
    return (id>0x1040 && id<0x1080) || id==0x1000 || id==0x1001 || id==0x1005;
}
static chos_driver_state_t start_virtio(const chos_driver_context_t *ctx,chos_driver_binding_t *b) {
    b->owner=chos_virtio_pci_admit(ctx->boot,ctx->acpi,&b->device.id.pci);
    b->reason=chos_virtio_transport_error();
    if(!b->owner)return CHOS_DEVICE_FAILED; /* BAR probing may already have touched hardware. */
    b->capabilities=CHOS_DRIVER_CAP_TRANSPORT;return CHOS_DEVICE_READY;
}
static void poll_virtio(chos_driver_binding_t *b) {
    if(chos_virtio_read(b->owner,0x70)&128u) {b->state=CHOS_DEVICE_FAILED;b->reason="VirtIO device queue reported failure";}
}
static const chos_driver_ops_t builtin[]={
    {"modern VirtIO PCI",CHOS_DRIVER_TRANSPORT,100,match_virtio,start_virtio,poll_virtio,0},
    {"native xHCI HID",CHOS_DRIVER_INPUT,100,match_xhci,start_xhci,poll_xhci,stop_xhci}
};
void chos_platform_drivers_discover(const chos_boot_info_t *boot,const chos_acpi_info_t *acpi,
                                   const chos_pci_device_t *devices,unsigned count) {
    if(initialized)return;
    initialized=1;registry.context=(chos_driver_context_t){boot,acpi,0};
    for(unsigned i=0;i<sizeof(builtin)/sizeof(builtin[0]);++i)(void)chos_driver_register(&registry,&builtin[i]);
    if(!devices || count>CHOS_DRIVER_DEVICE_MAX)return;
    for(unsigned i=0;i<count;++i) {
        chos_driver_device_t d={.bus=CHOS_BUS_PCI,.id.pci=devices[i]};
        (void)chos_driver_add(&registry,&d);
    }
}
void chos_platform_drivers_probe(unsigned phase) {
    chos_driver_probe(&registry,phase);
    if(phase==CHOS_DRIVER_TRANSPORT)chos_virtio_seal_bindings();
}
void chos_platform_drivers_poll(void){chos_driver_poll(&registry);}
unsigned chos_platform_input_capabilities(void){return chos_xhci_input_capabilities();}
const chos_driver_registry_t *chos_platform_driver_registry(void){return &registry;}
