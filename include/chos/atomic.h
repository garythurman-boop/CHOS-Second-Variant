#ifndef CHOS_ATOMIC_H
#define CHOS_ATOMIC_H

#include <stdint.h>

/* Kernel RAM atomics. With translation disabled ARM64 accesses have Device
 * attributes; exclusive accesses to them may abort (DFSC 0x35). The current
 * bootstrap/UEFI runtime starts ONE CPU, so masking local IRQs provides the
 * required exclusion there. Never start secondary CPUs in this mode. With the
 * MMU enabled, kernel objects must be mapped Normal memory and use hardware
 * atomics. These helpers are not for MMIO or DMA-owned memory.
 *
 * The choice follows SCTLR on every call, including the direct boot transition
 * to Normal RAM. No flag, allocation, lock or atomic is needed to select it.
 */
#if defined(CHOS_ATOMIC_TEST_PLATFORM)
int chos_atomic_normal_memory(void);
uint64_t chos_atomic_irq_save(void);
void chos_atomic_irq_restore(uint64_t state);
void chos_atomic_barrier(void);
#elif defined(CHOS_HOST_TEST) || !defined(__aarch64__)
static inline int chos_atomic_normal_memory(void) { return 1; }
/* Unreachable on the host; do not emulate IRQ masking with a no-op. */
static inline uint64_t chos_atomic_irq_save(void) { __builtin_trap(); }
static inline void chos_atomic_irq_restore(uint64_t state) { (void)state; __builtin_trap(); }
static inline void chos_atomic_barrier(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
#else
static inline int chos_atomic_normal_memory(void) {
    uint64_t control;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(control) :: "memory");
    return (control & 1u) != 0u;
}
static inline uint64_t chos_atomic_irq_save(void) {
    uint64_t state;
    __asm__ volatile("mrs %0, daif\n\tmsr daifset, #2\n\tisb"
                     : "=r"(state) :: "memory");
    return state;
}
static inline void chos_atomic_irq_restore(uint64_t state) {
    __asm__ volatile("msr daif, %0\n\tisb" :: "r"(state) : "memory");
}
static inline void chos_atomic_barrier(void) { __asm__ volatile("dmb sy" ::: "memory"); }
#endif

static inline uint32_t chos_atomic_load_u32(const volatile uint32_t *value) {
    if (chos_atomic_normal_memory()) return __atomic_load_n(value, __ATOMIC_ACQUIRE);
    uint32_t result = *value;
    chos_atomic_barrier();
    return result;
}

static inline void chos_atomic_store_u32(volatile uint32_t *value, uint32_t next) {
    if (chos_atomic_normal_memory()) { __atomic_store_n(value, next, __ATOMIC_RELEASE); return; }
    chos_atomic_barrier();
    *value = next;
}

static inline uint32_t chos_atomic_exchange_u32(volatile uint32_t *value, uint32_t next) {
    if (chos_atomic_normal_memory()) return __atomic_exchange_n(value, next, __ATOMIC_ACQUIRE);
    uint64_t flags = chos_atomic_irq_save();
    chos_atomic_barrier();
    uint32_t previous = *value;
    *value = next;
    chos_atomic_barrier();
    chos_atomic_irq_restore(flags);
    return previous;
}

static inline uint64_t chos_atomic_load_u64(const volatile uint64_t *value) {
    if (chos_atomic_normal_memory()) return __atomic_load_n(value, __ATOMIC_RELAXED);
    /* Naturally aligned single-copy 64-bit access on AArch64. */
    return *value;
}

static inline uint64_t chos_atomic_fetch_add_u64(volatile uint64_t *value, uint64_t add) {
    if (chos_atomic_normal_memory()) return __atomic_fetch_add(value, add, __ATOMIC_RELAXED);
    uint64_t flags = chos_atomic_irq_save();
    chos_atomic_barrier();
    uint64_t previous = *value;
    *value = previous + add;
    chos_atomic_barrier();
    chos_atomic_irq_restore(flags);
    return previous;
}

#endif
