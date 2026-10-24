#ifndef CHOS_GICV3_H
#define CHOS_GICV3_H

#include <stdint.h>
#include "chos/acpi.h"
#include "chos/boot_info.h"

/* Minimal single-PE GICv3 Group-1 path for UEFI platform bring-up. */
int chos_gicv3_init(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                    uint32_t physical_timer_irq);
uint32_t chos_gicv3_acknowledge(void);
void chos_gicv3_end(uint32_t acknowledge_token);
void chos_gicv3_disable_timer_irq(void);
int chos_gicv3_enable_timer_irq(void);
int chos_gicv3_ready(void);

#endif
