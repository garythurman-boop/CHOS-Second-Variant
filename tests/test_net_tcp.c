#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/net_tcp.h"

#define IP_LOCAL  0x0a00020fu
#define IP_REMOTE 0x5db8d822u

typedef struct {
    uint32_t src_ip, dst_ip, seq, ack;
    uint16_t src_port, dst_port, window;
    uint8_t flags;
    uint8_t payload[CHOS_TCP_MSS];
    size_t payload_length;
    unsigned sends;
} emitted_t;

static uint64_t fake_time;
static emitted_t last;

static int emit(uint32_t src_ip, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window,
                const void *payload, size_t payload_length) {
    assert(payload_length <= sizeof(last.payload));
    last.src_ip = src_ip; last.dst_ip = dst_ip;
    last.src_port = src_port; last.dst_port = dst_port;
    last.seq = seq; last.ack = ack; last.flags = flags; last.window = window;
    last.payload_length = payload_length;
    if (payload_length != 0u) memcpy(last.payload, payload, payload_length);
    ++last.sends;
    return 1;
}

static uint64_t clock_now(void) { return fake_time; }

static void deliver(uint16_t local_port, uint32_t remote_sequence, uint32_t acknowledgement,
                    uint8_t flags, uint16_t remote_window, const void *payload, size_t payload_length) {
    chos_ipv4_view_t ip;
    chos_tcp_view_t tcp;
    memset(&ip, 0, sizeof(ip));
    memset(&tcp, 0, sizeof(tcp));
    ip.protocol = 6u;
    ip.source_address = IP_REMOTE;
    ip.destination_address = IP_LOCAL;
    tcp.source_port = 80u;
    tcp.destination_port = local_port;
    tcp.sequence = remote_sequence;
    tcp.acknowledgement = acknowledgement;
    tcp.flags = flags;
    tcp.window = remote_window;
    tcp.payload = (const uint8_t *)payload;
    tcp.payload_length = payload_length;
    chos_tcp_input(&ip, &tcp);
}

static void test_connect_send_receive_close(void) {
    static const char request[] = "GET / HTTP/1.0\r\n\r\n";
    static const char response[] = "HTTP/1.0 200 OK\r\n\r\nhello";
    chos_tcp_socket_info_t info;
    uint32_t syn_sequence, data_sequence, fin_sequence;
    uint16_t local_port;
    unsigned before;
    char received[sizeof(response)];
    int handle;

    fake_time = 0u;
    memset(&last, 0, sizeof(last));
    chos_tcp_init(emit, clock_now);
    chos_tcp_set_local_address(IP_LOCAL);

    handle = chos_tcp_open(0u);
    assert(handle > 0);
    assert(chos_tcp_connect(handle, IP_REMOTE, 80u) == 1);
    assert(chos_tcp_get_info(handle, &info));
    local_port = info.local_port;
    assert(local_port >= 49152u);
    chos_tcp_poll();
    assert(last.sends == 1u && last.flags == CHOS_TCP_FLAG_SYN);
    syn_sequence = last.seq;

    /* Bounded SYN retry and retransmission accounting. */
    fake_time = 50u;
    chos_tcp_poll();
    assert(last.sends == 1u);
    fake_time = 100u;
    chos_tcp_poll();
    assert(last.sends == 2u && last.flags == CHOS_TCP_FLAG_SYN);
    assert(chos_tcp_get_info(handle, &info) && info.retransmissions == 1u);

    /* Complete the three-way handshake. */
    deliver(local_port, 7000u, syn_sequence + 1u,
            CHOS_TCP_FLAG_SYN | CHOS_TCP_FLAG_ACK, 4096u, NULL, 0u);
    assert(chos_tcp_get_info(handle, &info));
    assert(info.state == CHOS_TCP_ESTABLISHED && info.receive_next == 7001u);
    assert(last.flags == CHOS_TCP_FLAG_ACK);

    assert(chos_tcp_send(handle, request, sizeof(request) - 1u) == (int)(sizeof(request) - 1u));
    chos_tcp_poll();
    assert((last.flags & (CHOS_TCP_FLAG_ACK | CHOS_TCP_FLAG_PSH)) ==
           (CHOS_TCP_FLAG_ACK | CHOS_TCP_FLAG_PSH));
    assert(last.payload_length == sizeof(request) - 1u);
    assert(memcmp(last.payload, request, sizeof(request) - 1u) == 0);
    data_sequence = last.seq;

    deliver(local_port, 7001u, data_sequence + (uint32_t)(sizeof(request) - 1u),
            CHOS_TCP_FLAG_ACK, 4096u, NULL, 0u);
    assert(chos_tcp_get_info(handle, &info) && info.retransmissions == 0u);

    /* Receive in-order data exactly once and ACK it. */
    before = last.sends;
    deliver(local_port, 7001u, info.send_next,
            CHOS_TCP_FLAG_ACK | CHOS_TCP_FLAG_PSH, 4096u, response, sizeof(response) - 1u);
    assert(last.sends == before + 1u && last.flags == CHOS_TCP_FLAG_ACK);
    assert(chos_tcp_get_info(handle, &info));
    assert(info.receive_available == sizeof(response) - 1u);
    assert(info.bytes_received == sizeof(response) - 1u);
    assert(chos_tcp_recv(handle, received, sizeof(received)) == (int)(sizeof(response) - 1u));
    assert(memcmp(received, response, sizeof(response) - 1u) == 0);
    assert(chos_tcp_get_info(handle, &info) && info.receive_available == 0u);

    assert(chos_tcp_close(handle) == 1);
    chos_tcp_poll();
    assert(last.flags == (CHOS_TCP_FLAG_FIN | CHOS_TCP_FLAG_ACK));
    fin_sequence = last.seq;
    deliver(local_port, 7001u + (uint32_t)(sizeof(response) - 1u), fin_sequence + 1u,
            CHOS_TCP_FLAG_ACK, 4096u, NULL, 0u);
    assert(chos_tcp_get_info(handle, &info) && info.state == CHOS_TCP_FIN_WAIT_2);
    deliver(local_port, info.receive_next, info.send_next,
            CHOS_TCP_FLAG_FIN | CHOS_TCP_FLAG_ACK, 4096u, NULL, 0u);
    assert(chos_tcp_get_info(handle, &info) && info.state == CHOS_TCP_TIME_WAIT);
    fake_time += 201u;
    chos_tcp_poll();
    assert(chos_tcp_get_info(handle, &info) && info.state == CHOS_TCP_CLOSED);
}

