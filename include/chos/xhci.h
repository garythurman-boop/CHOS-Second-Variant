#ifndef CHOS_XHCI_H
#define CHOS_XHCI_H
#include "chos/pci.h"
#define CHOS_XHCI_RING_TRBS 64u
typedef struct { uint64_t parameter; uint32_t status, control; } chos_xhci_trb_t;
typedef struct {
    chos_xhci_trb_t trbs[CHOS_XHCI_RING_TRBS] __attribute__((aligned(1024)));
    uint16_t enqueue;
    uint8_t cycle;
} chos_xhci_ring_t;
void chos_xhci_ring_init(chos_xhci_ring_t *r);
/* Serialized, single TD producer: caller waits for completion before reuse. */
uint64_t chos_xhci_ring_push(chos_xhci_ring_t *r, uint64_t parameter, uint32_t status, uint32_t control);
int chos_xhci_init(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                   const chos_pci_device_t *device);
void chos_xhci_poll(void);
void chos_xhci_set_logger(void (*write)(const char *));
void chos_xhci_stop(void);
const char *chos_xhci_error(void);
unsigned chos_xhci_device_count(void);
unsigned chos_xhci_input_capabilities(void); /* bit 0 keyboard; bit 1 pointer */
int chos_xhci_running(void);
typedef struct {
    uint32_t cap,hcs,hcc,doorbell,runtime,route;
    uint16_t stage,failed_stage,port,descriptor,requested,received,completion;
    const char *last_failure;
} chos_xhci_diagnostic_t;
const chos_xhci_diagnostic_t *chos_xhci_diagnostics(void);
#endif
