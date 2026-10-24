#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "chos/spinlock.h"

#define THREAD_COUNT 4u
#define ITERATIONS 50000u

static chos_spinlock_t lock = CHOS_SPINLOCK_INITIALIZER;
static uint64_t protected_counter;
static unsigned int in_critical_section;
static unsigned int violation_count;

static void *worker(void *unused) {
    unsigned int i;
    (void)unused;
    for (i = 0u; i < ITERATIONS; ++i) {
        uint64_t flags = chos_spin_lock_irqsave(&lock);
        if (in_critical_section != 0u) ++violation_count;
        ++in_critical_section;
        ++protected_counter;
        --in_critical_section;
        chos_spin_unlock_irqrestore(&lock, flags);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[THREAD_COUNT];
    unsigned int i;
    for (i = 0u; i < THREAD_COUNT; ++i) {
        assert(pthread_create(&threads[i], NULL, worker, NULL) == 0);
    }
    for (i = 0u; i < THREAD_COUNT; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
    assert(protected_counter == (uint64_t)THREAD_COUNT * ITERATIONS);
    assert(violation_count == 0u);
    assert(in_critical_section == 0u);
    puts("spinlock: multi-threaded mutual exclusion stress test passed");
    return 0;
}
