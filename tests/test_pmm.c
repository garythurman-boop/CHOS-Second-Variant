#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "chos/fdt.h"
#include "chos/pmm.h"

static void set_ram(fdt_info_t *tree, size_t index, uint64_t base, uint64_t size) {
    tree->memory[index].base = base;
    tree->memory[index].size = size;
}

static void setup_tree(fdt_info_t *tree) {
    memset(tree, 0, sizeof(*tree));
    tree->valid = 1;
    tree->memory_count = 1u;
    set_ram(tree, 0u, 0x40000000u, 0x04000000u); /* 64 MiB */
}

static void test_reservations_and_alloc_free(void) {
    fdt_info_t tree;
    pmm_stats_t stats;
    uint64_t address = 0u;

    setup_tree(&tree);
    assert(pmm_init(&tree, 0x40200000u, 0x40220000u,
                    0x40000000u, 0x00100000u) == PMM_OK);
    pmm_get_stats(&stats);
    assert(stats.initialized);
    assert(stats.region_count == 1u);
    assert(stats.total_pages == 16384u);
    assert(stats.reserved_pages == 288u); /* 1 MiB FDT + 128 KiB kernel. */
    assert(stats.free_pages == 16096u);
    assert(stats.allocated_pages == 0u);

    /* Reservations cannot be returned to the free list. */
    assert(!pmm_free_page(0x40000000u));
    assert(!pmm_free_page(0x40200000u));
    assert(!pmm_free_page(0x40001000u));

    assert(pmm_alloc_page(&address));
    assert(address != 0u);
    assert(pmm_is_allocated_page(address));
    assert(!pmm_is_allocated_page(address + 1u));
    assert(!pmm_is_allocated_page(0x40200000u));
    assert((address & (CHOS_PAGE_SIZE - 1u)) == 0u);
    assert(!(address >= 0x40000000u && address < 0x40100000u));
    assert(!(address >= 0x40200000u && address < 0x40220000u));
    pmm_get_stats(&stats);
    assert(stats.free_pages == 16095u);
    assert(stats.allocated_pages == 1u);

    assert(pmm_free_page(address));
    assert(!pmm_is_allocated_page(address));
    assert(!pmm_free_page(address)); /* Double free is rejected. */
    pmm_get_stats(&stats);
    assert(stats.free_pages == 16096u);
    assert(stats.allocated_pages == 0u);
    assert(!pmm_free_page(address + 1u)); /* Unaligned pointer is rejected. */
    assert(!pmm_free_page(0x50000000u)); /* Address outside RAM is rejected. */
}

static void test_multiple_regions(void) {
    fdt_info_t tree;
    pmm_stats_t stats;
    uint64_t address = 0u;

    setup_tree(&tree);
    tree.memory_count = 2u;
    set_ram(&tree, 0u, 0x40000000u, 0x01000000u);
    set_ram(&tree, 1u, 0x42000000u, 0x01000000u);
    assert(pmm_init(&tree, 0x40200000u, 0x40210000u,
                    0x40000000u, 0x00100000u) == PMM_OK);
    pmm_get_stats(&stats);
    assert(stats.region_count == 2u);
    assert(stats.total_pages == 8192u);
    assert(pmm_alloc_page(&address));
    assert((address >= 0x40000000u && address < 0x41000000u) ||
           (address >= 0x42000000u && address < 0x43000000u));
    assert(!(address >= 0x40000000u && address < 0x40100000u));
    assert(!(address >= 0x40200000u && address < 0x40210000u));
    assert(pmm_free_page(address));
}

