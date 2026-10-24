#include "chos/spinlock.h"

#ifdef CHOS_HOST_TEST
#include <sched.h>
static uint64_t irq_save(void) { return 0u; }
static void irq_restore(uint64_t state) { (void)state; }
static void spin_hint(void) { (void)sched_yield(); }
#else
static uint64_t irq_save(void) {
    uint64_t state;
    __asm__ volatile("mrs %0, daif\n\tmsr daifset, #2\n\tisb"
                     : "=r"(state) :: "memory");
    return state;
}

static void irq_restore(uint64_t state) {
    __asm__ volatile("msr daif, %0\n\tisb" :: "r"(state) : "memory");
}

static void spin_hint(void) {
    __asm__ volatile("yield" ::: "memory");
}
#endif

uint64_t chos_spin_lock_irqsave(chos_spinlock_t *lock) {
    uint64_t state;
    if (lock == 0) return 0u;
    state = irq_save();
    while (__atomic_exchange_n(&lock->held, 1u, __ATOMIC_ACQUIRE) != 0u) {
        spin_hint();
    }
    return state;
}

void chos_spin_unlock_irqrestore(chos_spinlock_t *lock, uint64_t irq_state) {
    if (lock == 0) return;
    __atomic_store_n(&lock->held, 0u, __ATOMIC_RELEASE);
    irq_restore(irq_state);
}
