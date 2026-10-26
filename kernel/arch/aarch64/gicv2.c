#include "chos/threads.h"
#include <stdint.h>
#include "chos/interrupt.h"
#include "chos/platform.h"
#include "chos/timer.h"
#include "chos/scheduler.h"
#include "chos/gicv3.h"
#include "chos/acpi.h"
#include "chos/boot_info.h"

/* GICv2 register offsets. The QEMU virt platform adapter supplies the bases. */
#define GICD_CTLR       0x000u
#define GICD_IGROUPR0   0x080u
#define GICD_ISENABLER0 0x100u
#define GICD_ICENABLER0 0x180u
#define GICD_ICPENDR0   0x280u
#define GICD_IPRIORITYR 0x400u
#define GICC_CTLR       0x000u
#define GICC_PMR        0x004u
#define GICC_BPR        0x008u
#define GICC_IAR        0x00cu
#define GICC_EOIR       0x010u
#define GIC_SPURIOUS_MIN 1020u
#define TIMER_PPI_ID    30u

static uintptr_t distributor_base;
static uintptr_t cpu_interface_base;
static int controller_ready;
static int controller_mode; /* 2=legacy MMIO GICv2, 3=GICv3 system-register interface */
static uint32_t timer_interrupt_id = TIMER_PPI_ID;
static volatile uint64_t acknowledged_interrupts;
static volatile uint64_t unhandled_interrupts;
static volatile uint64_t spurious_interrupts;
static volatile uint64_t irq_context_frames;
static volatile uint64_t last_irq_elr;
static volatile uint64_t last_irq_spsr;

static inline uint32_t mmio_read32(uintptr_t address) {
    return *(volatile uint32_t *)address;
}

static inline void mmio_write32(uintptr_t address, uint32_t value) {
    *(volatile uint32_t *)address = value;
}

static inline void mmio_write8(uintptr_t address, uint8_t value) {
    *(volatile uint8_t *)address = value;
}