static void test_bad_ranges(void) {
    fdt_info_t tree;
    setup_tree(&tree);
    tree.memory_count = 2u;
    set_ram(&tree, 0u, 0x40000000u, 0x01000000u);
    set_ram(&tree, 1u, 0x40800000u, 0x01000000u);
    assert(pmm_init(&tree, 0x40200000u, 0x40210000u,
                    0x40000000u, 0x00100000u) == PMM_ERR_OVERLAPPING_RAM);

    setup_tree(&tree);
    set_ram(&tree, 0u, UINT64_MAX - 0x7ffu, 0x2000u);
    assert(pmm_init(&tree, 0x40200000u, 0x40210000u,
                    0x40000000u, 0x00100000u) == PMM_ERR_BAD_RAM_RANGE);

    setup_tree(&tree);
    set_ram(&tree, 0u, 0x40000000u,
            ((uint64_t)PMM_MAX_TRACKED_PAGES + 1u) * CHOS_PAGE_SIZE);
    assert(pmm_init(&tree, 0x40200000u, 0x40210000u,
                    0x40000000u, 0x00100000u) == PMM_ERR_TOO_MUCH_RAM);

    setup_tree(&tree);
    assert(pmm_init(&tree, 0x40220000u, 0x40210000u,
                    0x40000000u, 0x00100000u) == PMM_ERR_BAD_RESERVED_RANGE);
    assert(pmm_init(&tree, 0x40200000u, 0x40210000u,
                    UINT64_MAX - 0x7ffu, 0x1000u) == PMM_ERR_BAD_RESERVED_RANGE);
}

static void test_contiguous_runs(void) {
    fdt_info_t tree;
    pmm_stats_t before, during, after;
    uint64_t base = 0u;
    uint64_t single = 0u;
    uint64_t adjacent = 0u;

    setup_tree(&tree);
    assert(pmm_init(&tree, 0x40200000u, 0x40220000u,
                    0x40000000u, 0x00100000u) == PMM_OK);
    pmm_get_stats(&before);
    assert(pmm_alloc_pages(4u, &base));
    assert((base & (CHOS_PAGE_SIZE - 1u)) == 0u);
    pmm_get_stats(&during);
    assert(during.free_pages + 4u == before.free_pages);
    assert(during.allocated_pages == 4u);
    assert(!pmm_free_pages(base + CHOS_PAGE_SIZE, 2u)); /* Not an allocation base. */
    assert(!pmm_free_pages(base, 5u)); /* Must not partially free the run. */
    pmm_get_stats(&after);
    assert(after.allocated_pages == during.allocated_pages);
    assert(pmm_free_pages(base, 4u));
    assert(!pmm_free_pages(base, 4u)); /* Double free. */
    assert(pmm_alloc_page(&single));
    assert(pmm_alloc_page(&adjacent));
    assert(adjacent == single + CHOS_PAGE_SIZE);
    assert(!pmm_free_pages(single, 2u)); /* Two independent runs cannot be merged on free. */
    assert(pmm_free_page(single));
    assert(pmm_free_page(adjacent));
}

#define PMM_STRESS_THREADS 4u
#define PMM_STRESS_HOLD_COUNT 4u
#define PMM_STRESS_ITERATIONS 200u

static pthread_mutex_t pmm_active_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t pmm_active_pages[PMM_STRESS_THREADS * PMM_STRESS_HOLD_COUNT];
static int pmm_stress_errors;

typedef struct {
    size_t thread_index;
} pmm_stress_argument_t;

static void pmm_note_error(void) {
    (void)__atomic_fetch_add(&pmm_stress_errors, 1, __ATOMIC_RELAXED);
}

static void *pmm_stress_worker(void *argument) {
    const pmm_stress_argument_t *arg = (const pmm_stress_argument_t *)argument;
    uint64_t held[PMM_STRESS_HOLD_COUNT];
    size_t iteration;

    for (iteration = 0u; iteration < PMM_STRESS_ITERATIONS; ++iteration) {
        size_t allocated = 0u;
        size_t i;
        for (i = 0u; i < PMM_STRESS_HOLD_COUNT; ++i) {
            uint64_t address = 0u;
            size_t active_index;
            if (!pmm_alloc_page(&address)) {
                pmm_note_error();
                break;
            }
            pthread_mutex_lock(&pmm_active_mutex);
            for (active_index = 0u;
                 active_index < PMM_STRESS_THREADS * PMM_STRESS_HOLD_COUNT;
                 ++active_index) {
                if (pmm_active_pages[active_index] == address) pmm_note_error();
            }
            pmm_active_pages[arg->thread_index * PMM_STRESS_HOLD_COUNT + i] = address;
            pthread_mutex_unlock(&pmm_active_mutex);
            held[allocated++] = address;
        }

        /* Keep a few pages live simultaneously so uniqueness is tested across threads. */
        for (i = 0u; i < 1200u; ++i) __asm__ volatile("" ::: "memory");

        while (allocated > 0u) {
            uint64_t address = held[--allocated];
            size_t active_index = arg->thread_index * PMM_STRESS_HOLD_COUNT + allocated;
            pthread_mutex_lock(&pmm_active_mutex);
            if (pmm_active_pages[active_index] != address) pmm_note_error();
            pmm_active_pages[active_index] = 0u;
            pthread_mutex_unlock(&pmm_active_mutex);
            if (!pmm_free_page(address)) pmm_note_error();
        }
    }
    return NULL;
}

