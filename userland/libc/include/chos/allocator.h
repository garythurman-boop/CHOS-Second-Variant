#ifndef CHOS_LIBC_ALLOCATOR_H
#define CHOS_LIBC_ALLOCATOR_H
#include <stddef.h>
#include <stdint.h>
#include "chos/user_memory_limits.h"
#include "chos/resource_profile.h"

/* Each backing mapping is private RW/XN. This core requires caller serialization;
 * the native libc wrapper supplies a process-local mutex. The caller owns this
 * metadata; callbacks must not re-enter the allocator. */
#define CHOS_HEAP_REGIONS 16u
#define CHOS_HEAP_MAP_MAX CHOS_USER_MAP_MAX_BYTES
#define CHOS_HEAP_PAGE 4096u
#define CHOS_HEAP_ALIGNMENT 16u
#define CHOS_HEAP_EIO 5
#define CHOS_HEAP_ENOMEM 12
#define CHOS_HEAP_EINVAL 22
typedef void *(*chos_heap_map_fn)(void *,size_t);
typedef int (*chos_heap_unmap_fn)(void *,void *,size_t);
typedef struct { void *base; size_t bytes; unsigned quarantined; } chos_heap_region_t;
typedef struct {
    chos_heap_region_t regions[CHOS_HEAP_REGIONS];
    chos_heap_region_t *active_regions;
    unsigned region_limit;
    size_t mapping_limit;
    chos_heap_map_fn map;
    chos_heap_unmap_fn unmap;
    void *context;
    int error;
} chos_heap_t;
typedef struct {
    size_t mapped_bytes,requested_bytes,allocations,regions,quarantined_regions;
} chos_heap_stats_t;
#ifdef __cplusplus
extern "C" {
#endif
void chos_heap_init(chos_heap_t *,chos_heap_map_fn,chos_heap_unmap_fn,void *);
/* Optional caller-owned metadata; no hidden allocation or larger legacy stack.
 * Storage must remain alive, be serialized with the heap and not overlap it. */
int chos_heap_init_external(chos_heap_t *,chos_heap_map_fn,chos_heap_unmap_fn,void *,chos_heap_region_t *,unsigned);
/* An empty heap may opt into a larger backing-region limit when its backend
 * really provides contiguous mappings. This never changes kernel syscall caps. */
int chos_heap_set_mapping_limit(chos_heap_t *,size_t);
void *chos_heap_alloc(chos_heap_t *,size_t,size_t alignment);
void *chos_heap_calloc(chos_heap_t *,size_t,size_t);
void *chos_heap_realloc(chos_heap_t *,void *,size_t);
int chos_heap_free(chos_heap_t *,void *);
int chos_heap_destroy(chos_heap_t *);
int chos_heap_stats(chos_heap_t *,chos_heap_stats_t *);
#ifdef __cplusplus
}
#endif
#endif
