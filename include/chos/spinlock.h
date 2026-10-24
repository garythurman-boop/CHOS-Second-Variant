#ifndef CHOS_SPINLOCK_H
#define CHOS_SPINLOCK_H

#include <stdint.h>

/*
 * Small IRQ-safe spinlock for shared kernel/driver state.
 *
 * Lock acquisition masks local IRQs before taking the atomic lock, preventing
 * a timer-preempted task from being interrupted and then re-entering the same
 * lock on this CPU. Callers MUST NOT yield or sleep while holding a lock.
 * This is safe for the current single-core kernel and is SMP-capable at the
 * atomic-lock level, but it is not a substitute for per-CPU IRQ handling or a
 * complete SMP scheduler design.
 *
 * CHOS lock order: kheap -> PMM. PMM never calls kheap. UART/console locks
 * are isolated and must not be nested with allocator locks. Never yield, sleep,
 * schedule, or print while holding PMM or heap locks; IRQ handlers must not
 * acquire these locks.
 */
typedef struct {
    volatile uint32_t held;
} chos_spinlock_t;

#define CHOS_SPINLOCK_INITIALIZER { 0u }

uint64_t chos_spin_lock_irqsave(chos_spinlock_t *lock);
void chos_spin_unlock_irqrestore(chos_spinlock_t *lock, uint64_t irq_state);

#endif