static void test_close_wait_remains_writable(void) {
    static const char request[] = "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    chos_tcp_socket_info_t info;
    uint32_t syn_sequence, data_sequence;
    uint16_t local_port;
    int handle;

    fake_time = 0u;
    memset(&last, 0, sizeof(last));
    chos_tcp_init(emit, clock_now);
    chos_tcp_set_local_address(IP_LOCAL);
    handle = chos_tcp_open(0u);
    assert(handle > 0);
    assert(chos_tcp_connect(handle, IP_REMOTE, 80u) == 1);
    chos_tcp_poll();
    assert(last.flags == CHOS_TCP_FLAG_SYN);
    syn_sequence = last.seq;
    assert(chos_tcp_get_info(handle, &info));
    local_port = info.local_port;

    deliver(local_port, 7000u, syn_sequence + 1u,
            CHOS_TCP_FLAG_SYN | CHOS_TCP_FLAG_ACK, 4096u, NULL, 0u);
    assert(chos_tcp_get_info(handle, &info) && info.state == CHOS_TCP_ESTABLISHED);

    /* A server FIN only closes its send direction. The client can still send
     * its request while in CLOSE-WAIT, and then close its own direction. */
    deliver(local_port, 7001u, info.send_next,
            CHOS_TCP_FLAG_FIN | CHOS_TCP_FLAG_ACK, 4096u, NULL, 0u);
    assert(chos_tcp_get_info(handle, &info));
    assert(info.state == CHOS_TCP_CLOSE_WAIT);
    assert(info.receive_next == 7002u);
    assert(chos_tcp_send(handle, request, sizeof(request) - 1u) == (int)(sizeof(request) - 1u));
    chos_tcp_poll();
    assert(last.flags == (CHOS_TCP_FLAG_ACK | CHOS_TCP_FLAG_PSH));
    assert(last.payload_length == sizeof(request) - 1u);
    assert(memcmp(last.payload, request, sizeof(request) - 1u) == 0);
    data_sequence = last.seq;

    deliver(local_port, 7002u, data_sequence + (uint32_t)(sizeof(request) - 1u),
            CHOS_TCP_FLAG_ACK, 4096u, NULL, 0u);
    assert(chos_tcp_get_info(handle, &info) && info.state == CHOS_TCP_CLOSE_WAIT);
    assert(chos_tcp_close(handle) == 1);
    assert(chos_tcp_get_info(handle, &info) && info.state == CHOS_TCP_LAST_ACK);
    chos_tcp_poll();
    assert(last.flags == (CHOS_TCP_FLAG_FIN | CHOS_TCP_FLAG_ACK));
}

static void test_connect_timeout_and_address_loss(void) {
    chos_tcp_socket_info_t info;
    int handle;
    unsigned i;
    fake_time = 0u;
    memset(&last, 0, sizeof(last));
    chos_tcp_init(emit, clock_now);
    chos_tcp_set_local_address(IP_LOCAL);
    handle = chos_tcp_open(0u);
    assert(handle > 0 && chos_tcp_connect(handle, IP_REMOTE, 443u) == 1);
    chos_tcp_poll();
    for (i = 0u; i < 6u; ++i) {
        fake_time += (i == 0u ? 100u : (i == 1u ? 200u : (i == 2u ? 400u : 800u)));
        chos_tcp_poll();
    }
    assert(chos_tcp_get_info(handle, &info));
    assert(info.state == CHOS_TCP_FAILED && info.error == CHOS_TCP_ERROR_TIMEOUT);
    assert(chos_tcp_close(handle) == 1);

    handle = chos_tcp_open(0u);
    assert(handle > 0 && chos_tcp_connect(handle, IP_REMOTE, 443u) == 1);
    chos_tcp_poll();
    chos_tcp_set_local_address(0u);
    assert(chos_tcp_get_info(handle, &info));
    assert(info.state == CHOS_TCP_FAILED && info.error == CHOS_TCP_ERROR_NETWORK);
}

