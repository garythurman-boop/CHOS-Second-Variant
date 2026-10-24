#ifndef CHOS_INTERRUPT_H
#define CHOS_INTERRUPT_H

#include <stdint.h>
#include "chos/irq_context.h"
#include "chos/acpi.h"
#include "chos/boot_info.h"

/* QEMU virt profile currently uses GICv2; keep this behind an interface. */
int interrupt_controller_init(void);
int interrupt_controller_ready(void);
/* Optional ACPI-described GICv3 initialization for the UEFI boot path. */
int interrupt_controller_init_uefi(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi);
int interrupt_enable_timer_source(void);
void interrupt_disable_irqs(void);
/* Return the frame to restore; the development preemption mode may select another task frame. */
chos_irq_context_t *interrupt_irq_dispatch(chos_irq_context_t *context);
void interrupt_enable_irqs(void);
uint64_t interrupt_count(void);
uint64_t interrupt_spurious_count(void);
uint64_t interrupt_context_frame_count(void);
uint64_t interrupt_context_last_elr(void);
uint64_t interrupt_context_last_spsr(void);
uint64_t interrupt_unhandled_count(void);

#endif
