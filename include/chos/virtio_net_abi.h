#ifndef CHOS_VIRTIO_NET_ABI_H
#define CHOS_VIRTIO_NET_ABI_H

#include <stdint.h>

/* VirtIO 1.x (VIRTIO_F_VERSION_1) network header. The v1 header is 12 bytes:
 * the final num_buffers field is present even when mergeable RX buffers are
 * not negotiated. A 10-byte legacy header shifts the Ethernet frame by two
 * bytes on modern QEMU devices, corrupting the destination MAC and EtherType.
 */
#define CHOS_VIRTIO_NET_HEADER_V1_SIZE 12u

typedef struct __attribute__((packed)) {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} chos_virtio_net_header_v1_t;

_Static_assert(sizeof(chos_virtio_net_header_v1_t) == CHOS_VIRTIO_NET_HEADER_V1_SIZE,
               "VirtIO 1.x network header must be 12 bytes");

#endif
