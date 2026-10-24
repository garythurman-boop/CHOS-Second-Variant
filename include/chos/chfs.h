#ifndef CHOS_CHFS_H
#define CHOS_CHFS_H

#include <stddef.h>
#include <stdint.h>
#include "chos/blockdev.h"

#define CHOS_CHFS_VERSION 3u
#define CHOS_CHFS_V2_VERSION 2u
#define CHOS_CHFS_V1_VERSION 1u
#define CHOS_CHFS_MAX_FILES 511u
#define CHOS_CHFS_V2_MAX_FILES 63u
#define CHOS_CHFS_V1_MAX_FILES 23u
#define CHOS_CHFS_PATH_MAX 48u
#define CHOS_CHFS_DIRECTORY_SECTORS 64u
#define CHOS_CHFS_V2_DIRECTORY_SECTORS 8u
#define CHOS_CHFS_V1_DIRECTORY_SECTORS 3u
#define CHOS_CHFS_V3_DIR_A_LBA 1u
#define CHOS_CHFS_V3_DIR_B_LBA 65u
#define CHOS_CHFS_V3_DATA_LBA 129u
#define CHOS_CHFS_V1_DIR_A_LBA 1u
#define CHOS_CHFS_V1_DIR_B_LBA 4u
#define CHOS_CHFS_V1_DATA_LBA 7u
#define CHOS_CHFS_DATA_LBA CHOS_CHFS_V3_DATA_LBA
/* Legacy v1 directory LBAs; v2 stores metadata at the volume tail. V3 uses
 * two 64-sector front catalogues to support 511 persistent entries. */
#define CHOS_CHFS_DIR_A_LBA CHOS_CHFS_V1_DIR_A_LBA
#define CHOS_CHFS_DIR_B_LBA CHOS_CHFS_V1_DIR_B_LBA
#define CHOS_CHFS_FILE_MAX_BYTES (4u * 1024u * 1024u)
#define CHOS_CHFS_ENTRY_DIRECTORY_TAG 0x52494443u /* "CDIR" in little-endian metadata */

typedef struct {
    char path[CHOS_CHFS_PATH_MAX];
    uint32_t first_sector;
    uint32_t sector_count;
    uint32_t size_bytes;
    uint32_t crc32;
} chos_chfs_entry_t;

typedef struct {
    chos_block_device_t *device;
    chos_chfs_entry_t entries[CHOS_CHFS_MAX_FILES];
    uint64_t total_sectors;
    uint32_t active_directory_lba;
    uint32_t directory_a_lba;
    uint32_t directory_b_lba;
    uint32_t directory_sectors;
    uint32_t data_start_lba;
    uint32_t data_end_lba; /* exclusive; v2 reserves its final 16 sectors for metadata */
    uint32_t version;
    uint32_t generation;
    int mounted;
} chos_chfs_t;

typedef struct {
    size_t file_count;
    size_t directory_count;
    size_t used_sectors;
    size_t free_sectors;
    uint32_t generation;
    uint64_t capacity_bytes;
} chos_chfs_stats_t;

uint32_t chfs_crc32(const void *data, size_t length);
int chfs_format(chos_block_device_t *device);
int chfs_mount(chos_chfs_t *fs, chos_block_device_t *device);
int chfs_validate(chos_chfs_t *fs);
int chfs_lookup(chos_chfs_t *fs, const char *path, chos_chfs_entry_t *out);
int chfs_entry_is_directory(const chos_chfs_entry_t *entry);
int chfs_is_directory(chos_chfs_t *fs, const char *path);
int chfs_make_directory(chos_chfs_t *fs, const char *path);
int chfs_remove_directory(chos_chfs_t *fs, const char *path);
int chfs_sync(chos_chfs_t *fs);
int chfs_read_file(chos_chfs_t *fs, const char *path, void *buffer, size_t capacity,
                   size_t *bytes_read);
int chfs_write_file(chos_chfs_t *fs, const char *path, const void *data, size_t size);
int chfs_remove_file(chos_chfs_t *fs, const char *path);
int chfs_rename_file(chos_chfs_t *fs, const char *old_path, const char *new_path);
int chfs_rename_directory(chos_chfs_t *fs, const char *old_path, const char *new_path);
int chfs_get_at(chos_chfs_t *fs, size_t index, chos_chfs_entry_t *out);
void chfs_get_stats(chos_chfs_t *fs, chos_chfs_stats_t *out);

#endif
