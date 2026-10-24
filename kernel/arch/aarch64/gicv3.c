#include <stdint.h>
#include "chos/gicv3.h"

#define GICD_CTLR              0x0000u
#define GICD_CTLR_RWP          (1u << 31)
#define GICD_CTLR_ENABLE_G1NS  (1u << 1)
#define GICD_CTLR_ARE_NS       (1u << 4)
#define GICR_TYPER             0x0008u
#define GICR_WAKER             0x0014u
#define GICR_WAKER_PROCESSOR_SLEEP (1u << 1)
#define GICR_WAKER_CHILDREN_ASLEEP (1u << 2)
#define GICR_CTLR_RWP          (1u << 3)
#define GICR_SGI_BASE          0x10000u
#define GICR_IGROUPR0          0x0080u
#define GICR_ISENABLER0        0x0100u
#define GICR_ICENABLER0        0x0180u
#define GICR_ICPENDR0          0x0280u
#define GICR_IPRIORITYR        0x0400u
#define GICR_FRAME_SIZE        0x20000u
#define GICD_MIN_SIZE     0x10000u
#define GIC_SPURIOUS_MIN       1020u
#define GIC_WAIT_LIMIT        1000000u

static uintptr_t distributor;
static uintptr_t redistributor;
static uint32_t timer_irq;
static int ready;

static uint32_t read32(uintptr_t address) {
    return *(volatile uint32_t *)address;
}

static uint64_t read64(uintptr_t address) {
    return *(volatile uint64_t *)address;
}

static void write32(uintptr_t address, uint32_t value) {
    *(volatile uint32_t *)address = value;
}

static void write8(uintptr_t address, uint8_t value) {
    *(volatile uint8_t *)address = value;
}

static void barrier(void) {
    __asm__ volatile("dsb sy\n\tisb" ::: "memory");
}

static int wait_mask_clear(uintptr_t address, uint32_t mask) {
    uint32_t attempts;
    for (attempts = 0u; attempts < GIC_WAIT_LIMIT; ++attempts) {
        if ((read32(address) & mask) == 0u) return 1;
    }
    return 0;
}

static uint32_t current_affinity(void) {
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    /* GICR_TYPER.Affinity_Value packs MPIDR Aff3:Aff2:Aff1:Aff0 into 32 bits. */
    return (uint32_t)(((mpidr >> 8u) & UINT64_C(0xff000000)) |
                      (mpidr & UINT64_C(0x00ffffff)));
}

static int find_current_redistributor(const chos_boot_info_t *boot,
                                      uint64_t base, uint32_t length,
                                      uintptr_t *found) {
    uint32_t offset;
    uint32_t affinity = current_affinity();
    if (found == 0 || length < GICR_FRAME_SIZE || (base & (GICR_FRAME_SIZE - 1u)) != 0u)
        return 0;
    for (offset = 0u; offset <= length - GICR_FRAME_SIZE;) {
        uint64_t typer;
        uint32_t step = GICR_FRAME_SIZE;
        uintptr_t frame = (uintptr_t)(base + offset);
        if (!chos_boot_info_mmio_safe(boot, (uint64_t)frame, GICR_FRAME_SIZE)) return 0;
        typer = read64(frame + GICR_TYPER);
        if ((uint32_t)(typer >> 32u) == affinity) {
            *found = frame;
            return 1;
        }
        /* A VLPIS redistributor occupies an additional 128 KiB VLPI frame. */
        if ((typer & (UINT64_C(1) << 1u)) != 0u) step += GICR_FRAME_SIZE;
        if ((typer & (UINT64_C(1) << 4u)) != 0u) break; /* LAST */
        if (step > length - offset) break;
        offset += step;
    }
    return 0;
}

static int set_cpu_interface(void) {
    uint64_t value;
    /* Enable the GICv3 system-register interface; verify SRE latched. */
    value = 1u;
    __asm__ volatile("msr ICC_SRE_EL1, %0\n\tisb\n\tmrs %0, ICC_SRE_EL1"
                     : "+r"(value) :: "memory");
    if ((value & 1u) == 0u) return 0;
    value = 0xffu;
    __asm__ volatile("msr ICC_PMR_EL1, %0" :: "r"(value) : "memory");
    value = 0u;
    __asm__ volatile("msr ICC_BPR1_EL1, %0" :: "r"(value) : "memory");
    value = 1u;
    __asm__ volatile("msr ICC_IGRPEN1_EL1, %0\n\tisb" :: "r"(value) : "memory");
    barrier();
    return 1;
}

