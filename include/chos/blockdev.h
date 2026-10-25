#ifndef CHOS_BLOCKDEV_H
#define CHOS_BLOCKDEV_H

#include <stdint.h>
/* This interface admits only 512-byte logical sectors. Other geometries must
 * be rejected at discovery. LBAs/counts denote whole sectors; caller buffers may
 * be unaligned because DMA drivers use their own isolated bounce storage.
 * A callback returns 1 only for a complete transfer, 0 for failure/short I/O.
 * Ownership is serialized by the caller. Missing flush cannot establish durable
 * writeback: destructive installation explicitly requires a flush callback.
 * Installer identity/geometry comes from a separate live snapshot callback, not
 * the display name or a cached pointer. */
#define CHOS_BLOCK_SECTOR_SIZE 512u

typedef struct chos_block_device chos_block_device_t;
typedef int (*chos_block_read_fn)(chos_block_device_t *device, uint64_t first_sector,
                                  uint32_t sector_count, void *buffer);
typedef int (*chos_block_write_fn)(chos_block_device_t *device, uint64_t first_sector,
                                   uint32_t sector_count, const void *buffer);
typedef int (*chos_block_flush_fn)(chos_block_device_t *device);

struct chos_block_device {
    const char *name;
    uint64_t sector_count;
    void *context;
    chos_block_read_fn read;
    chos_block_write_fn write;
    chos_block_flush_fn flush;
    int ready;
};

int block_read(chos_block_device_t *device, uint64_t first_sector,
               uint32_t sector_count, void *buffer);
int block_write(chos_block_device_t *device, uint64_t first_sector,
                uint32_t sector_count, const void *buffer);
int block_flush(chos_block_device_t *device);

/* Read/write view over a bounded range of a parent block device. Useful for
 * mounting a filesystem inside a GPT partition without altering its LBAs. */
typedef struct {
    chos_block_device_t *parent;
    uint64_t first_sector;
    uint64_t sector_count;
} chos_block_slice_context_t;
int chos_block_slice_init(chos_block_device_t *slice,
                          chos_block_slice_context_t *context,
                          chos_block_device_t *parent,
                          uint64_t first_sector, uint64_t sector_count,
                          const char *name);

#endif
