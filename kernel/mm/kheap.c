#include <stddef.h>
#include <stdint.h>
#include "chos/atomic.h"
#include "chos/kheap.h"
#include "chos/pmm.h"
#include "chos/spinlock.h"

#define KHEAP_ARENA_MAGIC 0x43484152u /* 'CHAR' */
#define KHEAP_BLOCK_MAGIC 0x4348424Bu /* 'CHBK' */
#define KHEAP_MIN_SPLIT_PAYLOAD KHEAP_ALIGNMENT

#define ALIGN_UP_VALUE(value, alignment) (((value) + ((alignment) - 1u)) & ~((alignment) - 1u))
#define ALIGN_DOWN_VALUE(value, alignment) ((value) & ~((alignment) - 1u))

typedef struct kheap_arena kheap_arena_t;
typedef struct kheap_block kheap_block_t;

struct kheap_block {
    size_t size;                 /* Usable payload bytes, always 16-byte aligned. */
    uint32_t magic;
    uint32_t is_free;
    kheap_arena_t *arena;
    kheap_block_t *prev;
    kheap_block_t *next;
};

struct kheap_arena {
    uint32_t magic;
    uint32_t reserved;
    size_t pages;
    size_t bytes;
    kheap_arena_t *next;
    kheap_block_t *first_block;
};

#define BLOCK_HEADER_SIZE ((size_t)ALIGN_UP_VALUE(sizeof(kheap_block_t), KHEAP_ALIGNMENT))
#define ARENA_HEADER_SIZE ((size_t)ALIGN_UP_VALUE(sizeof(kheap_arena_t), KHEAP_ALIGNMENT))

static kheap_arena_t *arena_list;
/* Lock order: kernel heap -> PMM. Never acquire heap while holding PMM. */
static chos_spinlock_t kheap_lock = CHOS_SPINLOCK_INITIALIZER;
static int heap_initialized;
/* Single-core, IRQ-masked startup runs before scheduler/IRQ clients exist. */
static volatile uint32_t kheap_bootstrap_mode;

int kheap_is_bootstrap_mode(void) {
    return chos_atomic_load_u32(&kheap_bootstrap_mode) != 0u;
}

void kheap_begin_bootstrap(void) {
    /* Caller contract: no prior heap users, with local IRQs masked. Explicitly
     * reset BSS-backed state at the UEFI entry boundary instead of trusting a
     * platform-specific loader to leave every zero-init object pristine. */
    chos_atomic_store_u32(&kheap_lock.held, 0u);
    arena_list = NULL;
    heap_initialized = 0;
    chos_atomic_store_u32(&kheap_bootstrap_mode, 1u);
}

void kheap_enable_runtime_locking(void) {
    /* Single core and IRQs masked: no concurrent user can hold this lock yet. */
    chos_atomic_store_u32(&kheap_lock.held, 0u);
    chos_atomic_store_u32(&kheap_bootstrap_mode, 0u);
}

static int add_overflow_size(size_t a, size_t b, size_t *out) {
    if (b > SIZE_MAX - a) return 1;
    *out = a + b;
    return 0;
}

static int align_request(size_t requested, size_t *aligned) {
    size_t value;
    if (requested == 0u) return 0;
    if (add_overflow_size(requested, KHEAP_ALIGNMENT - 1u, &value)) return 0;
    *aligned = ALIGN_DOWN_VALUE(value, KHEAP_ALIGNMENT);
    return *aligned != 0u;
}

static uint8_t *block_payload(kheap_block_t *block) {
    return (uint8_t *)block + BLOCK_HEADER_SIZE;
}

