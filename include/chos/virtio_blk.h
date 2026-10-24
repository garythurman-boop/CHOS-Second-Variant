#ifndef CHOS_VIRTIO_BLK_H
#define CHOS_VIRTIO_BLK_H
#include <stdint.h>
#include "chos/blockdev.h"
int virtio_blk_init(chos_block_device_t *out);
const char *virtio_blk_last_error(void);
uintptr_t virtio_blk_detected_base(void);
#endif