int chos_gicv3_init(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                    uint32_t physical_timer_irq) {
    uintptr_t rdist;
    uint32_t waker, group, ctlr, attempts;
    ready = 0;
    distributor = 0u;
    redistributor = 0u;
    timer_irq = physical_timer_irq;

    if (boot == 0 || acpi == 0 || acpi->valid == 0u || acpi->gic_version < 3u ||
        acpi->gic_distributor_base == 0u || acpi->gic_redistributor_base == 0u ||
        acpi->gic_redistributor_length < GICR_FRAME_SIZE || physical_timer_irq < 16u || physical_timer_irq >= 32u ||
        (acpi->gic_distributor_base & 0xffffu) != 0u ||
        !chos_boot_info_mmio_safe(boot, acpi->gic_distributor_base, GICD_MIN_SIZE))
        return 0;
    if (!find_current_redistributor(boot, acpi->gic_redistributor_base,
                                    acpi->gic_redistributor_length, &rdist)) return 0;

    distributor = (uintptr_t)acpi->gic_distributor_base;
    redistributor = rdist;

    /* Keep CPU IRQ delivery masked while the distributor and local PE are set up. */
    __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
    write32(distributor + GICD_CTLR, 0u);
    if (!wait_mask_clear(distributor + GICD_CTLR, GICD_CTLR_RWP)) return 0;

    waker = read32(redistributor + GICR_WAKER);
    write32(redistributor + GICR_WAKER, waker & ~GICR_WAKER_PROCESSOR_SLEEP);
    barrier();
    for (attempts = 0u; attempts < GIC_WAIT_LIMIT; ++attempts) {
        if ((read32(redistributor + GICR_WAKER) & GICR_WAKER_CHILDREN_ASLEEP) == 0u) break;
    }
    if (attempts == GIC_WAIT_LIMIT) return 0;

    /* Timer is a PPI, therefore configure it in this PE's Redistributor frame. */
    group = read32(redistributor + GICR_SGI_BASE + GICR_IGROUPR0);
    write32(redistributor + GICR_SGI_BASE + GICR_IGROUPR0,
            group | (UINT32_C(1) << timer_irq));
    write32(redistributor + GICR_SGI_BASE + GICR_ICENABLER0, UINT32_C(1) << timer_irq);
    write32(redistributor + GICR_SGI_BASE + GICR_ICPENDR0, UINT32_C(1) << timer_irq);
    write8(redistributor + GICR_SGI_BASE + GICR_IPRIORITYR + timer_irq, 0x80u);
    barrier();

    if (!set_cpu_interface()) return 0;
    ctlr = GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1NS;
    write32(distributor + GICD_CTLR, ctlr);
    if (!wait_mask_clear(distributor + GICD_CTLR, GICD_CTLR_RWP)) return 0;
    ready = 1;
    barrier();
    return 1;
}

uint32_t chos_gicv3_acknowledge(void) {
    uint64_t value = 0u;
    __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(value));
    return (uint32_t)value;
}

void chos_gicv3_end(uint32_t acknowledge_token) {
    uint64_t value = acknowledge_token;
    __asm__ volatile("msr ICC_EOIR1_EL1, %0\n\tdsb sy\n\tisb" :: "r"(value) : "memory");
}

void chos_gicv3_disable_timer_irq(void) {
    if (!ready || redistributor == 0u || timer_irq >= 32u) return;
    write32(redistributor + GICR_SGI_BASE + GICR_ICENABLER0, UINT32_C(1) << timer_irq);
    barrier();
}

int chos_gicv3_enable_timer_irq(void) {
    if (!ready || redistributor == 0u || timer_irq >= 32u) return 0;
    write32(redistributor + GICR_SGI_BASE + GICR_ICPENDR0, UINT32_C(1) << timer_irq);
    write32(redistributor + GICR_SGI_BASE + GICR_ISENABLER0, UINT32_C(1) << timer_irq);
    barrier();
    return 1;
}

int chos_gicv3_ready(void) { return ready; }
