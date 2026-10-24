#ifndef CHOS_XHCI_H
#define CHOS_XHCI_H
#include "chos/pci.h"
#define CHOS_XHCI_RING_TRBS 64u
typedef struct { uint64_t parameter; uint32_t status, control; } chos_xhci_trb_t;
typedef struct {
    chos_xhci_trb_t trbs[CHOS_XHCI_RING_TRBS] __attribute__((aligned(64)));
    uint16_t enqueue;
    uint8_t cycle;
} chos_xhci_ring_t;
void chos_xhci_ring_init(chos_xhci_ring_t *r);
/* Serialized, single TD producer: caller waits for completion before reuse. */
uint64_t chos_xhci_ring_push(chos_xhci_ring_t *r, uint64_t parameter, uint32_t status, uint32_t control);
int chos_xhci_init(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                   const chos_pci_device_t *device);
void chos_xhci_poll(void);
void chos_xhci_stop(void);
const char *chos_xhci_error(void);
unsigned chos_xhci_device_count(void);
#endif
