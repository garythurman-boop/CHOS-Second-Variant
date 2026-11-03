#ifndef CHOS_RESOURCE_PROFILE_H
#define CHOS_RESOURCE_PROFILE_H
#include <stdint.h>
#define CHOS_RESOURCE_LEGACY 0u
#define CHOS_RESOURCE_ENGINE 1u
#define CHOS_RESOURCE_LEGACY_FRAMES 2048u
#define CHOS_RESOURCE_ENGINE_FRAMES 49152u
#define CHOS_RESOURCE_ENGINE_BUCKETS 131072u
#define CHOS_RESOURCE_COMMITTED_FRAMES 65536u
#define CHOS_RESOURCE_LEGACY_IMAGE_SPAN (4u*1024u*1024u)
#define CHOS_RESOURCE_ENGINE_IMAGE_SPAN (64u*1024u*1024u)
#define CHOS_RESOURCE_ENGINE_MAP_BASE UINT64_C(0x28000000)
#define CHOS_RESOURCE_ENGINE_MAP_BYTES (UINT64_C(96)*1024u*1024u)
#define CHOS_RESOURCE_ENGINE_REGIONS 512u
#define CHOS_RESOURCE_ENGINE_RESERVE_MAX (32u*1024u*1024u)
#define CHOS_RESOURCE_ENGINE_STACK_PAGES 256u /* 1MiB main stack; worker policy is separate. */
#define CHOS_RESOURCE_STACK_MASK_WORDS ((CHOS_RESOURCE_ENGINE_STACK_PAGES+63u)/64u)
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
/* Executable policy is selected by the validated ABI note, independently of
 * user runtime hints. Both profiles use the existing private256MiB window. */
typedef struct {
    uint32_t id, image_span, root_stack_pages, map_regions, default_worker_stack_bytes;
    uint64_t map_base, map_bytes;
} chos_execution_profile_t;
static inline int chos_execution_profile_get(uint32_t id, chos_execution_profile_t *out) {
    if (!out) return 0;
    if (id == CHOS_RESOURCE_LEGACY)
        *out = (chos_execution_profile_t){id, CHOS_RESOURCE_LEGACY_IMAGE_SPAN, 8u, 16u, 32768u,
            UINT64_C(0x21000000), UINT64_C(2)*1024u*1024u};
    else if (id == CHOS_RESOURCE_ENGINE)
        *out = (chos_execution_profile_t){id, CHOS_RESOURCE_ENGINE_IMAGE_SPAN,
            CHOS_RESOURCE_ENGINE_STACK_PAGES, CHOS_RESOURCE_ENGINE_REGIONS, 128u*1024u,
            CHOS_RESOURCE_ENGINE_MAP_BASE, CHOS_RESOURCE_ENGINE_MAP_BYTES};
    else return 0;
    return 1;
}
static inline uint32_t chos_execution_stack_mask(uint32_t pages) {
    if(!pages || pages>32u)return 0u;
    return pages==32u?UINT32_MAX:((UINT32_C(1)<<pages)-1u);
}
static inline uint64_t chos_execution_stack_word_mask(uint32_t pages,uint32_t word) {
    if(!pages||pages>CHOS_RESOURCE_ENGINE_STACK_PAGES||word>=CHOS_RESOURCE_STACK_MASK_WORDS||
       word*64u>=pages)return 0;
    uint32_t remaining=pages-word*64u;
    return remaining>=64u?UINT64_MAX:((UINT64_C(1)<<remaining)-1u);
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
