#ifndef CHOS_KHEAP_H
#define CHOS_KHEAP_H

#include <stddef.h>
#include <stdint.h>

#define KHEAP_ALIGNMENT 16u
#define KHEAP_MIN_ARENA_PAGES 4u

typedef struct {
    int initialized;
    size_t arena_count;
    uint64_t total_pages;
    uint64_t total_bytes;
    uint64_t allocated_payload_bytes;
    uint64_t free_payload_bytes;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t quarantined_arenas;
    uint64_t quarantined_pages;
} kheap_stats_t;

/* Begin single-core, IRQ-masked UEFI bootstrap mode before the first heap call.
 * This resets early heap state and avoids runtime spinlocks until explicitly
 * published. Only call before any heap users or interrupt handlers are active. */
void kheap_begin_bootstrap(void);
int kheap_is_bootstrap_mode(void);
void kheap_enable_runtime_locking(void);

/* Initialize after the physical memory manager has been initialized. */
int kheap_init(void);

/* Metadata is protected by an IRQ-save lock; these routines must not be called from IRQ context. */
/* Lock order is heap -> PMM. Do not hold the UART/console lock while calling these APIs. */
/* Freestanding malloc/free-like API. Allocations are 16-byte aligned. */
void *kheap_alloc(size_t size);
int kheap_free(void *ptr);

void kheap_get_stats(kheap_stats_t *out);

#endif
