#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/net_stack.h"
#include "chos/net_packet.h"
#include "chos/net_services.h"
#include "chos/net_tcp.h"
#include "chos/net_device.h"
#include "chos/net6.h"
#include "chos/timer.h"

static const uint8_t client_mac[6] = {0x52u,0x54u,0x00u,0x12u,0x34u,0x56u};
static const uint8_t server_mac[6] = {0x52u,0x55u,0x0au,0x00u,0x02u,0x02u};
static uint64_t fake_ticks;
static uint8_t last_tx[CHOS_NET_MAX_ETHERNET_FRAME];
static size_t last_tx_length;
static unsigned tx_count;
static uint8_t queued_rx[CHOS_NET_MAX_ETHERNET_FRAME];
static size_t queued_rx_length;
static int rx_pending;
/* This fixture isolates the retained IPv4 implementation; net6 has a real-core suite. */
void chos_net6_init(const uint8_t mac[6]){(void)mac;}
void chos_net6_poll(int enabled,int link){(void)enabled;(void)link;}
void chos_net6_receive(const void *p,size_t n){(void)p;(void)n;}

static uint16_t read_be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

int timer_is_ready(void) { return 1; }
uint64_t timer_ticks(void) { return fake_ticks; }
uint64_t timer_frequency_hz(void) { return CHOS_TIMER_HZ; }
uint64_t timer_counter(void) { return fake_ticks; }
int timer_init(void) { return 1; }
void timer_stop(void) { }
void timer_handle_irq(void) { }

static int backend_ready=1,backend_link=1;
int chos_net_device_ready(void) { return backend_ready; }
const char *chos_net_device_error(void) { return "test backend"; }
int chos_net_device_init(void) { return 1; }
void chos_net_device_get_info(chos_net_device_info_t *out) {
    size_t i;
    if (out == NULL) return;
    for (i = 0u; i < 6u; ++i) out->mac[i] = client_mac[i];
    out->ready = backend_ready; out->link_status_known = 1; out->link_up = backend_link;
    out->device_status = 0u; out->rx_frames = 0u; out->tx_frames = tx_count;
    out->rx_dropped = 0u; out->tx_errors = 0u; out->mmio_base = 0u;
}
int chos_net_device_send(const void *frame, size_t length) {
    if (frame == NULL || length > sizeof(last_tx)) return 0;
    memcpy(last_tx, frame, length); last_tx_length = length; ++tx_count; return 1;
}
int chos_net_device_receive(void *frame, size_t capacity, size_t *length_out) {
    if (length_out != NULL) *length_out = 0u;
    if (!rx_pending) return 0;
    if (frame == NULL || length_out == NULL || capacity < queued_rx_length) {
        rx_pending = 0; return -1;
    }
    memcpy(frame, queued_rx, queued_rx_length); *length_out = queued_rx_length; rx_pending = 0;
    return 1;
}

static size_t build_dhcp_reply(uint8_t *frame, size_t capacity, uint32_t xid,
                               uint8_t message_type, uint32_t yiaddr,
                               int bad_client_mac, int include_lease) {
    uint8_t payload[300];
    static const uint8_t broadcast_mac[6] = {0xffu,0xffu,0xffu,0xffu,0xffu,0xffu};
    const uint32_t server_ip = 0xc0a80101u;
    size_t p = 240u, i, payload_length, frame_length;
    memset(payload, 0, sizeof(payload));
    payload[0] = 2u; payload[1] = 1u; payload[2] = 6u;
    put32(&payload[4], xid); put32(&payload[16], yiaddr);
    for (i = 0u; i < 6u; ++i) payload[28u + i] = client_mac[i];
    if (bad_client_mac) payload[33] ^= 0x01u;
    put32(&payload[236], 0x63825363u);
    payload[p++] = 53u; payload[p++] = 1u; payload[p++] = message_type;
    payload[p++] = 54u; payload[p++] = 4u; put32(&payload[p], server_ip); p += 4u;
    if (message_type == 2u || message_type == 5u) {
        payload[p++] = 1u; payload[p++] = 4u; put32(&payload[p], 0xffffff00u); p += 4u;
        payload[p++] = 3u; payload[p++] = 4u; put32(&payload[p], server_ip); p += 4u;
        payload[p++] = 6u; payload[p++] = 4u; put32(&payload[p], 0x08080808u); p += 4u;
        if (include_lease) {
            payload[p++] = 51u; payload[p++] = 4u; put32(&payload[p], 3600u); p += 4u;
        }
    }
    payload[p++] = 255u;
    payload_length = p;
    frame_length = chos_net_build_udp_ipv4_frame(frame, capacity, server_mac, broadcast_mac,
        server_ip, 0xffffffffu, 67u, 68u, 0x1212u, 64u, payload, payload_length);
    return frame_length;
}

