#ifndef CHOS_NET_STATUS_API_H
#define CHOS_NET_STATUS_API_H
#include <stdint.h>
#define CHOS_NET_STATUS_VERSION 1u
#define CHOS_NET_STATUS_ADAPTER 1u
#define CHOS_NET_STATUS_LINK_KNOWN 2u
#define CHOS_NET_STATUS_LINK_UP 4u
#define CHOS_NET_STATUS_CONFIGURED 8u
#define CHOS_NET_STATUS_ADMIN_ENABLED 16u
#define CHOS_NET_SUPPORT_IPV4 1u
#define CHOS_NET_SUPPORT_DHCP4 2u
#define CHOS_NET_SUPPORT_STATIC4 4u
/* Read-only, no kernel pointers/MMIO addresses and no claim of Internet access. */
typedef struct {
 uint32_t version,size_bytes,flags,state,mode;
 uint8_t mac[6],reserved[2];
 uint32_t ipv4,netmask,gateway,dns,capabilities;
 uint64_t rx_frames,tx_frames;
} chos_net_status_t;
_Static_assert(sizeof(chos_net_status_t)==64,"network status ABI");
#endif