static void test_fin_wait_2_timeout(void) {
    chos_tcp_socket_info_t info;
    uint16_t local_port;
    uint32_t syn_sequence;
    int handle;
    fake_time=0u; memset(&last,0,sizeof(last));
    chos_tcp_init(emit,clock_now); chos_tcp_set_local_address(IP_LOCAL);
    handle=chos_tcp_open(0u); assert(handle>0);
    assert(chos_tcp_connect(handle,IP_REMOTE,80u)==1);
    assert(chos_tcp_get_info(handle,&info)); local_port=info.local_port;
    chos_tcp_poll(); syn_sequence=last.seq;
    deliver(local_port,9000u,syn_sequence+1u,CHOS_TCP_FLAG_SYN|CHOS_TCP_FLAG_ACK,4096u,NULL,0u);
    assert(chos_tcp_get_info(handle,&info) && info.state==CHOS_TCP_ESTABLISHED);
    assert(chos_tcp_close(handle)==1); chos_tcp_poll();
    assert(last.flags==(CHOS_TCP_FLAG_FIN|CHOS_TCP_FLAG_ACK));
    deliver(local_port,9001u,last.seq+1u,CHOS_TCP_FLAG_ACK,4096u,NULL,0u);
    assert(chos_tcp_get_info(handle,&info) && info.state==CHOS_TCP_FIN_WAIT_2);
    fake_time += 201u; chos_tcp_poll();
    assert(chos_tcp_get_info(handle,&info) && info.state==CHOS_TCP_CLOSED);
    assert(chos_tcp_close(handle)==1);
}

static void test_abort_releases_socket(void) {
    chos_tcp_socket_info_t info;
    int handle;
    fake_time=0u; memset(&last,0,sizeof(last));
    chos_tcp_init(emit,clock_now); chos_tcp_set_local_address(IP_LOCAL);
    handle=chos_tcp_open_for_owner(77u,0u); assert(handle>0);
    assert(chos_tcp_connect_for_owner(77u,handle,IP_REMOTE,80u)==1);
    chos_tcp_poll();
    assert(chos_tcp_abort_for_owner(77u,handle)==1);
    assert(!chos_tcp_get_info_for_owner(77u,handle,&info));
    assert(last.flags==CHOS_TCP_FLAG_RST);
    assert(chos_tcp_open_for_owner(78u,0u)==handle);
}

static void test_process_owner_isolation_and_cleanup(void) {
    chos_tcp_socket_info_t info;
    int owner_a_handle, owner_b_handle, owner_c_handle;
    fake_time = 0u;
    memset(&last, 0, sizeof(last));
    chos_tcp_init(emit, clock_now);
    chos_tcp_set_local_address(IP_LOCAL);
    owner_a_handle = chos_tcp_open_for_owner(101u, 0u);
    assert(owner_a_handle > 0);
    assert(chos_tcp_connect_for_owner(101u, owner_a_handle, IP_REMOTE, 80u) == 1);
    owner_b_handle = chos_tcp_open_for_owner(202u, 0u);
    assert(owner_b_handle > 0 && owner_b_handle != owner_a_handle);
    assert(chos_tcp_get_info_for_owner(101u, owner_a_handle, &info));
    assert(!chos_tcp_get_info_for_owner(202u, owner_a_handle, &info));
    assert(chos_tcp_connect_for_owner(202u, owner_a_handle, IP_REMOTE, 80u) == 0);
    assert(chos_tcp_send_for_owner(202u, owner_a_handle, "x", 1u) == -1);
    chos_tcp_drop_owner(101u);
    assert(!chos_tcp_get_info_for_owner(101u, owner_a_handle, &info));
    assert(chos_tcp_get_info_for_owner(202u, owner_b_handle, &info));
    owner_c_handle = chos_tcp_open_for_owner(303u, 0u);
    assert(owner_c_handle == owner_a_handle); /* the dropped process's slot was reclaimed */
    chos_tcp_drop_owner(303u);
    assert(chos_tcp_get_info_for_owner(202u, owner_b_handle, &info));
    chos_tcp_drop_owner(202u);
    assert(!chos_tcp_get_info_for_owner(202u, owner_b_handle, &info));
}

int main(void) {
    test_connect_send_receive_close();
    test_close_wait_remains_writable();
    test_connect_timeout_and_address_loss();
    test_fin_wait_2_timeout();
    test_abort_releases_socket();
    test_process_owner_isolation_and_cleanup();
    puts("TCP client: handshake, bounded retransmission, send/receive, CLOSE-WAIT writes, FIN-WAIT-2 timeout, abort and process ownership passed");
    return 0;
}
