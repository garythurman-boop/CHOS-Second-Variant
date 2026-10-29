#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "chos/kheap.h"
#include "chos/pmm.h"

#define FAKE_PAGE_COUNT 128u
_Alignas(CHOS_PAGE_SIZE) static uint8_t fake_ram[FAKE_PAGE_COUNT * CHOS_PAGE_SIZE];
static uint8_t fake_allocated[FAKE_PAGE_COUNT];
static int fake_pmm_ready = 1;
static int fake_free_failure;

int pmm_alloc_pages(size_t count, uint64_t *physical_address) {
    size_t start, i;
    if (!fake_pmm_ready || physical_address == NULL || count == 0u || count > FAKE_PAGE_COUNT) return 0;
    for (start = 0u; start + count <= FAKE_PAGE_COUNT; ++start) {
        for (i = 0u; i < count; ++i) if (fake_allocated[start + i]) break;
        if (i == count) {
            for (i = 0u; i < count; ++i) fake_allocated[start + i] = 1u;
            *physical_address = (uint64_t)(uintptr_t)&fake_ram[start * CHOS_PAGE_SIZE];
            return 1;
        }
        start += i;
    }
    return 0;
}

int pmm_free_pages(uint64_t physical_address, size_t count) {
    uintptr_t base = (uintptr_t)fake_ram;
    uintptr_t value = (uintptr_t)physical_address;
    size_t start, i;
    if (fake_free_failure) return 0;
    if (count == 0u || value < base || value >= base + sizeof(fake_ram) ||
        ((value - base) % CHOS_PAGE_SIZE) != 0u) return 0;
    start = (size_t)((value - base) / CHOS_PAGE_SIZE);
    if (count > FAKE_PAGE_COUNT - start) return 0;
    for (i = 0u; i < count; ++i) if (!fake_allocated[start + i]) return 0;
    for (i = 0u; i < count; ++i) fake_allocated[start + i] = 0u;
    return 1;
}

void pmm_get_stats(pmm_stats_t *out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    out->initialized = fake_pmm_ready;
    out->error = fake_pmm_ready ? PMM_OK : PMM_ERR_NO_RAM;
}

static void reset_fake_pmm(void) {
    memset(fake_allocated, 0, sizeof(fake_allocated));
    memset(fake_ram, 0, sizeof(fake_ram));
    fake_pmm_ready = 1;
    fake_free_failure = 0;
}

static void test_bootstrap_to_runtime_transition(void) {
    kheap_stats_t stats;
    uint8_t *payload;

    reset_fake_pmm();
    kheap_begin_bootstrap();
    assert(kheap_is_bootstrap_mode());
    assert(kheap_init());
    payload = (uint8_t *)kheap_alloc(512u);
    assert(payload != NULL);
    payload[0] = 0x5au;
    kheap_get_stats(&stats);
    assert(stats.initialized && stats.arena_count == 1u);
    assert(stats.allocated_blocks == 1u);
    assert(kheap_free(payload));

    kheap_enable_runtime_locking();
    assert(!kheap_is_bootstrap_mode());
    payload = (uint8_t *)kheap_alloc(128u);
    assert(payload != NULL);
    assert(kheap_free(payload));
}

