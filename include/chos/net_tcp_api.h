#ifndef CHOS_NET_TCP_API_H
#define CHOS_NET_TCP_API_H

#include <stdint.h>

/* Stable EL0 TCP API ABI. The client is non-blocking; inspect state after each
 * operation. These constants bound kernel-owned storage and syscall copies. */
#define CHOS_TCP_MAX_SOCKETS 4u
#define CHOS_TCP_MSS 512u
#define CHOS_TCP_RX_CAPACITY 2048u
#define CHOS_TCP_API_MAX_TRANSFER 512u

typedef enum {
    CHOS_TCP_CLOSED = 0,
    CHOS_TCP_SYN_SENT,
    CHOS_TCP_ESTABLISHED,
    CHOS_TCP_FIN_WAIT_1,
    CHOS_TCP_FIN_WAIT_2,
    CHOS_TCP_CLOSE_WAIT,
    CHOS_TCP_LAST_ACK,
    CHOS_TCP_TIME_WAIT,
    CHOS_TCP_FAILED
} chos_tcp_state_t;

typedef enum {
    CHOS_TCP_ERROR_NONE = 0,
    CHOS_TCP_ERROR_TIMEOUT,
    CHOS_TCP_ERROR_RESET,
    CHOS_TCP_ERROR_NETWORK,
    CHOS_TCP_ERROR_ARGUMENT
} chos_tcp_error_t;

typedef struct {
    int32_t handle; /* process-owned handle, globally allocated slot encoded as 1..4 */
    chos_tcp_state_t state;
    chos_tcp_error_t error;
    uint32_t local_ipv4;
    uint32_t remote_ipv4;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t send_unacknowledged;
    uint32_t send_next;
    uint32_t receive_next;
    uint32_t retransmissions;
    uint32_t remote_window;
    uint64_t receive_available;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} chos_tcp_socket_info_t;

#endif