static void test_concurrent_alloc_free(void) {
    pthread_t threads[PMM_STRESS_THREADS];
    pmm_stress_argument_t args[PMM_STRESS_THREADS];
    pmm_stats_t before, after;
    size_t i;

    memset(pmm_active_pages, 0, sizeof(pmm_active_pages));
    pmm_stress_errors = 0;
    pmm_get_stats(&before);
    assert(before.initialized && before.allocated_pages == 0u);
    for (i = 0u; i < PMM_STRESS_THREADS; ++i) {
        args[i].thread_index = i;
        assert(pthread_create(&threads[i], NULL, pmm_stress_worker, &args[i]) == 0);
    }
    for (i = 0u; i < PMM_STRESS_THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
    assert(pmm_stress_errors == 0);
    pmm_get_stats(&after);
    assert(after.allocated_pages == before.allocated_pages);
    assert(after.free_pages == before.free_pages);
    for (i = 0u; i < PMM_STRESS_THREADS * PMM_STRESS_HOLD_COUNT; ++i) {
        assert(pmm_active_pages[i] == 0u);
    }
}

static void test_fdt_reservations_are_excluded(void) {
    fdt_info_t tree;
    pmm_stats_t stats;
    uint64_t address = 0u;
    unsigned iterations = 0u;

    setup_tree(&tree);
    tree.reserved_count = 2u;
    tree.reserved[0].base = 0x40500000u;
    tree.reserved[0].size = 0x00008000u; /* Eight pages. */
    tree.reserved[0].source = 1u;
    tree.reserved[1].base = 0x40600000u;
    tree.reserved[1].size = 0x00010000u; /* Sixteen pages. */
    tree.reserved[1].source = 2u;

    assert(pmm_init(&tree, 0x40200000u, 0x40220000u,
                    0x40000000u, 0x00100000u) == PMM_OK);
    pmm_get_stats(&stats);
    assert(stats.reserved_pages == 312u); /* DTB + kernel + 8 + 16 pages. */
    assert(!pmm_free_page(0x40500000u));
    assert(!pmm_free_page(0x40600000u));

    /* Walk allocation forward across both reserved holes. The allocator must
       skip them rather than return any part of their pages. */
    while (iterations < 3000u) {
        assert(pmm_alloc_page(&address));
        assert(!(address >= 0x40500000u && address < 0x40508000u));
        assert(!(address >= 0x40600000u && address < 0x40610000u));
        ++iterations;
        if (address >= 0x40700000u) break;
    }
    assert(address >= 0x40700000u);
}

static void test_unsupported_and_invalid_reservations(void) {
    fdt_info_t tree;
    setup_tree(&tree);
    tree.reserved_dynamic_count = 1u;
    assert(pmm_init(&tree, 0x40200000u, 0x40210000u,
                    0x40000000u, 0x00100000u) == PMM_ERR_UNSUPPORTED_RESERVATION);

    setup_tree(&tree);
    tree.reserved_count = 1u;
    tree.reserved[0].base = UINT64_MAX - 0x1000u;
    tree.reserved[0].size = 0x2000u;
    assert(pmm_init(&tree, 0x40200000u, 0x40210000u,
                    0x40000000u, 0x00100000u) == PMM_ERR_BAD_RESERVED_RANGE);
}

int main(void) {
    test_reservations_and_alloc_free();
    test_multiple_regions();
    test_bad_ranges();
    test_contiguous_runs();
    test_concurrent_alloc_free();
    test_fdt_reservations_are_excluded();
    test_unsupported_and_invalid_reservations();
    puts("PMM allocator and concurrent alloc/free stress tests passed");
    return 0;
}
