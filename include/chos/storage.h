#ifndef CHOS_STORAGE_H
#define CHOS_STORAGE_H
#include <stddef.h>
#include <stdint.h>
#include "chos/blockdev.h"
#include "chos/chfs.h"
#include "chos/storage_versions.h"

int storage_init(void);
/* Single-CPU installer ownership transfer: no filesystem I/O may be active. */
void storage_detach(void);
/* Attach an already initialized platform block backend without auto-formatting. */
int storage_attach_block_device(chos_block_device_t *device);
const char *storage_error(void);
int storage_block_ready(void);
int storage_mounted(void);
int storage_format(void);
int storage_mount(void);
chos_block_device_t *storage_device(void);
chos_chfs_t *storage_filesystem(void);
int storage_lookup(const char *path, chos_chfs_entry_t *out);
int storage_read_file(const char *path, void *buffer, size_t capacity, size_t *bytes_read);
int storage_write_file(const char *path, const void *data, size_t size);
/* Kernel-owned transactional snapshots. Output is committed only on success.
 * A mount/identity/content change makes the old lease stale; unrelated files do
 * not invalidate it. No inode persistence or public user-token authority implied. */
int storage_read_snapshot(const char *,void *,size_t,size_t *,chos_storage_lease_t *);
int storage_commit_snapshot(const char *,const chos_storage_lease_t *,const void *,size_t,chos_storage_lease_t *);
int storage_write_ready(void);
int storage_remove_file(const char *path);
int storage_rename_file(const char *old_path, const char *new_path);
int storage_rename_directory(const char *old_path, const char *new_path);
int storage_make_directory(const char *path);
int storage_remove_directory(const char *path);
int storage_is_directory(const char *path);
int storage_sync(void);
int storage_get_at(size_t index, chos_chfs_entry_t *out);
void storage_get_stats(chos_chfs_stats_t *out);
#endif