static void block_split(kheap_block_t *block, size_t requested) {
    size_t remaining;
    kheap_block_t *new_block;

    if (block->size < requested) return;
    remaining = block->size - requested;
    if (remaining < BLOCK_HEADER_SIZE + KHEAP_MIN_SPLIT_PAYLOAD) return;

    new_block = (kheap_block_t *)(block_payload(block) + requested);
    new_block->size = remaining - BLOCK_HEADER_SIZE;
    new_block->magic = KHEAP_BLOCK_MAGIC;
    new_block->is_free = 1u;
    new_block->arena = block->arena;
    new_block->prev = block;
    new_block->next = block->next;
    if (new_block->next != NULL) new_block->next->prev = new_block;

    block->next = new_block;
    block->size = requested;
}

static int arena_grow(size_t minimum_payload) {
    size_t required_bytes;
    size_t pages;
    size_t arena_bytes;
    uint64_t physical_base;
    kheap_arena_t *arena;
    kheap_block_t *first;

    if (add_overflow_size(minimum_payload, ARENA_HEADER_SIZE, &required_bytes) ||
        add_overflow_size(required_bytes, BLOCK_HEADER_SIZE, &required_bytes) ||
        add_overflow_size(required_bytes, CHOS_PAGE_SIZE - 1u, &required_bytes)) {
        return 0;
    }
    pages = required_bytes / CHOS_PAGE_SIZE;
    if (pages < KHEAP_MIN_ARENA_PAGES) pages = KHEAP_MIN_ARENA_PAGES;
    if (pages > SIZE_MAX / CHOS_PAGE_SIZE) return 0;
    arena_bytes = pages * CHOS_PAGE_SIZE;

    if (!pmm_alloc_pages(pages, &physical_base)) return 0;
    /* Current CHOS boot stage runs with the MMU off, so RAM addresses are
       identity-addressable. This conversion must change with virtual memory. */
    arena = (kheap_arena_t *)(uintptr_t)physical_base;
    arena->magic = KHEAP_ARENA_MAGIC;
    arena->reserved = 0u;
    arena->pages = pages;
    arena->bytes = arena_bytes;
    arena->next = arena_list;
    arena->first_block = NULL;

    first = (kheap_block_t *)((uint8_t *)arena + ARENA_HEADER_SIZE);
    first->size = ALIGN_DOWN_VALUE(arena_bytes - ARENA_HEADER_SIZE - BLOCK_HEADER_SIZE,
                                   KHEAP_ALIGNMENT);
    first->magic = KHEAP_BLOCK_MAGIC;
    first->is_free = 1u;
    first->arena = arena;
    first->prev = NULL;
    first->next = NULL;
    arena->first_block = first;
    arena_list = arena;
    return 1;
}

static int kheap_init_unlocked(void) {
    pmm_stats_t stats;
    if (heap_initialized) return 1;
    pmm_get_stats(&stats);
    if (!stats.initialized) {
        heap_initialized = 0;
        arena_list = NULL;
        return 0;
    }
    arena_list = NULL;
    heap_initialized = 1;
    return 1;
}

int kheap_init(void) {
    uint64_t irq_flags;
    int result;
    if (kheap_is_bootstrap_mode()) return kheap_init_unlocked();
    irq_flags = chos_spin_lock_irqsave(&kheap_lock);
    result = kheap_init_unlocked();
    chos_spin_unlock_irqrestore(&kheap_lock, irq_flags);
    return result;
}

static void *kheap_alloc_unlocked(size_t size) {
    size_t requested;
    kheap_arena_t *arena;

    if (!heap_initialized || !align_request(size, &requested)) return NULL;

    for (;;) {
        for (arena = arena_list; arena != NULL; arena = arena->next) {
            kheap_block_t *block;
            if (arena->magic != KHEAP_ARENA_MAGIC) return NULL;
            for (block = arena->first_block; block != NULL; block = block->next) {
                if (block->magic != KHEAP_BLOCK_MAGIC || block->arena != arena) return NULL;
                if (block->is_free && block->size >= requested) {
                    block_split(block, requested);
                    block->is_free = 0u;
                    return block_payload(block);
                }
            }
        }
        if (!arena_grow(requested)) return NULL;
    }
}

