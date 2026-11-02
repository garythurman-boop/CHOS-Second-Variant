#include "chos/frame_ownership.h"
#include <stddef.h>

static int valid_frame(uint64_t physical) {
    return physical && !(physical & 4095u);
}
static int valid_table(const chos_frame_table_t *t) {
    return t && t->entries && t->buckets >= 2u &&
        !(t->buckets & (t->buckets - 1u)) && t->capacity &&
        t->capacity <= t->buckets / 2u && t->count <= t->capacity;
}
static uint32_t home(uint64_t physical, uint32_t mask) {
    /* Mix the complete PFN, including high UEFI RAM; adjacent frames must not
     * all hit the same bucket. No platform address truncation or sentinel PA. */
    uint64_t x = physical >> 12;
    x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return (uint32_t)x & mask;
}
int chos_frame_table_init(chos_frame_table_t *t, chos_owned_frame_t *entries,
        uint32_t buckets, uint32_t capacity) {
    if (!t) return 0;
    chos_frame_table_t candidate = {entries, buckets, capacity, 0u};
    if (!valid_table(&candidate)) return 0;
    /* Do not silently erase live ownership if a caller mistakenly reinitializes
     * occupied storage. The address-space slot is explicitly zeroed on creation. */
    for (uint32_t i = 0; i < buckets; ++i)
        if (entries[i].physical_address || entries[i].mapped_virtual_address) return 0;
    *t = candidate;
    return 1;
}
int chos_frame_table_find(const chos_frame_table_t *t, uint64_t physical, uint32_t *out) {
    if (!valid_table(t) || !valid_frame(physical)) return 0;
    uint32_t mask = t->buckets - 1u, index = home(physical, mask);
    for (uint32_t n = 0; n < t->buckets; ++n, index = (index + 1u) & mask) {
        if (!t->entries[index].physical_address) return 0;
        if (t->entries[index].physical_address == physical) {
            if (out) *out = index;
            return 1;
        }
    }
    return 0;
}
int chos_frame_table_insert(chos_frame_table_t *t, uint64_t physical, uint32_t *out) {
    if (!valid_table(t) || !valid_frame(physical) || t->count == t->capacity) return 0;
    uint32_t mask = t->buckets - 1u, index = home(physical, mask);
    for (uint32_t n = 0; n < t->buckets; ++n, index = (index + 1u) & mask) {
        if (t->entries[index].physical_address == physical) return 0;
        if (!t->entries[index].physical_address) {
            if (t->entries[index].mapped_virtual_address) return 0;
            t->entries[index] = (chos_owned_frame_t){physical, 0u};
            ++t->count;
            if (out) *out = index;
            return 1;
        }
    }
    return 0;
}
int chos_frame_table_prepare_remove(const chos_frame_table_t *t, uint64_t physical,
        chos_frame_remove_t *out) {
    uint32_t index;
    if (!out || !chos_frame_table_find(t, physical, &index) || !t->count ||
        t->entries[index].mapped_virtual_address) return 0;
    uint32_t mask = t->buckets - 1u, end = (index + 1u) & mask;
    for (uint32_t n = 1; n < t->buckets; ++n, end = (end + 1u) & mask) {
        if (!t->entries[end].physical_address) {
            if (t->entries[end].mapped_virtual_address) return 0;
            *out = (chos_frame_remove_t){index, end};
            return 1;
        }
        if (!valid_frame(t->entries[end].physical_address)) return 0;
    }
    return 0;
}
void chos_frame_table_commit_remove(chos_frame_table_t *t, chos_frame_remove_t plan) {
    uint32_t mask = t->buckets - 1u, hole = plan.hole;
    for (uint32_t i = (hole + 1u) & mask; i != plan.end; i = (i + 1u) & mask) {
        uint32_t start = home(t->entries[i].physical_address, mask);
        if (((i - start) & mask) >= ((i - hole) & mask)) {
            t->entries[hole] = t->entries[i];
            hole = i;
        }
    }
    t->entries[hole] = (chos_owned_frame_t){0u, 0u};
    --t->count;
}
