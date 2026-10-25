#ifndef CHOS_NET_DEVICE_H
#define CHOS_NET_DEVICE_H
#include <stdint.h>
#include <stddef.h>
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
} chos_net_device_info_t;

/* Single selected Ethernet adapter. A new backend supplies these five callbacks;
 * IP/DHCP/DNS/TCP/browser remain independent of its PCI/register/queue model.
 * A claimed/failed adapter is never rebound to a second DMA owner. */
typedef struct {
 const char *name;
 int (*ready)(void);
 void (*get_info)(chos_net_device_info_t *);
 int (*send)(const void *,size_t);
 int (*receive)(void *,size_t,size_t *);
 const char *(*error)(void);
} chos_net_device_ops_t;
int chos_net_device_bind(const chos_net_device_ops_t *ops);
int chos_net_device_init(void);
void chos_net_device_note_unclaimed(const char *reason);
int chos_net_device_ready(void);
const char *chos_net_device_name(void);
const char *chos_net_device_error(void);
void chos_net_device_get_info(chos_net_device_info_t *out);
int chos_net_device_send(const void *frame,size_t bytes);
int chos_net_device_receive(void *frame,size_t capacity,size_t *bytes);
#endif
