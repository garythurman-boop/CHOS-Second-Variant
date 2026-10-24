#include "chos/atomic.h"
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
    while (chos_atomic_exchange_u32(&lock->held, 1u) != 0u) {
        spin_hint();
    }
    return state;
}

void chos_spin_unlock_irqrestore(chos_spinlock_t *lock, uint64_t irq_state) {
    if (lock == 0) return;
    chos_atomic_store_u32(&lock->held, 0u);
    irq_restore(irq_state);
}