static void test_heap_lifecycle(void) {
    kheap_stats_t before, allocated, freed;
    uint8_t *a, *b, *c;
    size_t i;

    reset_fake_pmm();
    kheap_begin_bootstrap();
    assert(kheap_is_bootstrap_mode());
    assert(kheap_init());
    kheap_get_stats(&before);
    assert(before.initialized && before.arena_count == 0u);
    assert(kheap_alloc(0u) == NULL);

    a = (uint8_t *)kheap_alloc(37u);
    b = (uint8_t *)kheap_alloc(700u);
    c = (uint8_t *)kheap_alloc(20000u);
    assert(a != NULL && b != NULL && c != NULL);
    assert(((uintptr_t)a % KHEAP_ALIGNMENT) == 0u);
    assert(((uintptr_t)b % KHEAP_ALIGNMENT) == 0u);
    assert(((uintptr_t)c % KHEAP_ALIGNMENT) == 0u);

    for (i = 0u; i < 37u; ++i) a[i] = (uint8_t)(i ^ 0x5au);
    for (i = 0u; i < 700u; ++i) b[i] = (uint8_t)(i ^ 0xa5u);
    for (i = 0u; i < 20000u; ++i) c[i] = (uint8_t)(i ^ 0x3cu);
    for (i = 0u; i < 37u; ++i) assert(a[i] == (uint8_t)(i ^ 0x5au));
    for (i = 0u; i < 700u; ++i) assert(b[i] == (uint8_t)(i ^ 0xa5u));
    for (i = 0u; i < 20000u; ++i) assert(c[i] == (uint8_t)(i ^ 0x3cu));

    kheap_get_stats(&allocated);
    assert(allocated.allocated_blocks == 3u);
    assert(allocated.allocated_payload_bytes >= 37u + 700u + 20000u);
    assert(allocated.arena_count >= 2u);
    assert(!kheap_free(a + 1));
    assert(!kheap_free((void *)(uintptr_t)0x1234u));
    assert(kheap_free(b));
    assert(kheap_free(a));
    assert(kheap_free(c));
    assert(!kheap_free(a));

    kheap_get_stats(&freed);
    assert(freed.allocated_blocks == 0u);
    assert(freed.allocated_payload_bytes == 0u);
    assert(freed.free_blocks == freed.arena_count); /* Every arena coalesced to one free block. */

    /* Reuse free blocks, then verify a large block can be allocated again. */
    a = (uint8_t *)kheap_alloc(128u);
    c = (uint8_t *)kheap_alloc(20000u);
    assert(a != NULL && c != NULL);
    assert(kheap_free(c));
    assert(kheap_free(a));
    kheap_enable_runtime_locking();
    assert(!kheap_is_bootstrap_mode());
}

static unsigned owned_pages(void){unsigned n=0;for(unsigned i=0;i<FAKE_PAGE_COUNT;i++)n+=fake_allocated[i];return n;}
static void test_transient_small_arena_return(void){
    reset_fake_pmm();kheap_begin_bootstrap();assert(kheap_init());kheap_enable_runtime_locking();
    uint8_t *anchor=kheap_alloc(256);assert(anchor);memset(anchor,0x73,256);
    unsigned baseline=owned_pages();uint8_t *scratch=kheap_alloc(16000);assert(scratch);
    assert(owned_pages()==baseline+KHEAP_MIN_ARENA_PAGES);
    assert(kheap_free(scratch));assert(owned_pages()==baseline);
    for(unsigned i=0;i<256;i++)assert(anchor[i]==0x73);
    assert(kheap_free(anchor)&&!owned_pages());
}
static void test_empty_arena_return(void){
    reset_fake_pmm();kheap_begin_bootstrap();assert(kheap_init());kheap_enable_runtime_locking();
    uint8_t *anchor=kheap_alloc(128);assert(anchor);anchor[0]=0x73;
    unsigned baseline=owned_pages();uint8_t *large=kheap_alloc(20000);assert(large);
    unsigned peak=owned_pages();assert(peak>baseline);
    fake_free_failure=1;assert(kheap_free(large));assert(owned_pages()==peak&&anchor[0]==0x73);
    kheap_stats_t quarantined;kheap_get_stats(&quarantined);
    assert(quarantined.quarantined_arenas==1&&quarantined.quarantined_pages==peak-baseline);
    uint8_t *again=kheap_alloc(20000);assert(again&&again!=large); /* uncertain physical ownership never reused */
    fake_free_failure=0;assert(kheap_free(again));assert(owned_pages()==peak&&anchor[0]==0x73);
    assert(!kheap_free(again)); /* released arena is no longer in the heap list */
    assert(kheap_free(anchor));
    /* A fresh simulated boot resets the deliberately quarantined PMM model. */
    reset_fake_pmm();kheap_begin_bootstrap();assert(kheap_init());kheap_enable_runtime_locking();
    anchor=kheap_alloc(128);assert(anchor);baseline=owned_pages();large=kheap_alloc(20000);assert(large);
    assert(kheap_free(large)&&owned_pages()==baseline&&kheap_free(anchor));
    void *a=kheap_alloc(8192),*b=kheap_alloc(8192),*c=kheap_alloc(8192);assert(a&&b&&c);
    assert(kheap_free(a)&&kheap_free(b)&&kheap_free(c));
    kheap_stats_t stats;kheap_get_stats(&stats);
    assert(!stats.allocated_blocks&&!stats.arena_count&&!stats.total_pages);
    assert(!owned_pages());
}

