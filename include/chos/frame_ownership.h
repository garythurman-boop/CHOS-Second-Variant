#ifndef CHOS_FRAME_OWNERSHIP_H
#define CHOS_FRAME_OWNERSHIP_H
#include <stdint.h>

/* Internal normal-frame ownership index. The caller supplies zeroed storage,
 * serializes every operation, and never retains an entry index across a mutation.
 * Physical zero is the empty marker; only nonzero 4 KiB frames are accepted.
 * A half-full power-of-two table bounds storage and avoids tombstone buildup. */
typedef struct {
    uint64_t physical_address;
    uint64_t mapped_virtual_address; /* zero means owned, currently unmapped */
} chos_owned_frame_t;
typedef struct {
    chos_owned_frame_t *entries;
    uint32_t buckets, capacity, count;
} chos_frame_table_t;
typedef struct {
    uint32_t hole, end;
} chos_frame_remove_t;

int chos_frame_table_init(chos_frame_table_t *, chos_owned_frame_t *, uint32_t, uint32_t);
int chos_frame_table_find(const chos_frame_table_t *, uint64_t, uint32_t *);
int chos_frame_table_insert(chos_frame_table_t *, uint64_t, uint32_t *);
/* Prepare validates without changing ownership. The caller may then scrub and
 * free the physical page. Commit cannot fail or allocate: the same table must
 * remain locked and unchanged between prepare and commit. Mapped frames refuse
 * removal. Other mapped entries may move while preserving their complete data. */
int chos_frame_table_prepare_remove(const chos_frame_table_t *, uint64_t, chos_frame_remove_t *);
void chos_frame_table_commit_remove(chos_frame_table_t *, chos_frame_remove_t);
#endif
