#ifndef CHOS_VIRTIO_NET_H
#define CHOS_VIRTIO_NET_H

#include <stddef.h>
#include <stdint.h>

#include "chos/net_device.h"
typedef chos_net_device_info_t chos_virtio_net_info_t;

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