static void queue_dhcp_reply(uint32_t xid, uint8_t type, uint32_t yiaddr,
                             int bad_mac, int include_lease) {
    queued_rx_length = build_dhcp_reply(queued_rx, sizeof(queued_rx), xid, type,
                                        yiaddr, bad_mac, include_lease);
    assert(queued_rx_length > 0u);
    rx_pending = 1;
}

static void queue_arp_reply(void) {
    size_t i;
    memset(queued_rx, 0, sizeof(queued_rx));
    for (i = 0u; i < 6u; ++i) {
        queued_rx[i] = client_mac[i];
        queued_rx[6u + i] = server_mac[i];
        queued_rx[22u + i] = server_mac[i];
        queued_rx[32u + i] = client_mac[i];
    }
    put16(&queued_rx[12], 0x0806u);
    put16(&queued_rx[14], 1u); put16(&queued_rx[16], 0x0800u);
    queued_rx[18] = 6u; queued_rx[19] = 4u; put16(&queued_rx[20], 2u);
    put32(&queued_rx[28], 0xc0a80101u);
    put32(&queued_rx[38], 0xc0a80164u);
    queued_rx_length = 42u; rx_pending = 1;
}

static size_t build_dns_a_reply(uint8_t *out, size_t capacity, uint16_t query_id) {
    static const uint8_t qname[] = {7u,'e','x','a','m','p','l','e',3u,'c','o','m',0u};
    size_t p = 12u;
    if (out == NULL || capacity < 45u) return 0u;
    memset(out, 0, capacity);
    put16(&out[0], query_id); put16(&out[2], 0x8180u); put16(&out[4], 1u); put16(&out[6], 1u);
    memcpy(&out[p], qname, sizeof(qname)); p += sizeof(qname);
    put16(&out[p], 1u); put16(&out[p + 2u], 1u); p += 4u;
    out[p++] = 0xc0u; out[p++] = 0x0cu;
    put16(&out[p], 1u); put16(&out[p + 2u], 1u); put32(&out[p + 4u], 120u);
    put16(&out[p + 8u], 4u); p += 10u;
    out[p++] = 93u; out[p++] = 184u; out[p++] = 216u; out[p++] = 34u;
    return p;
}