void *kheap_alloc(size_t size) {
    uint64_t irq_flags;
    void *result;
    if (kheap_is_bootstrap_mode()) return kheap_alloc_unlocked(size);
    irq_flags = chos_spin_lock_irqsave(&kheap_lock);
    result = kheap_alloc_unlocked(size);
    chos_spin_unlock_irqrestore(&kheap_lock, irq_flags);
    return result;
}

static void coalesce_with_next(kheap_block_t *block) {
    kheap_block_t *next = block->next;
    if (next == NULL || !next->is_free || next->magic != KHEAP_BLOCK_MAGIC) return;
    if (block->size > SIZE_MAX - BLOCK_HEADER_SIZE) return;
    if (next->size > SIZE_MAX - BLOCK_HEADER_SIZE - block->size) return;
    block->size += BLOCK_HEADER_SIZE + next->size;
    block->next = next->next;
    if (block->next != NULL) block->next->prev = block;
    next->magic = 0u;
    next->arena = NULL;
    next->prev = NULL;
    next->next = NULL;
}

static int kheap_free_unlocked(void *ptr) {
    kheap_arena_t *arena;
    if (!heap_initialized || ptr == NULL) return 0;

    /* Find an exact payload pointer before touching memory adjacent to ptr. This
       rejects foreign and interior pointers without dereferencing attacker data. */
    for (arena = arena_list; arena != NULL; arena = arena->next) {
        kheap_block_t *block;
        if (arena->magic != KHEAP_ARENA_MAGIC) return 0;
        for (block = arena->first_block; block != NULL; block = block->next) {
            if (block->magic != KHEAP_BLOCK_MAGIC || block->arena != arena) return 0;
            if (block_payload(block) != (uint8_t *)ptr) continue;
            if (block->is_free) return 0;
            block->is_free = 1u;
            coalesce_with_next(block);
            if (block->prev != NULL && block->prev->is_free) {
                block = block->prev;
                coalesce_with_next(block);
            }
            return 1;
        }
    }
    return 0;
}

int kheap_free(void *ptr) {
    uint64_t irq_flags;
    int result;
    if (kheap_is_bootstrap_mode()) return kheap_free_unlocked(ptr);
    irq_flags = chos_spin_lock_irqsave(&kheap_lock);
    result = kheap_free_unlocked(ptr);
    chos_spin_unlock_irqrestore(&kheap_lock, irq_flags);
    return result;
}

static void kheap_get_stats_unlocked(kheap_stats_t *out) {
    kheap_arena_t *arena;
    if (out == NULL) return;
    out->initialized = heap_initialized;
    out->arena_count = 0u;
    out->total_pages = 0u;
    out->total_bytes = 0u;
    out->allocated_payload_bytes = 0u;
    out->free_payload_bytes = 0u;
    out->allocated_blocks = 0u;
    out->free_blocks = 0u;

    if (!heap_initialized) return;
    for (arena = arena_list; arena != NULL; arena = arena->next) {
        kheap_block_t *block;
        ++out->arena_count;
        out->total_pages += arena->pages;
        out->total_bytes += arena->bytes;
        for (block = arena->first_block; block != NULL; block = block->next) {
            if (block->is_free) {
                out->free_payload_bytes += block->size;
                ++out->free_blocks;
            } else {
                out->allocated_payload_bytes += block->size;
                ++out->allocated_blocks;
            }
        }
    }
}


void kheap_get_stats(kheap_stats_t *out) {
    uint64_t irq_flags;
    if (out == NULL) return;
    if (kheap_is_bootstrap_mode()) {
        kheap_get_stats_unlocked(out);
        return;
    }
    irq_flags = chos_spin_lock_irqsave(&kheap_lock);
    kheap_get_stats_unlocked(out);
    chos_spin_unlock_irqrestore(&kheap_lock, irq_flags);
}
