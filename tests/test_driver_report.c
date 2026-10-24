#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chos/driver_report.h"
#include "chos/drivers.h"
#include "chos/input.h"
#include "chos/xhci.h"
static chos_driver_registry_t registry;
static chos_xhci_diagnostic_t usb;
chos_input_diagnostics_t chos_input_diagnostics;
const chos_driver_registry_t *chos_platform_driver_registry(void){return &registry;}
const char *chos_clock_source(void){return "UEFI snapshot + counter";}
int timer_is_ready(void){return 0;}
uint64_t timer_ticks(void){return 123;}
static unsigned caps,virtio;
unsigned chos_platform_input_capabilities(void){return caps;}
unsigned chos_input_device_count(void){return virtio;}
const chos_xhci_diagnostic_t *chos_xhci_diagnostics(void){return &usb;}
const char *chos_xhci_error(void){return "no device";}
int main(void) {
    static const chos_driver_ops_t driver={.name="test native"};
    static char long_reason[1024];memset(long_reason,'x',sizeof(long_reason)-1);
    registry.device_count=5;
    for(unsigned i=0;i<5;++i) {
        registry.devices[i]=(chos_driver_binding_t){.device={.bus=CHOS_BUS_PCI,.id.pci={.bus=(uint8_t)(9+i),.vendor_id=0x15ad,.device_id=0x077a}},.state=CHOS_DEVICE_UNSUPPORTED,.reason=long_reason};
    }
    registry.devices[1].driver=registry.devices[2].driver=&driver;
    registry.devices[1].state=CHOS_DEVICE_READY;registry.devices[2].state=CHOS_DEVICE_FAILED;
    struct {unsigned before;char lines[CHOS_DRIVER_REPORT_ROWS][CHOS_DRIVER_REPORT_COLS+1];unsigned after;} report={.before=0x12345678,.after=0x87654321};
    for(unsigned page=0;page<9;++page) {
        memset(report.lines,0xcc,sizeof(report.lines));assert(chos_platform_driver_report(page,report.lines)==3);
        for(unsigned row=0;row<CHOS_DRIVER_REPORT_ROWS;++row)assert(memchr(report.lines[row],0,CHOS_DRIVER_REPORT_COLS+1));
        assert(report.before==0x12345678 && report.after==0x87654321);
    }
    assert(chos_platform_driver_report(0,report.lines)==3 && strstr(report.lines[9],"0000:0a"));
    assert(strstr(report.lines[2],"counter-only"));
    assert(chos_platform_needs_input_recovery());caps=3;assert(!chos_platform_needs_input_recovery());
    caps=0;virtio=2;assert(!chos_platform_needs_input_recovery());
    registry=(chos_driver_registry_t){0};assert(chos_platform_driver_report(999,report.lines)==1);
    puts("Driver report: bounded output, prioritized failures, pagination, missing hardware and input-recovery policy passed");
}