int main(void) {
    chos_net_stack_info_t info;
    uint32_t xid;
    fake_ticks = 0u; tx_count = 0u; rx_pending = 0; last_tx_length = 0u;
    chos_net_stack_init();
    chos_net_stack_get_info(&info);
    assert(info.driver_ready && info.dhcp_state == CHOS_NET_DHCP_DISCOVERING);
    assert(info.transaction_id != 0u && tx_count == 1u && last_tx_length > 300u);
    xid = info.transaction_id;

    /* Reject a valid-XID packet associated with another client's chaddr. */
    queue_dhcp_reply(xid, 2u, 0xc0a80164u, 1, 1);
    chos_net_stack_poll();
    chos_net_stack_get_info(&info);
    assert(info.dhcp_state == CHOS_NET_DHCP_DISCOVERING);
    assert(info.rejected_dhcp_replies == 1u);

    queue_dhcp_reply(xid, 2u, 0xc0a80164u, 0, 1);
    chos_net_stack_poll();
    chos_net_stack_get_info(&info);
    assert(info.dhcp_state == CHOS_NET_DHCP_REQUESTING);
    assert(info.rx_dhcp_replies == 1u && tx_count >= 2u);

    queue_dhcp_reply(xid, 5u, 0xc0a80164u, 0, 1);
    chos_net_stack_poll();
    chos_net_stack_get_info(&info);
    assert(info.dhcp_state == CHOS_NET_DHCP_BOUND);
    assert(info.ipv4_address == 0xc0a80164u);
    assert(info.subnet_mask == 0xffffff00u);
    assert(info.gateway_address == 0xc0a80101u);
    assert(info.dns_address == 0x08080808u);
    assert(info.dhcp_server == 0xc0a80101u && info.lease_seconds == 3600u);

    /* At T1, a non-blocking rebinding/renew request is sent; a renewal ACK
     * with yiaddr=0 retains the already configured client address. */
    fake_ticks = (uint64_t)1800u * CHOS_TIMER_HZ;
    chos_net_stack_poll();
    chos_net_stack_get_info(&info);
    assert(info.dhcp_state == CHOS_NET_DHCP_RENEWING);
    queue_dhcp_reply(xid, 5u, 0u, 0, 1);
    chos_net_stack_poll();
    chos_net_stack_get_info(&info);
    assert(info.dhcp_state == CHOS_NET_DHCP_BOUND && info.ipv4_address == 0xc0a80164u);
    assert(info.rx_dhcp_replies == 3u);

    /* DNS stays asynchronous: resolve the off-subnet server's gateway by ARP,
     * transmit an A query, and accept a matching response. */
    assert(chos_net_dns_lookup_start("example.com"));

    /* The first transmission after starting the query is an ARP request for
     * the gateway. The DNS query is sent only after a matching neighbor reply. */
    assert(last_tx_length == 42u && last_tx[12] == 0x08u && last_tx[13] == 0x06u);
    queue_arp_reply();
    fake_ticks += 50u;
    chos_net_stack_poll();
    fake_ticks += 50u;
    chos_net_stack_poll();
    {
        chos_net_dns_info_t dns;
        chos_ipv4_view_t ip;
        chos_udp_view_t udp;
        uint8_t dns_reply[64];
        uint8_t dns_frame[CHOS_NET_MAX_ETHERNET_FRAME];
        size_t dns_length, dns_frame_length;
        chos_net_dns_get_info(&dns);
        assert(dns.state == CHOS_NET_DNS_PENDING && dns.query_id != 0u);
        assert(chos_net_parse_ipv4(last_tx, last_tx_length, &ip));
        assert(chos_net_parse_udp(&ip, &udp));
        assert(udp.source_port == 53000u && udp.destination_port == 53u);
        assert(read_be16(udp.payload) == (uint16_t)dns.query_id);
        dns_length = build_dns_a_reply(dns_reply, sizeof(dns_reply), (uint16_t)dns.query_id);
        dns_frame_length = chos_net_build_udp_ipv4_frame(dns_frame, sizeof(dns_frame),
            server_mac, client_mac, 0x08080808u, 0xc0a80164u, 53u, 53000u, 0x3333u, 64u,
            dns_reply, dns_length);
        assert(dns_frame_length > 0u);
        memcpy(queued_rx, dns_frame, dns_frame_length); queued_rx_length = dns_frame_length; rx_pending = 1;
        chos_net_stack_poll();
        chos_net_dns_get_info(&dns);
        assert(dns.state == CHOS_NET_DNS_RESOLVED);
        assert(dns.ipv4_address == 0x5db8d822u && dns.ttl_seconds == 120u);
    }

    /* ICMP echo: send to a learned on-link neighbor and accept only a reply
     * whose source, identifier and sequence match the outstanding request. */
    assert(chos_net_ping_start(0xc0a80101u));
    {
        chos_ipv4_view_t ip;
        chos_icmpv4_echo_view_t echo;
        chos_net_ping_info_t ping;
        size_t reply_length;
        assert(chos_net_parse_ipv4(last_tx, last_tx_length, &ip));
        assert(ip.protocol == 1u && ip.destination_address == 0xc0a80101u);
        assert(chos_net_parse_icmpv4_echo(&ip, &echo));
        assert(echo.type == 8u);
        reply_length = chos_net_build_icmpv4_frame(queued_rx, sizeof(queued_rx),
            server_mac, client_mac, 0xc0a80101u, 0xc0a80164u, 0x4455u, 64u,
            0u, echo.identifier, echo.sequence, echo.payload, echo.payload_length);
        assert(reply_length > 0u);
        queued_rx_length = reply_length; rx_pending = 1;
        fake_ticks += 7u;
        chos_net_stack_poll();
        chos_net_ping_get_info(&ping);
        assert(ping.state == CHOS_NET_PING_REPLIED && ping.elapsed_ticks == 7u);
    }

    /* A missing echo reply retries a bounded number of times and reaches a
     * terminal timeout rather than waiting forever. */
    assert(chos_net_ping_start(0xc0a80101u));
    for (unsigned retry = 0u; retry < 4u; ++retry) {
        fake_ticks += 2u * CHOS_TIMER_HZ;
        chos_net_stack_poll();
    }
    {
        chos_net_ping_info_t ping;
        chos_net_ping_get_info(&ping);
        assert(ping.state == CHOS_NET_PING_TIMED_OUT && ping.attempts == 3u);
    }

    /* TCP integration: use the learned ARP neighbor, start an outbound
     * connection, accept a matching SYN/ACK, and send application data via
     * the same RX/TX path used by DHCP, DNS, and ICMP. */
    {
        int handle = chos_tcp_open(0u);
        chos_tcp_socket_info_t tcp_info;
        chos_ipv4_view_t ip;
        chos_tcp_view_t tcp;
        uint8_t tcp_frame[CHOS_NET_MAX_ETHERNET_FRAME];
        size_t tcp_frame_length;
        uint16_t local_port;
        uint32_t initial_sequence;
        static const char payload[] = "CHOS-TCP";
        assert(handle > 0);
        assert(chos_tcp_connect(handle, 0xc0a80101u, 80u));
        chos_net_stack_poll();
        assert(chos_net_parse_ipv4(last_tx, last_tx_length, &ip) && ip.protocol == 6u);
        assert(chos_net_parse_tcp(&ip, &tcp) && (tcp.flags & CHOS_TCP_FLAG_SYN) != 0u);
        local_port = tcp.source_port;
        initial_sequence = tcp.sequence;
        tcp_frame_length = chos_net_build_tcp_ipv4_frame(tcp_frame, sizeof(tcp_frame),
            server_mac, client_mac, 0xc0a80101u, 0xc0a80164u, 80u, local_port,
            0x55667788u, initial_sequence + 1u,
            CHOS_TCP_FLAG_SYN | CHOS_TCP_FLAG_ACK, 4096u, 0x7778u, 64u, NULL, 0u);
        assert(tcp_frame_length > 0u);
        memcpy(queued_rx, tcp_frame, tcp_frame_length);
        queued_rx_length = tcp_frame_length; rx_pending = 1;
        chos_net_stack_poll();
        assert(chos_tcp_get_info(handle, &tcp_info) && tcp_info.state == CHOS_TCP_ESTABLISHED);
        assert(chos_tcp_send(handle, payload, sizeof(payload) - 1u) == (int)(sizeof(payload) - 1u));
        chos_net_stack_poll();
        assert(chos_net_parse_ipv4(last_tx, last_tx_length, &ip) && ip.protocol == 6u);
        assert(chos_net_parse_tcp(&ip, &tcp));
        assert((tcp.flags & CHOS_TCP_FLAG_PSH) != 0u && tcp.payload_length == sizeof(payload) - 1u);
        assert(memcmp(tcp.payload, payload, sizeof(payload) - 1u) == 0);
    }

    assert(!chos_net_dns_set_server(0));assert(!chos_net_dns_set_server(0xe0000001));
    assert(chos_net_dns_set_server(0x0a000202));
    backend_ready=0;chos_net_stack_poll();
    chos_net_stack_info_t offline;chos_net_stack_get_info(&offline);
    assert(!offline.driver_ready && offline.dhcp_state==CHOS_NET_DHCP_OFFLINE && !offline.ipv4_address);
    backend_ready=1;
    chos_net_config_t cfg={.mode=CHOS_NET_STATIC,.ipv4=0x0a000219,.netmask=0xffffff00,.gateway=0x0a000202,.dns=0x0a000203};
    assert(chos_net_stack_configure(&cfg));chos_net_stack_get_info(&offline);
    assert(offline.dhcp_state==CHOS_NET_DHCP_STATIC&&offline.ipv4_address==cfg.ipv4);
    assert(chos_net_dns_lookup_start("static.test"));
    backend_link=0;chos_net_stack_poll();chos_net_stack_get_info(&offline);
    assert(offline.dhcp_state==CHOS_NET_DHCP_OFFLINE&&!offline.ipv4_address);
    backend_link=1;chos_net_stack_poll();chos_net_stack_get_info(&offline);
    assert(offline.dhcp_state==CHOS_NET_DHCP_STATIC&&offline.ipv4_address==cfg.ipv4);
    cfg=(chos_net_config_t){.mode=CHOS_NET_DISABLED};assert(chos_net_stack_configure(&cfg));
    unsigned transmissions=tx_count;chos_net_stack_poll();assert(tx_count==transmissions);
    cfg=(chos_net_config_t){0};assert(chos_net_stack_configure(&cfg));
    chos_net_stack_get_info(&offline);assert(offline.dhcp_state==CHOS_NET_DHCP_DISCOVERING);
    puts("NET stack: DHCP lease/renewal, ARP, DNS, ICMP and integrated TCP handshake/send passed");
    return 0;
}
