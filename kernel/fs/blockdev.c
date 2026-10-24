#include "chos/blockdev.h"
#include <stddef.h>

int block_read(chos_block_device_t *device, uint64_t first_sector,
               uint32_t sector_count, void *buffer) {
    if (device == NULL || !device->ready || device->read == NULL || buffer == NULL ||
        sector_count == 0u || first_sector >= device->sector_count ||
        sector_count > device->sector_count - first_sector) return 0;
    return device->read(device, first_sector, sector_count, buffer);
}

int block_write(chos_block_device_t *device, uint64_t first_sector,
                uint32_t sector_count, const void *buffer) {
    if (device == NULL || !device->ready || device->write == NULL || buffer == NULL ||
        sector_count == 0u || first_sector >= device->sector_count ||
        sector_count > device->sector_count - first_sector) return 0;
    return device->write(device, first_sector, sector_count, buffer);
}

int block_flush(chos_block_device_t *device) {
    if (device == NULL || !device->ready) return 0;
    return device->flush == NULL || device->flush(device);
}

static int slice_read(chos_block_device_t *device, uint64_t first_sector,
                      uint32_t sector_count, void *buffer) {
    chos_block_slice_context_t *ctx = (chos_block_slice_context_t *)device->context;
    if (ctx == NULL || ctx->parent == NULL || first_sector > ctx->sector_count ||
        sector_count > ctx->sector_count - first_sector ||
        ctx->first_sector > UINT64_MAX - first_sector) return 0;
    return block_read(ctx->parent, ctx->first_sector + first_sector, sector_count, buffer);
}
static int slice_write(chos_block_device_t *device, uint64_t first_sector,
                       uint32_t sector_count, const void *buffer) {
    chos_block_slice_context_t *ctx = (chos_block_slice_context_t *)device->context;
    if (ctx == NULL || ctx->parent == NULL || ctx->parent->write == NULL ||
        first_sector > ctx->sector_count || sector_count > ctx->sector_count - first_sector ||
        ctx->first_sector > UINT64_MAX - first_sector) return 0;
    return block_write(ctx->parent, ctx->first_sector + first_sector, sector_count, buffer);
}
static int slice_flush(chos_block_device_t *device) {
    chos_block_slice_context_t *ctx = (chos_block_slice_context_t *)device->context;
    return ctx != NULL && ctx->parent != NULL && block_flush(ctx->parent);
}
int chos_block_slice_init(chos_block_device_t *slice,
                          chos_block_slice_context_t *context,
                          chos_block_device_t *parent,
                          uint64_t first_sector, uint64_t sector_count,
                          const char *name) {
    if (slice == NULL || context == NULL || parent == NULL || !parent->ready ||
        first_sector >= parent->sector_count || sector_count == 0u ||
        sector_count > parent->sector_count - first_sector || parent->read == NULL) return 0;
    context->parent = parent;
    context->first_sector = first_sector;
    context->sector_count = sector_count;
    slice->name = name;
    slice->sector_count = sector_count;
    slice->context = context;
    slice->read = slice_read;
    slice->write = parent->write != NULL ? slice_write : NULL;
    slice->flush = parent->flush != NULL ? slice_flush : NULL;
    slice->ready = 1;
    return 1;
}
