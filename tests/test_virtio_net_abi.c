#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/virtio_net_abi.h"

int main(void) {
    static const uint8_t frame[] = {
        0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,
        0x52u,0x54u,0x00u,0x12u,0x34u,0x56u,
        0x08u,0x00u,0x45u,0x00u
    };
    uint8_t descriptor_payload[CHOS_VIRTIO_NET_HEADER_V1_SIZE + sizeof(frame)];
    chos_virtio_net_header_v1_t header = {0};

    assert(sizeof(header) == 12u);
    assert(offsetof(chos_virtio_net_header_v1_t, num_buffers) == 10u);
    assert(header.num_buffers == 0u);
    memcpy(descriptor_payload, &header, sizeof(header));
    memcpy(descriptor_payload + sizeof(header), frame, sizeof(frame));

    /* Simulate what the device receives after stripping the VirtIO header. */
    assert(memcmp(descriptor_payload + CHOS_VIRTIO_NET_HEADER_V1_SIZE,
                  frame, sizeof(frame)) == 0);
    assert(memcmp(descriptor_payload + CHOS_VIRTIO_NET_HEADER_V1_SIZE,
                  "\xff\xff\xff\xff\xff\xff", 6u) == 0);
    assert(descriptor_payload[CHOS_VIRTIO_NET_HEADER_V1_SIZE + 12u] == 0x08u);
    assert(descriptor_payload[CHOS_VIRTIO_NET_HEADER_V1_SIZE + 13u] == 0x00u);

    puts("VirtIO 1.x network ABI: 12-byte header, num_buffers offset, and Ethernet alignment passed");
    return 0;
}
