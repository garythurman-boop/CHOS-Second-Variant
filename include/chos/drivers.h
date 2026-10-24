#ifndef CHOS_DRIVERS_H
#define CHOS_DRIVERS_H
#include "chos/driver_registry.h"
/* The built-in catalogue is a policy adapter over the reusable registry. New
 * hardware drivers register ops here; discovery and the GUI do not change. */
void chos_platform_drivers_discover(const chos_boot_info_t *,const chos_acpi_info_t *,
                                   const chos_pci_device_t *,unsigned count);
void chos_platform_drivers_probe(unsigned phase);
void chos_platform_drivers_poll(void);
unsigned chos_platform_input_capabilities(void); /* bit 0 keyboard; bit 1 pointer */
const chos_driver_registry_t *chos_platform_driver_registry(void);
#endif