static void data_sync_barrier(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

int interrupt_controller_init(void) {
    uint32_t group;

    controller_ready = 0;
    controller_mode = 0;
    timer_interrupt_id = TIMER_PPI_ID;
    acknowledged_interrupts = 0u;
    unhandled_interrupts = 0u;
    spurious_interrupts = 0u;
    irq_context_frames = 0u;
    last_irq_elr = 0u;
    last_irq_spsr = 0u;
    distributor_base = platform_gic_distributor_base();
    cpu_interface_base = platform_gic_cpu_interface_base();
    if (distributor_base == 0u || cpu_interface_base == 0u) return 0;

    /* Interrupts remain masked at the CPU while the controller is configured. */
    mmio_write32(distributor_base + GICD_CTLR, 0u);
    mmio_write32(cpu_interface_base + GICC_CTLR, 0u);
    data_sync_barrier();

    /* QEMU's non-secure guest handles the timer PPI as Group 1. */
    group = mmio_read32(distributor_base + GICD_IGROUPR0);
    group |= (uint32_t)1u << TIMER_PPI_ID;
    mmio_write32(distributor_base + GICD_IGROUPR0, group);

    /* Ensure a stale state cannot leave the timer PPI masked or pending. */
    mmio_write32(distributor_base + GICD_ICENABLER0, (uint32_t)1u << TIMER_PPI_ID);
    mmio_write32(distributor_base + GICD_ICPENDR0, (uint32_t)1u << TIMER_PPI_ID);
    mmio_write8(distributor_base + GICD_IPRIORITYR + TIMER_PPI_ID, 0x80u);

    /*
     * QEMU virt defaults to secure=off, so this GICv2 configuration has no
     * Security Extensions. Keep the timer PPI in Group 1. Set GICC_CTLR.AckCtl
     * as well as EnableGrp1: QEMU returns special ID 1022 for a Group-1 IAR
     * read in the secure view when AckCtl is clear. Treating that as a spurious
     * interrupt without servicing the timer can produce an IRQ storm and make
     * the polled console appear frozen. AckCtl is ignored for the non-secure
     * register view on GICs that implement Security Extensions.
     */
    mmio_write32(cpu_interface_base + GICC_PMR, 0xffu);
    mmio_write32(cpu_interface_base + GICC_BPR, 0u);
    mmio_write32(distributor_base + GICD_ISENABLER0, (uint32_t)1u << TIMER_PPI_ID);
    mmio_write32(distributor_base + GICD_CTLR, 2u);
    mmio_write32(cpu_interface_base + GICC_CTLR, 2u | (1u << 2));
    data_sync_barrier();
    __asm__ volatile("isb" ::: "memory");

    controller_mode = 2;
    timer_interrupt_id = TIMER_PPI_ID;
    controller_ready = 1;
    return 1;
}

int interrupt_controller_init_uefi(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi) {
    controller_ready = 0;
    controller_mode = 0;
    timer_interrupt_id = TIMER_PPI_ID;
    acknowledged_interrupts = 0u;
    unhandled_interrupts = 0u;
    spurious_interrupts = 0u;
    irq_context_frames = 0u;
    last_irq_elr = 0u;
    last_irq_spsr = 0u;
    interrupt_disable_irqs();
    if (acpi == NULL || acpi->physical_timer_irq >= 32u ||
        !chos_gicv3_init(boot, acpi, acpi->physical_timer_irq)) return 0;
    timer_interrupt_id = acpi->physical_timer_irq;
    controller_mode = 3;
    controller_ready = 1;
    return 1;
}

int interrupt_enable_timer_source(void) {
    if (!controller_ready) return 0;
    if (controller_mode == 3) return chos_gicv3_enable_timer_irq();
    return controller_mode == 2;
}

int interrupt_controller_ready(void) {
    return controller_ready;
}

chos_irq_context_t *interrupt_irq_dispatch(chos_irq_context_t *context) {
    uint32_t acknowledge;
    uint32_t interrupt_id;

    /* The vector ABI guarantees a non-null frame on every IRQ entry. */
    ++irq_context_frames;
    last_irq_elr = context->elr;
    last_irq_spsr = context->spsr;
    if (!controller_ready) return context;
    if (controller_mode == 3) {
        acknowledge = chos_gicv3_acknowledge();
        interrupt_id = acknowledge & 0x00ffffffu;
    } else {
        acknowledge = mmio_read32(cpu_interface_base + GICC_IAR);
        interrupt_id = acknowledge & 0x3ffu;
    }
    if (interrupt_id >= GIC_SPURIOUS_MIN) {
        /*
         * IDs 1020-1023 are special/spurious. Since the timer is currently
         * the only enabled source, disable it and its PPI as a circuit breaker:
         * a bad IAR response must not trap the polled console in an IRQ storm.
         */
        ++spurious_interrupts;
        timer_stop();
        if (controller_mode == 3) {
            chos_gicv3_disable_timer_irq();
        } else {
            mmio_write32(distributor_base + GICD_ICENABLER0,
                         (uint32_t)1u << timer_interrupt_id);
        }
        data_sync_barrier();
        return context;
    }

    ++acknowledged_interrupts;
    if (interrupt_id == timer_interrupt_id) {
        timer_handle_irq();
    } else {
        ++unhandled_interrupts;
    }

    /* EOI before changing the return frame; the selected task still returns via ERET. */
    if (controller_mode == 3) chos_gicv3_end(acknowledge);
    else mmio_write32(cpu_interface_base + GICC_EOIR, acknowledge);
    data_sync_barrier();
    if (interrupt_id == timer_interrupt_id) {
        return chos_threads_cancel_return(scheduler_on_timer_irq(context));
    }
    return context;
}

void interrupt_disable_irqs(void) {
    __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
}

void interrupt_enable_irqs(void) {
    __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");
}

uint64_t interrupt_count(void) {
    return acknowledged_interrupts;
}

uint64_t interrupt_unhandled_count(void) {
    return unhandled_interrupts;
}

uint64_t interrupt_spurious_count(void) {
    return spurious_interrupts;
}

uint64_t interrupt_context_frame_count(void) {
    return irq_context_frames;
}

uint64_t interrupt_context_last_elr(void) {
    return last_irq_elr;
}

uint64_t interrupt_context_last_spsr(void) {
    return last_irq_spsr;
}
