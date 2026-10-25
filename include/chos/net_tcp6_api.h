#ifndef CHOS_NET_TCP6_API_H
#define CHOS_NET_TCP6_API_H
#include <stdint.h>
#include "chos/net_tcp_api.h"
#define CHOS_TCP6_API_VERSION 1u
#define CHOS_TCP6_MAX_SOCKETS 4u
#define CHOS_TCP6_MAX_TRANSFER 512u
#define CHOS_TCP6_RX_CAPACITY 2048u
/* All addresses are 16 network-order bytes. Scope is the OS-selected interface;
 * no raw NIC, route configuration, or device access is exposed. */
typedef struct {
 uint32_t version,size_bytes;
 int32_t handle;
 uint32_t state,error,receive_eof;
 uint8_t local_address[16],remote_address[16];
 uint16_t local_port,remote_port;
 uint32_t receive_available,send_queued,reserved;
 uint64_t bytes_sent,bytes_received;
} chos_tcp6_socket_info_t;
_Static_assert(sizeof(chos_tcp6_socket_info_t)==88u,"TCP6 status ABI");
#endif
