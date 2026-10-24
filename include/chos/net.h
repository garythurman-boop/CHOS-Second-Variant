#ifndef CHOS_NET_H
#define CHOS_NET_H

/*
 * CHOS networking ABI design header (0.12.0-dev).
 *
 * This header defines fixed-width types and states for the planned user-space
 * socket service. It is a design contract only: 0.12.0-dev does not yet ship
 * network syscalls, a TCP/IP stack, an Ethernet driver, or usable sockets.
 */
#include <stdint.h>

#define CHOS_NET_ABI_VERSION 1u
#define CHOS_NET_AF_INET 2u
#define CHOS_NET_SOCKET_INVALID ((int32_t)-1)
#define CHOS_NET_MAX_SOCKETS_PER_PROCESS 16u
#define CHOS_NET_MAX_IO_BYTES (64u * 1024u)

typedef int32_t chos_socket_t;

typedef struct {
    uint8_t octet[4];
} chos_ipv4_addr_t;

typedef struct {
    uint16_t family;
    /* Port is represented in network byte order; no host-endian conversion. */
    uint16_t port_be;
    chos_ipv4_addr_t address;
    uint8_t reserved[8];
} chos_sockaddr_ipv4_t;

typedef enum {
    CHOS_NET_STATE_CLOSED = 0u,
    CHOS_NET_STATE_OPEN = 1u,
    CHOS_NET_STATE_BOUND = 2u,
    CHOS_NET_STATE_LISTENING = 3u,
    CHOS_NET_STATE_CONNECTED = 4u,
    CHOS_NET_STATE_FAILED = 5u
} chos_net_socket_state_t;

typedef struct {
    uint32_t abi_version;
    uint32_t size_bytes;
    uint32_t state;
    uint32_t flags;
} chos_net_socket_info_t;

_Static_assert(sizeof(chos_ipv4_addr_t) == 4u, "IPv4 address ABI changed");
_Static_assert(sizeof(chos_sockaddr_ipv4_t) == 16u, "IPv4 socket address ABI changed");
_Static_assert(sizeof(chos_net_socket_info_t) == 16u, "socket info ABI changed");

#endif
