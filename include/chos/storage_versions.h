#ifndef CHOS_STORAGE_VERSIONS_H
#define CHOS_STORAGE_VERSIONS_H
#include <stddef.h>
#include <stdint.h>
#include "chos/chfs.h"
/* Runtime catalogue ownership. These are opaque identities, not on-disk inode
 * numbers. Remount invalidates every lease; delete/recreate gets a new identity.
 * Caller serializes the ledger with filesystem mutations under storage lock. */
typedef struct {
 uint64_t mount,object,revision;
 uint32_t index,reserved;
} chos_storage_lease_t;
typedef struct {
 uint64_t last_issued,mount;
 uint64_t objects[CHOS_CHFS_MAX_FILES],revisions[CHOS_CHFS_MAX_FILES];
 int live;
} chos_storage_versions_t;
void chos_storage_versions_invalidate(chos_storage_versions_t *);
int chos_storage_versions_mount(chos_storage_versions_t *,const chos_chfs_entry_t *);
/* Check worst-case identity budget before an operation that can mutate metadata.
 * IDs never wrap. Exhaustion leaves existing readers valid but forbids mutation. */
int chos_storage_versions_can_mutate(const chos_storage_versions_t *);
int chos_storage_versions_update(chos_storage_versions_t *,const chos_chfs_entry_t *before,
 const chos_chfs_entry_t *after,size_t force_index);
int chos_storage_versions_get(const chos_storage_versions_t *,size_t,chos_storage_lease_t *);
int chos_storage_versions_match(const chos_storage_versions_t *,const chos_storage_lease_t *);
#endif