#define KHEAP_STRESS_THREADS 4u
#define KHEAP_STRESS_ITERATIONS 300u

static int kheap_stress_errors;

typedef struct {
    size_t thread_index;
} kheap_stress_argument_t;

static void kheap_note_error(void) {
    (void)__atomic_fetch_add(&kheap_stress_errors, 1, __ATOMIC_RELAXED);
}

static void *kheap_stress_worker(void *argument) {
    const kheap_stress_argument_t *arg = (const kheap_stress_argument_t *)argument;
    size_t iteration;
    for (iteration = 0u; iteration < KHEAP_STRESS_ITERATIONS; ++iteration) {
        size_t size = 32u + ((iteration * 37u + arg->thread_index * 113u) % 2048u);
        uint8_t pattern = (uint8_t)(0x31u + arg->thread_index * 17u + iteration);
        uint8_t *payload = (uint8_t *)kheap_alloc(size);
        size_t i;
        if (payload == NULL) {
            kheap_note_error();
            continue;
        }
        for (i = 0u; i < size; ++i) {
            payload[i] = (uint8_t)(pattern ^ (uint8_t)(i * 13u));
        }
        for (i = 0u; i < size; ++i) {
            if (payload[i] != (uint8_t)(pattern ^ (uint8_t)(i * 13u))) {
                kheap_note_error();
                break;
            }
        }
        if (!kheap_free(payload)) kheap_note_error();
    }
    return NULL;
}

static void test_concurrent_alloc_free(void) {
    pthread_t threads[KHEAP_STRESS_THREADS];
    kheap_stress_argument_t args[KHEAP_STRESS_THREADS];
    kheap_stats_t before, after;
    size_t i;

    kheap_stress_errors = 0;
    kheap_get_stats(&before);
    assert(before.initialized && before.allocated_blocks == 0u);
    for (i = 0u; i < KHEAP_STRESS_THREADS; ++i) {
        args[i].thread_index = i;
        assert(pthread_create(&threads[i], NULL, kheap_stress_worker, &args[i]) == 0);
    }
    for (i = 0u; i < KHEAP_STRESS_THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
    assert(kheap_stress_errors == 0);
    kheap_get_stats(&after);
    assert(after.allocated_blocks == before.allocated_blocks);
    assert(after.allocated_payload_bytes == before.allocated_payload_bytes);
    assert(after.free_blocks == after.arena_count);
}

static void test_uninitialized_pmm(void) {
    fake_pmm_ready = 0;
    assert(!kheap_init());
    assert(kheap_alloc(16u) == NULL);
    fake_pmm_ready = 1;
    fake_free_failure = 0;
}

int main(void) {
    test_uninitialized_pmm();
    test_bootstrap_to_runtime_transition();
    test_heap_lifecycle();
    test_transient_small_arena_return();
    test_empty_arena_return();
    test_concurrent_alloc_free();
    puts("Kernel heap lifecycle and concurrent alloc/free stress tests passed");
    return 0;
}
