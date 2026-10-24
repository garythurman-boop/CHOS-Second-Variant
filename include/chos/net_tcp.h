#ifndef CHOS_NET_TCP_H
#define CHOS_NET_TCP_H

#include <stddef.h>
#include <stdint.h>
#include "chos/net_packet.h"
#include "chos/net_tcp_api.h"

#define CHOS_TCP_FLAG_FIN 0x01u
#define CHOS_TCP_FLAG_SYN 0x02u
#define CHOS_TCP_FLAG_RST 0x04u
#define CHOS_TCP_FLAG_PSH 0x08u
#define CHOS_TCP_FLAG_ACK 0x10u

/* Bounded non-blocking outbound TCP client. The transport callback returns 1
 * when a frame is sent, 0 when it must be retried (e.g. ARP unresolved), and
 * -1 for a permanent send error. The callback must not retain payload memory. */
typedef int (*chos_tcp_emit_fn)(uint32_t source_ipv4, uint32_t destination_ipv4,
                                uint16_t source_port, uint16_t destination_port,
                                uint32_t sequence, uint32_t acknowledgement,
                                uint8_t flags, uint16_t window,
                                const void *payload, size_t payload_length);
typedef uint64_t (*chos_tcp_clock_fn)(void);

void chos_tcp_init(chos_tcp_emit_fn emit, chos_tcp_clock_fn clock_fn);
void chos_tcp_set_local_address(uint32_t local_ipv4);
/* Returns handle 1..4, or -1 when unavailable/conflicting. Port 0 chooses an ephemeral port. */
int chos_tcp_open(uint16_t local_port);
/* Process-owned variants are used by EL0 syscalls. Owner 0 is reserved for the serial shell. */
int chos_tcp_open_for_owner(uint32_t owner_pid, uint16_t local_port);
/* Non-blocking connect: returns 1 when started, 0 when rejected. */
int chos_tcp_connect(int handle, uint32_t remote_ipv4, uint16_t remote_port);
int chos_tcp_connect_for_owner(uint32_t owner_pid, int handle, uint32_t remote_ipv4, uint16_t remote_port);
/* Queue at most one MSS of data; returns bytes accepted, 0 if busy/not ready, or -1 for invalid handle. */
int chos_tcp_send(int handle, const void *data, size_t length);
int chos_tcp_send_for_owner(uint32_t owner_pid, int handle, const void *data, size_t length);
/* Reads available data; returns bytes read, 0 if no data, or -1 for an invalid handle. */
int chos_tcp_recv(int handle, void *buffer, size_t capacity);
int chos_tcp_recv_for_owner(uint32_t owner_pid, int handle, void *buffer, size_t capacity);
/* Starts a graceful FIN when possible; returns 1 on accepted close request, 0 if busy, -1 if invalid. */
int chos_tcp_close(int handle);
int chos_tcp_close_for_owner(uint32_t owner_pid, int handle);
/* Aborts an in-flight connection and releases the handle immediately. */
int chos_tcp_abort_for_owner(uint32_t owner_pid, int handle);
int chos_tcp_get_info(int handle, chos_tcp_socket_info_t *out);
int chos_tcp_get_info_for_owner(uint32_t owner_pid, int handle, chos_tcp_socket_info_t *out);
/* Immediately releases all sockets owned by a process when it exits. Owner 0 is shell-owned. */
void chos_tcp_drop_owner(uint32_t owner_pid);
const char *chos_tcp_state_name(chos_tcp_state_t state);
const char *chos_tcp_error_name(chos_tcp_error_t error);
/* Call from the network service poll and pass validated IPv4/TCP views. */
void chos_tcp_poll(void);
void chos_tcp_input(const chos_ipv4_view_t *ip, const chos_tcp_view_t *tcp);

#endif
