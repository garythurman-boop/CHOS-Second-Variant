#ifndef CHOS_VIRTIO_NET_H
#define CHOS_VIRTIO_NET_H

#include <stddef.h>
#include <stdint.h>

#define CHOS_NET_MAC_BYTES 6u
#define CHOS_NET_MAX_ETHERNET_FRAME 1518u

typedef struct {
    uint8_t mac[CHOS_NET_MAC_BYTES];
    uint16_t device_status;
    uint64_t rx_frames;
    uint64_t tx_frames;
    uint64_t rx_dropped;
    uint64_t tx_errors;
    uint16_t rx_queue_size;
    uint16_t rx_queue_num_readback;
    uint16_t rx_available_idx;
    uint16_t rx_device_used_idx;
    uint16_t rx_consumed_idx;
    uint32_t rx_queue_ready;
    uint32_t interrupt_status;
    uintptr_t mmio_base;
    int ready;
    int link_status_known;
    int link_up;
} chos_virtio_net_info_t;

/* Polled VirtIO-net backend for QEMU virt. Raw Ethernet only in this milestone;
 * DHCP/IP/TCP sockets are layered on later and are not implied by driver-ready. */
int virtio_net_init(void);
int virtio_net_ready(void);
const char *virtio_net_last_error(void);
void virtio_net_get_info(chos_virtio_net_info_t *out);
/* Send one complete Ethernet frame without FCS. Returns 1 on TX completion. */
int virtio_net_send_frame(const void *frame, size_t length);
/* Returns 1 with a frame, 0 if no frame is queued, or -1 on invalid/unavailable. */
int virtio_net_receive_frame(void *frame, size_t capacity, size_t *length_out);

#endif
