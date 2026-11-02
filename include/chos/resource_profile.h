#ifndef CHOS_RESOURCE_PROFILE_H
#define CHOS_RESOURCE_PROFILE_H
#include <stdint.h>
#define CHOS_RESOURCE_LEGACY 0u
#define CHOS_RESOURCE_ENGINE 1u
#define CHOS_RESOURCE_LEGACY_FRAMES 2048u
#define CHOS_RESOURCE_ENGINE_FRAMES 49152u
#define CHOS_RESOURCE_ENGINE_BUCKETS 131072u
#define CHOS_RESOURCE_COMMITTED_FRAMES 65536u
#define CHOS_RESOURCE_SYSTEM_RESERVE_FRAMES 16384u /* 64 MiB */
typedef struct {
    uint32_t id, frame_limit, buckets;
} chos_resource_profile_t;
static inline int chos_resource_profile_get(uint32_t id, chos_resource_profile_t *out) {
    if (!out) return 0;
    if (id == CHOS_RESOURCE_LEGACY)
        *out = (chos_resource_profile_t){id, CHOS_RESOURCE_LEGACY_FRAMES, 4096u};
    else if (id == CHOS_RESOURCE_ENGINE)
        *out = (chos_resource_profile_t){id, CHOS_RESOURCE_ENGINE_FRAMES, CHOS_RESOURCE_ENGINE_BUCKETS};
    else return 0;
    return 1;
}
/* Logical commitment is independent of actual physical allocation. Arithmetic
 * is subtraction-based so an overflowing or inconsistent reservation refuses. */
static inline int chos_resource_admit(uint32_t committed, uint64_t free_pages,
        const chos_resource_profile_t *profile) {
    chos_resource_profile_t expected;
    if (!profile || !chos_resource_profile_get(profile->id, &expected) ||
        profile->frame_limit != expected.frame_limit || profile->buckets != expected.buckets ||
        committed > CHOS_RESOURCE_COMMITTED_FRAMES ||
        profile->frame_limit > CHOS_RESOURCE_COMMITTED_FRAMES - committed) return 0;
    return free_pages >= (uint64_t)CHOS_RESOURCE_SYSTEM_RESERVE_FRAMES + profile->frame_limit + 5u;
}
#endif
