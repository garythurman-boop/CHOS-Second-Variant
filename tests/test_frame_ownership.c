#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chos/frame_ownership.h"

static uint64_t state = UINT64_C(0x7dbac671c20a1f3d);
static uint32_t random32(void) {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    return (uint32_t)state;
}
static uint64_t physical(uint32_t id) {
    /* Same low 32 bits recur in distinct high physical banks. */
    return UINT64_C(0x10000000000) + ((uint64_t)(id & 255u) << 32) +
        ((uint64_t)(id >> 8) + 1u) * 4096u;
}
static void erase(chos_frame_table_t *t, uint64_t pa) {
    chos_frame_remove_t plan;
    assert(chos_frame_table_prepare_remove(t, pa, &plan));
    chos_frame_table_commit_remove(t, plan);
    assert(!chos_frame_table_find(t, pa, NULL));
}
static void check_model(chos_frame_table_t *t, const uint8_t *live, uint32_t ids) {
    uint32_t count = 0, occupied = 0;
    for (uint32_t id = 0; id < ids; ++id) {
        uint32_t at = UINT32_MAX;
        assert(chos_frame_table_find(t, physical(id), &at) == (live[id] != 0));
        if (live[id]) {
            ++count;
            assert(at < t->buckets && t->entries[at].physical_address == physical(id));
            assert(t->entries[at].mapped_virtual_address == ((uint64_t)id + 1u) * 4096u);
        }
    }
    for (uint32_t i = 0; i < t->buckets; ++i) {
        occupied += t->entries[i].physical_address != 0;
        if (!t->entries[i].physical_address) assert(!t->entries[i].mapped_virtual_address);
    }
    assert(t->count == count && occupied == count);
}
static void churn(uint32_t capacity, uint32_t buckets, uint32_t iterations) {
    chos_owned_frame_t *entries = calloc(buckets, sizeof(*entries));
    uint8_t *live = calloc(capacity * 2u, 1);
    assert(entries && live);
    chos_frame_table_t t;
    assert(chos_frame_table_init(&t, entries, buckets, capacity));
    for (uint32_t id = 0; id < capacity; ++id) {
        uint32_t at;
        assert(chos_frame_table_insert(&t, physical(id), &at));
        entries[at].mapped_virtual_address = ((uint64_t)id + 1u) * 4096u;
        live[id] = 1;
    }
    uint32_t at = UINT32_MAX;
    assert(!chos_frame_table_insert(&t, physical(capacity), &at) && at == UINT32_MAX);
    check_model(&t, live, capacity * 2u);
    for (uint32_t n = 0; n < iterations; ++n) {
        uint32_t id = random32() % (capacity * 2u);
        if (live[id]) {
            assert(chos_frame_table_find(&t, physical(id), &at));
            assert(entries[at].mapped_virtual_address == ((uint64_t)id + 1u) * 4096u);
            chos_frame_remove_t plan = {UINT32_MAX, UINT32_MAX};
            assert(!chos_frame_table_prepare_remove(&t, physical(id), &plan));
            assert(plan.hole == UINT32_MAX && plan.end == UINT32_MAX);
            entries[at].mapped_virtual_address = 0;
            erase(&t, physical(id)); live[id] = 0;
        } else {
            uint32_t count = t.count;
            int ok = chos_frame_table_insert(&t, physical(id), &at);
            assert(ok == (count < capacity));
            if (ok) {
                live[id] = 1; entries[at].mapped_virtual_address = ((uint64_t)id + 1u) * 4096u;
                assert(!chos_frame_table_insert(&t, physical(id), NULL));
            }
        }
        if (!(n % 8192u)) check_model(&t, live, capacity * 2u);
    }
    check_model(&t, live, capacity * 2u);
    for (uint32_t id = capacity * 2u; id-- > 0;) if (live[id]) {
        assert(chos_frame_table_find(&t, physical(id), &at));
        entries[at].mapped_virtual_address = 0; erase(&t, physical(id));
    }
    assert(!t.count);
    for (uint32_t i = 0; i < buckets; ++i)
        assert(!entries[i].physical_address && !entries[i].mapped_virtual_address);
    free(entries); free(live);
}
static void wrapping_collisions(void) {
    chos_owned_frame_t entries[64] = {{0}};
    chos_frame_table_t t;
    uint64_t keys[32]; uint32_t found = 0;
    assert(chos_frame_table_init(&t, entries, 64, 32));
    for (uint64_t pa = 4096; found < 32; pa += 4096) {
        uint32_t at;
        assert(chos_frame_table_insert(&t, pa, &at));
        if (at == 63) keys[found++] = pa;
        erase(&t, pa);
    }
    for (uint32_t pass = 0; pass < 3; ++pass) {
        for (uint32_t i = 0; i < 32; ++i) {
            uint32_t at;
            assert(chos_frame_table_insert(&t, keys[i], &at));
            assert(at == ((63u + i) & 63u));
            entries[at].mapped_virtual_address = 4096u * (i + 1u);
        }
        for (uint32_t i = 0; i < 32; ++i) {
            uint32_t victim = pass == 0 ? i : pass == 1 ? 31u - i : (i * 13u) & 31u;
            uint32_t at;
            assert(chos_frame_table_find(&t, keys[victim], &at));
            assert(entries[at].mapped_virtual_address == (victim + 1u) * 4096u);
            entries[at].mapped_virtual_address = 0; erase(&t, keys[victim]);
            for (uint32_t j = 0; j < 32; ++j)
                if (chos_frame_table_find(&t, keys[j], &at))
                    assert(entries[at].mapped_virtual_address == (j + 1u) * 4096u);
        }
        assert(!t.count);
    }
}
static void invalid_and_atomic(void) {
    chos_owned_frame_t entries[8] = {{0}}, before[8];
    chos_frame_table_t t = {0}, old = t;
    assert(!chos_frame_table_init(NULL, entries, 8, 4));
    assert(!chos_frame_table_init(&t, NULL, 8, 4));
    assert(!chos_frame_table_init(&t, entries, 7, 3));
    assert(!chos_frame_table_init(&t, entries, 8, 5));
    assert(!memcmp(&t, &old, sizeof(t)));
    assert(chos_frame_table_init(&t, entries, 8, 4));
    assert(!chos_frame_table_insert(&t, 0, NULL));
    assert(!chos_frame_table_insert(&t, 4097, NULL));
    assert(chos_frame_table_insert(&t, UINT64_MAX & ~UINT64_C(4095), NULL));
    old = t; memcpy(before, entries, sizeof(entries));
    assert(!chos_frame_table_init(&t, entries, 8, 4));
    assert(!memcmp(&t, &old, sizeof(t)) && !memcmp(entries, before, sizeof(entries)));
    chos_frame_remove_t plan;
    assert(!chos_frame_table_prepare_remove(&t, 4096, &plan));
    assert(!chos_frame_table_prepare_remove(&t, UINT64_MAX & ~UINT64_C(4095), NULL));
    assert(!memcmp(entries, before, sizeof(entries)));
    erase(&t, UINT64_MAX & ~UINT64_C(4095));
    /* A malformed full cluster cannot loop or release a PMM frame. */
    for (uint32_t i = 0; i < 8; ++i) entries[i].physical_address = (i + 1u) * 4096u;
    t.count = 4; memcpy(before, entries, sizeof(entries));
    assert(!chos_frame_table_prepare_remove(&t, 4096, &plan));
    assert(!memcmp(entries, before, sizeof(entries)) && t.count == 4);
    assert(!chos_frame_table_find(&t, 81920, NULL));
}
int main(void) {
    invalid_and_atomic(); wrapping_collisions();
    churn(2048, 4096, 500000);
    churn(49152, 131072, 2000000);
    puts("frame ownership: PASS; 2500000 model operations, 2048/49152-frame capacities, high PAs, wrapping collisions, mapped refusal and hole reuse");
}
