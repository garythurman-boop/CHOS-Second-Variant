#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define CHOS_ATOMIC_TEST_PLATFORM
#include "chos/atomic.h"

static int normal_memory;
static uint64_t irq_state = 0x140u;
static unsigned saves, restores, barriers, masked_barriers;
static int pending_irq;
static volatile uint64_t counter;

int chos_atomic_normal_memory(void) { return normal_memory; }
uint64_t chos_atomic_irq_save(void) {
    uint64_t previous = irq_state;
    irq_state |= 0x80u;
    ++saves;
    return previous;
}
void chos_atomic_irq_restore(uint64_t previous) {
    assert(irq_state & 0x80u);
    irq_state = previous;
    ++restores;
    /* An IRQ pending across the read/modify/write runs only after restoration. */
    if (pending_irq && !(irq_state & 0x80u)) {
        pending_irq = 0;
        (void)chos_atomic_fetch_add_u64(&counter, 100u);
    }
}
void chos_atomic_barrier(void) {
    ++barriers;
    if (irq_state & 0x80u) ++masked_barriers;
}

int main(void) {
    volatile uint32_t word = 0;
    normal_memory = 0;
    pending_irq = 1;
    assert(chos_atomic_fetch_add_u64(&counter, 1u) == 0u);
    assert(counter == 101u && !pending_irq && irq_state == 0x140u);
    assert(saves == 2u && restores == 2u && masked_barriers == 4u);
    /* Nested users must preserve the masked state of an outer IRQ-save lock. */
    irq_state = 0x3c0u;
    pending_irq = 1;
    assert(chos_atomic_exchange_u32(&word, 7u) == 0u);
    assert(chos_atomic_exchange_u32(&word, 9u) == 7u);
    assert(word == 9u && irq_state == 0x3c0u && pending_irq);
    pending_irq = 0;
    chos_atomic_store_u32(&word, 3u);
    assert(chos_atomic_load_u32(&word) == 3u);
    assert(chos_atomic_load_u64(&counter) == 101u);
    counter = UINT64_MAX;
    assert(chos_atomic_fetch_add_u64(&counter, 2u) == UINT64_MAX);
    assert(counter == 1u);
    /* Switching to Normal RAM must stop using the IRQ fallback. */
    unsigned old_saves = saves, old_barriers = barriers;
    normal_memory = 1;
    assert(chos_atomic_exchange_u32(&word, 0u) == 3u);
    chos_atomic_store_u32(&word, 5u);
    assert(chos_atomic_load_u32(&word) == 5u);
    assert(chos_atomic_fetch_add_u64(&counter, 2u) == 1u);
    assert(chos_atomic_load_u64(&counter) == 3u);
    assert(saves == old_saves && barriers == old_barriers);
    puts("atomic: MMU-off IRQ exclusion, nested flags, pending IRQ, wrap and Normal-RAM transition passed");
    return 0;
}
