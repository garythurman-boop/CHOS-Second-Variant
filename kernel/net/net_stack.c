#include <stddef.h>
#include <stdint.h>
#include "chos/net_stack.h"
#include "chos/net_packet.h"
#include "chos/net_services.h"
#include "chos/net_tcp.h"
#include "chos/net_device.h"
#include "chos/timer.h"

#define DHCP_CLIENT_PORT 68u
#define DHCP_SERVER_PORT 67u
#define DHCP_RETRY_TICKS (4u * CHOS_TIMER_HZ)
#define DHCP_MAX_BACKOFF_TICKS (40u * CHOS_TIMER_HZ)
#define DHCP_MAX_RX_PER_POLL 8u
#define DHCP_FALLBACK_POLL_TICKS 1000u
#define ARP_CACHE_ENTRIES 8u
#define ARP_CACHE_TTL_TICKS (300u * CHOS_TIMER_HZ)
#define ARP_RETRY_TICKS CHOS_TIMER_HZ
#define ARP_MAX_RETRIES 3u
#define DNS_CLIENT_PORT 53000u
#define DNS_SERVER_PORT 53u
#define DNS_TIMEOUT_TICKS (5u * CHOS_TIMER_HZ)
#define DNS_MAX_RETRIES 2u
#define DNS_MAX_ARP_RETRIES 3u
#define PING_TIMEOUT_TICKS (2u * CHOS_TIMER_HZ)
#define PING_MAX_ATTEMPTS 3u
#define PING_ARP_MAX_ATTEMPTS 3u

typedef struct {
    uint32_t address;
    uint8_t mac[6];
    uint64_t updated_at;
    int valid;
} arp_entry_t;

static chos_net_stack_info_t net_state;
static chos_net_dns_info_t dns_state;
static chos_net_ping_info_t ping_state;
static arp_entry_t arp_cache[ARP_CACHE_ENTRIES];
static uint8_t rx_frame[CHOS_NET_MAX_ETHERNET_FRAME];
static uint8_t tx_frame[CHOS_NET_MAX_ETHERNET_FRAME];
static uint8_t dhcp_payload[320u];
static uint8_t dns_payload[512u];
static uint16_t ip_identification = 1u;
static uint64_t fallback_ticks;
static uint64_t retry_deadline;
static uint64_t retry_interval = DHCP_RETRY_TICKS;
static uint64_t renewal_deadline;
static uint64_t lease_deadline;
static uint64_t arp_request_deadline;
static uint32_t arp_requested_address;
static uint32_t offered_ipv4;
static uint32_t offered_server;
static uint32_t selected_transaction;
static uint32_t dns_query_id;
static uint32_t dns_server_address;
static uint32_t dns_override;
static uint32_t dns_next_hop;
static uint64_t dns_deadline;
static uint32_t dns_query_attempts;
static uint32_t dns_arp_attempts;
static int dns_packet_sent;
static uint64_t ping_started_at;
static uint64_t ping_deadline;
static uint32_t ping_arp_attempts;
static int ping_packet_sent;
static uint16_t ping_next_id = 0x4348u;
static int initialized;

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value;
}

static void write_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

static uint64_t net_now(void) {
    if (timer_is_ready()) return timer_ticks();
    return fallback_ticks;
}

static void copy_bytes(uint8_t *out, const uint8_t *in, size_t length) {
    size_t i;
    for (i = 0u; i < length; ++i) out[i] = in[i];
}

static void clear_arp_cache(void) {
    size_t i, j;
    for (i = 0u; i < ARP_CACHE_ENTRIES; ++i) {
        arp_cache[i].address = 0u;
        arp_cache[i].updated_at = 0u;
        arp_cache[i].valid = 0;
        for (j = 0u; j < 6u; ++j) arp_cache[i].mac[j] = 0u;
    }
    arp_requested_address = 0u;
    arp_request_deadline = 0u;
}

static void clear_configuration(void) {
    net_state.ipv4_address = 0u;
    net_state.subnet_mask = 0u;
    net_state.gateway_address = 0u;
    net_state.dns_address = 0u;
    net_state.dhcp_server = 0u;
    net_state.lease_seconds = 0u;
    renewal_deadline = 0u;
    lease_deadline = 0u;
    clear_arp_cache();
    chos_tcp_set_local_address(0u);
}

static uint32_t make_xid(void) {
    uint32_t xid = (uint32_t)net_now() ^ 0x43484f53u;
    size_t i;
    for (i = 0u; i < sizeof(net_state.mac); ++i)
        xid = (xid << 5) ^ (xid >> 2) ^ (uint32_t)net_state.mac[i];
    if (xid == 0u) xid = 0x43484f53u;
    return xid;
}

static int send_dhcp_payload(const uint8_t *payload, size_t payload_length,
                             uint32_t source_ip) {
    static const uint8_t broadcast_mac[6] = {0xffu,0xffu,0xffu,0xffu,0xffu,0xffu};
    size_t frame_length;
    if (payload == NULL || payload_length == 0u) return 0;
    frame_length = chos_net_build_udp_ipv4_frame(tx_frame, sizeof(tx_frame),
        net_state.mac, broadcast_mac, source_ip, 0xffffffffu,
        DHCP_CLIENT_PORT, DHCP_SERVER_PORT, ip_identification++, 64u,
        payload, payload_length);
    if (frame_length == 0u) return 0;
    return chos_net_device_send(tx_frame, frame_length) == 1;
}

static uint64_t retry_backoff(void) {
    uint64_t interval = retry_interval;
    if (interval < DHCP_RETRY_TICKS) interval = DHCP_RETRY_TICKS;
    if (interval > DHCP_MAX_BACKOFF_TICKS) interval = DHCP_MAX_BACKOFF_TICKS;
    return interval;
}

static void schedule_retry(void) {
    retry_deadline = net_now() + retry_backoff();
    if (retry_interval < DHCP_MAX_BACKOFF_TICKS) {
        retry_interval *= 2u;
        if (retry_interval > DHCP_MAX_BACKOFF_TICKS) retry_interval = DHCP_MAX_BACKOFF_TICKS;
    }
}

static int send_discover(void) {
    size_t payload_length;
    net_state.dhcp_state = CHOS_NET_DHCP_DISCOVERING;
    selected_transaction = make_xid();
    net_state.transaction_id = selected_transaction;
    payload_length = chos_net_build_dhcpv4_discover(dhcp_payload, sizeof(dhcp_payload),
                                                    selected_transaction, net_state.mac);
    if (payload_length == 0u || !send_dhcp_payload(dhcp_payload, payload_length, 0u)) {
        net_state.dhcp_state = CHOS_NET_DHCP_FAILED;
        schedule_retry();
        return 0;
    }
    ++net_state.retries;
    schedule_retry();
    return 1;
}

static int send_selecting_request(void) {
    size_t payload_length;
    payload_length = chos_net_build_dhcpv4_request(dhcp_payload, sizeof(dhcp_payload),
        selected_transaction, net_state.mac, offered_ipv4, offered_server);
    if (payload_length == 0u || !send_dhcp_payload(dhcp_payload, payload_length, 0u)) {
        net_state.dhcp_state = CHOS_NET_DHCP_FAILED;
        schedule_retry();
        return 0;
    }
    net_state.dhcp_state = CHOS_NET_DHCP_REQUESTING;
    ++net_state.retries;
    schedule_retry();
    return 1;
}

/* DHCPREQUEST in the current renewal path: ciaddr is set, with no
 * requested-IP or server-id option. It is broadcast, avoiding a dependency
 * on route/neighbor resolution for renewing an existing lease. Renewal is
 * bounded by renewal_deadline and lease_deadline below. */
static size_t build_rebind_request(uint8_t *out, size_t capacity) {
    static const uint8_t requested_options[] = {1u,3u,6u,51u};
    size_t i, pos = 240u;
    if (out == NULL || capacity < 300u || net_state.ipv4_address == 0u) return 0u;
    for (i = 0u; i < 300u; ++i) out[i] = 0u;
    out[0] = 1u; out[1] = 1u; out[2] = 6u;
    write_be32(&out[4], selected_transaction);
    write_be16(&out[10], 0x8000u);
    write_be32(&out[12], net_state.ipv4_address);
    copy_bytes(&out[28], net_state.mac, 6u);
    write_be32(&out[236], 0x63825363u);
    out[pos++] = 53u; out[pos++] = 1u; out[pos++] = 3u;
    out[pos++] = 61u; out[pos++] = 7u; out[pos++] = 1u;
    copy_bytes(&out[pos], net_state.mac, 6u); pos += 6u;
    out[pos++] = 55u; out[pos++] = (uint8_t)sizeof(requested_options);
    for (i = 0u; i < sizeof(requested_options); ++i) out[pos++] = requested_options[i];
    /* DHCP/BOOTP messages are padded to at least 300 bytes for server compatibility. */
    while (pos < 299u) out[pos++] = 0u;
    out[pos++] = 255u;
    return pos;
}

static int send_rebind_request(void) {
    size_t payload_length = build_rebind_request(dhcp_payload, sizeof(dhcp_payload));
    if (payload_length == 0u || !send_dhcp_payload(dhcp_payload, payload_length, net_state.ipv4_address))
        return 0;
    net_state.dhcp_state = CHOS_NET_DHCP_RENEWING;
    ++net_state.retries;
    schedule_retry();
    return 1;
}

static int reply_matches_client(const uint8_t *payload, size_t length) {
    size_t i;
    if (payload == NULL || length < 34u) return 0;
    for (i = 0u; i < 6u; ++i) if (payload[28u + i] != net_state.mac[i]) return 0;
    return 1;
}

static void accept_dhcp_reply(const chos_dhcpv4_message_t *reply) {
    uint64_t now = net_now();
    uint64_t lease_ticks;
    uint32_t lease_seconds = reply->has_lease ? reply->lease_seconds : 3600u;
    uint32_t old_address = net_state.ipv4_address;
    if (lease_seconds < 60u) lease_seconds = 60u;
    if (lease_seconds > 0x00ffffffu) lease_seconds = 0x00ffffffu;
    if (reply->offered_address != 0u) net_state.ipv4_address = reply->offered_address;
    if (net_state.ipv4_address == 0u) {
        clear_configuration();
        retry_interval = DHCP_RETRY_TICKS;
        (void)send_discover();
        return;
    }
    if (reply->has_subnet_mask) net_state.subnet_mask = reply->subnet_mask;
    else if (net_state.subnet_mask == 0u) net_state.subnet_mask = 0xffffff00u;
    if (reply->has_router) net_state.gateway_address = reply->router_address;
    if (reply->has_dns) net_state.dns_address = reply->dns_address;
    if(dns_override)net_state.dns_address=dns_override;
    if (reply->has_server_address) net_state.dhcp_server = reply->server_address;
    if (net_state.dhcp_server == 0u) net_state.dhcp_server = offered_server;
    net_state.lease_seconds = lease_seconds;
    lease_ticks = (uint64_t)lease_seconds * (uint64_t)CHOS_TIMER_HZ;
    renewal_deadline = now + lease_ticks / 2u;
    lease_deadline = now + lease_ticks;
    retry_deadline = renewal_deadline;
    retry_interval = DHCP_RETRY_TICKS;
    offered_ipv4 = net_state.ipv4_address;
    offered_server = net_state.dhcp_server;
    if (old_address != net_state.ipv4_address) clear_arp_cache();
    net_state.dhcp_state = CHOS_NET_DHCP_BOUND;
    chos_tcp_set_local_address(net_state.ipv4_address);
}

static void process_dhcp_payload(const chos_udp_view_t *udp, uint32_t source_ip) {
    chos_dhcpv4_message_t reply;
    if (udp->source_port != DHCP_SERVER_PORT || udp->destination_port != DHCP_CLIENT_PORT) return;
    if (!reply_matches_client(udp->payload, udp->payload_length) ||
        !chos_net_parse_dhcpv4_reply(udp->payload, udp->payload_length, selected_transaction, &reply)) {
        ++net_state.rejected_dhcp_replies;
        return;
    }
    if (reply.has_server_address && source_ip != 0u && reply.server_address != source_ip) {
        ++net_state.rejected_dhcp_replies;
        return;
    }
    ++net_state.rx_dhcp_replies;
    switch (reply.message_type) {
        case 2u: /* OFFER */
            if (net_state.dhcp_state != CHOS_NET_DHCP_DISCOVERING || reply.offered_address == 0u ||
                !reply.has_server_address || reply.server_address == 0u) return;
            offered_ipv4 = reply.offered_address;
            offered_server = reply.server_address;
            retry_interval = DHCP_RETRY_TICKS;
            (void)send_selecting_request();
            break;
        case 5u: /* ACK; yiaddr may be zero in a renewal/rebinding ACK. */
            if (net_state.dhcp_state != CHOS_NET_DHCP_REQUESTING &&
                net_state.dhcp_state != CHOS_NET_DHCP_RENEWING) return;
            if (net_state.dhcp_state == CHOS_NET_DHCP_REQUESTING &&
                reply.offered_address != offered_ipv4) return;
            if (reply.has_server_address && offered_server != 0u &&
                reply.server_address != offered_server) return;
            accept_dhcp_reply(&reply);
            break;
        case 6u: /* NAK */
            if (net_state.dhcp_state == CHOS_NET_DHCP_REQUESTING ||
                net_state.dhcp_state == CHOS_NET_DHCP_RENEWING) {
                clear_configuration();
                retry_interval = DHCP_RETRY_TICKS;
                (void)send_discover();
            }
            break;
        default:
            break;
    }
}

static int mac_is_zero(const uint8_t mac[6]) {
    size_t i;
    for (i = 0u; i < 6u; ++i) if (mac[i] != 0u) return 0;
    return 1;
}

static int mac_is_unicast(const uint8_t mac[6]) {
    return mac != NULL && (mac[0] & 1u) == 0u && !mac_is_zero(mac);
}

static int arp_lookup(uint32_t address, uint8_t mac_out[6]) {
    uint64_t now = net_now();
    size_t i;
    if (address == 0u || mac_out == NULL) return 0;
    if (address == net_state.ipv4_address) {
        copy_bytes(mac_out, net_state.mac, 6u);
        return 1;
    }
    for (i = 0u; i < ARP_CACHE_ENTRIES; ++i) {
        if (arp_cache[i].valid && now - arp_cache[i].updated_at <= ARP_CACHE_TTL_TICKS) {
            if (arp_cache[i].address == address) {
                copy_bytes(mac_out, arp_cache[i].mac, 6u);
                return 1;
            }
        } else {
            arp_cache[i].valid = 0;
        }
    }
    return 0;
}

static void arp_cache_insert(uint32_t address, const uint8_t mac[6]) {
    size_t i, chosen = 0u;
    uint64_t oldest = UINT64_MAX;
    uint64_t now = net_now();
    if (address == 0u || address == net_state.ipv4_address || !mac_is_unicast(mac)) return;
    for (i = 0u; i < ARP_CACHE_ENTRIES; ++i) {
        if (arp_cache[i].valid && arp_cache[i].address == address) { chosen = i; goto found; }
        if (!arp_cache[i].valid) { chosen = i; goto found; }
        if (arp_cache[i].updated_at < oldest) { oldest = arp_cache[i].updated_at; chosen = i; }
    }
found:
    arp_cache[chosen].address = address;
    arp_cache[chosen].updated_at = now;
    arp_cache[chosen].valid = 1;
    copy_bytes(arp_cache[chosen].mac, mac, 6u);
}

static int send_arp_request(uint32_t target_ip) {
    size_t frame_length;
    uint64_t now = net_now();
    if (net_state.ipv4_address == 0u || target_ip == 0u) return 0;
    if (arp_requested_address == target_ip && now < arp_request_deadline) return 1;
    frame_length = chos_net_build_arp_ipv4_request(tx_frame, sizeof(tx_frame), net_state.mac,
                                                   net_state.ipv4_address, target_ip);
    if (frame_length == 0u || chos_net_device_send(tx_frame, frame_length) != 1) return 0;
    arp_requested_address = target_ip;
    arp_request_deadline = now + ARP_RETRY_TICKS;
    ++net_state.arp_requests_sent;
    return 1;
}

static void send_arp_reply(const uint8_t *request) {
    static const size_t ethernet_bytes = 14u;
    size_t i;
    uint8_t *arp;
    for (i = 0u; i < 6u; ++i) { tx_frame[i] = request[22u + i]; tx_frame[6u + i] = net_state.mac[i]; }
    write_be16(&tx_frame[12], 0x0806u);
    arp = &tx_frame[ethernet_bytes];
    write_be16(&arp[0], 1u); write_be16(&arp[2], 0x0800u); arp[4] = 6u; arp[5] = 4u;
    write_be16(&arp[6], 2u);
    copy_bytes(&arp[8], net_state.mac, 6u);
    write_be32(&arp[14], net_state.ipv4_address);
    copy_bytes(&arp[18], &request[22], 6u);
    copy_bytes(&arp[24], &request[28], 4u);
    (void)chos_net_device_send(tx_frame, ethernet_bytes + 28u);
}

static void process_arp_frame(const uint8_t *frame, size_t length) {
    uint16_t operation;
    const uint8_t *arp;
    uint32_t sender_ip, target_ip;
    if (!chos_net_validate_arp_ipv4(frame, length, &operation)) return;
    arp = &frame[14];
    sender_ip = read_be32(&arp[14]);
    target_ip = read_be32(&arp[24]);
    if (mac_is_unicast(&arp[8]) && sender_ip != 0u) {
        arp_cache_insert(sender_ip, &arp[8]);
        if (arp_requested_address == sender_ip) arp_requested_address = 0u;
        if (operation == 2u) ++net_state.arp_replies_received;
    }
    if (operation == 1u && target_ip == net_state.ipv4_address && net_state.ipv4_address != 0u)
        send_arp_reply(frame);
}

static uint32_t route_next_hop(uint32_t destination) {
    if (net_state.ipv4_address == 0u || destination == 0u) return 0u;
    if (net_state.subnet_mask != 0u &&
        (destination & net_state.subnet_mask) == (net_state.ipv4_address & net_state.subnet_mask))
        return destination;
    return net_state.gateway_address;
}

/* The TCP core is transport-only; this callback binds it to IPv4, ARP, and
 * the polled VirtIO-net device. Return 0 while neighbor resolution is pending. */
static int tcp_emit_callback(uint32_t source_ipv4, uint32_t destination_ipv4,
                             uint16_t source_port, uint16_t destination_port,
                             uint32_t sequence, uint32_t acknowledgement,
                             uint8_t flags, uint16_t window,
                             const void *payload, size_t payload_length) {
    uint8_t destination_mac[6];
    uint32_t next_hop;
    size_t frame_length;
    if (source_ipv4 == 0u || source_ipv4 != net_state.ipv4_address ||
        destination_ipv4 == 0u || destination_port == 0u || source_port == 0u)
        return -1;
    next_hop = route_next_hop(destination_ipv4);
    if (next_hop == 0u) return -1;
    if (!arp_lookup(next_hop, destination_mac)) {
        (void)send_arp_request(next_hop);
        return 0;
    }
    frame_length = chos_net_build_tcp_ipv4_frame(tx_frame, sizeof(tx_frame),
        net_state.mac, destination_mac, source_ipv4, destination_ipv4,
        source_port, destination_port, sequence, acknowledgement, flags, window,
        ip_identification++, 64u, payload, payload_length);
    if (frame_length == 0u) return -1;
    return chos_net_device_send(tx_frame, frame_length) == 1 ? 1 : 0;
}

static int dns_send_attempt(void) {
    uint8_t destination_mac[6];
    size_t query_length, frame_length;
    if (net_state.dns_address == 0u || net_state.ipv4_address == 0u) return 0;
    dns_next_hop = route_next_hop(net_state.dns_address);
    if (dns_next_hop == 0u) return 0;
    if (!arp_lookup(dns_next_hop, destination_mac)) {
        if (dns_arp_attempts < DNS_MAX_ARP_RETRIES) {
            if (send_arp_request(dns_next_hop)) ++dns_arp_attempts;
            dns_deadline = net_now() + ARP_RETRY_TICKS;
        } else {
            dns_state.state = CHOS_NET_DNS_FAILED;
        }
        return 0;
    }
    query_length = chos_net_build_dns_a_query(dns_payload, sizeof(dns_payload),
        (uint16_t)dns_query_id, dns_state.hostname, 1);
    if (query_length == 0u) { dns_state.state = CHOS_NET_DNS_FAILED; return 0; }
    frame_length = chos_net_build_udp_ipv4_frame(tx_frame, sizeof(tx_frame),
        net_state.mac, destination_mac, net_state.ipv4_address, net_state.dns_address,
        DNS_CLIENT_PORT, DNS_SERVER_PORT, ip_identification++, 64u, dns_payload, query_length);
    if (frame_length == 0u || chos_net_device_send(tx_frame, frame_length) != 1) {
        dns_deadline = net_now() + ARP_RETRY_TICKS;
        return 0;
    }
    dns_packet_sent = 1;
    ++dns_query_attempts;
    dns_state.retries = dns_query_attempts > 0u ? dns_query_attempts - 1u : 0u;
    dns_deadline = net_now() + DNS_TIMEOUT_TICKS;
    return 1;
}

static void process_dns_payload(const chos_ipv4_view_t *ip, const chos_udp_view_t *udp) {
    uint32_t address, ttl;
    if (dns_state.state != CHOS_NET_DNS_PENDING || !dns_packet_sent ||
        udp->source_port != DNS_SERVER_PORT || udp->destination_port != DNS_CLIENT_PORT ||
        ip->source_address != dns_server_address) return;
    if (!chos_net_parse_dns_a_response(udp->payload, udp->payload_length,
            (uint16_t)dns_query_id, dns_state.hostname, &address, &ttl)) return;
    dns_state.ipv4_address = address;
    dns_state.ttl_seconds = ttl;
    dns_state.state = CHOS_NET_DNS_RESOLVED;
    dns_packet_sent = 0;
}

static int send_ping_attempt(void) {
    uint8_t destination_mac[6];
    static const uint8_t payload[] = {'C','H','O','S',' ','p','i','n','g'};
    uint32_t next_hop;
    size_t frame_length;
    if (ping_state.state != CHOS_NET_PING_PENDING || net_state.ipv4_address == 0u) return 0;
    next_hop = route_next_hop(ping_state.destination_ipv4);
    if (next_hop == 0u) { ping_state.state = CHOS_NET_PING_FAILED; return 0; }
    if (!arp_lookup(next_hop, destination_mac)) {
        if (ping_arp_attempts >= PING_ARP_MAX_ATTEMPTS) {
            ping_state.state = CHOS_NET_PING_FAILED;
            return 0;
        }
        if (send_arp_request(next_hop)) ++ping_arp_attempts;
        ping_deadline = net_now() + ARP_RETRY_TICKS;
        return 0;
    }
    ping_started_at = net_now();
    frame_length = chos_net_build_icmpv4_frame(tx_frame, sizeof(tx_frame), net_state.mac,
        destination_mac, net_state.ipv4_address, ping_state.destination_ipv4,
        ip_identification++, 64u, 8u, ping_state.identifier, ping_state.sequence,
        payload, sizeof(payload));
    if (frame_length == 0u || chos_net_device_send(tx_frame, frame_length) != 1) {
        ping_deadline = net_now() + ARP_RETRY_TICKS;
        return 0;
    }
    ping_packet_sent = 1;
    ++ping_state.attempts;
    ping_deadline = net_now() + PING_TIMEOUT_TICKS;
    return 1;
}

static void process_icmp_payload(const chos_ipv4_view_t *ip) {
    chos_icmpv4_echo_view_t echo;
    size_t frame_length;
    if (!chos_net_parse_icmpv4_echo(ip, &echo)) return;
    if (echo.type == 8u && ip->destination_address == net_state.ipv4_address &&
        net_state.ipv4_address != 0u) {
        uint8_t source_mac[6];
        /* A sender must have been learned from its Ethernet header. */
        if (ip->frame == NULL || ip->frame_length < 14u) return;
        copy_bytes(source_mac, &ip->frame[6], 6u);
        if (!mac_is_unicast(source_mac)) return;
        frame_length = chos_net_build_icmpv4_frame(tx_frame, sizeof(tx_frame), net_state.mac,
            source_mac, net_state.ipv4_address, ip->source_address,
            ip_identification++, 64u, 0u, echo.identifier, echo.sequence,
            echo.payload, echo.payload_length);
        if (frame_length != 0u) (void)chos_net_device_send(tx_frame, frame_length);
        return;
    }
    if (echo.type == 0u && ping_state.state == CHOS_NET_PING_PENDING && ping_packet_sent &&
        ip->source_address == ping_state.destination_ipv4 &&
        echo.identifier == ping_state.identifier && echo.sequence == ping_state.sequence) {
        ping_state.state = CHOS_NET_PING_REPLIED;
        ping_state.elapsed_ticks = net_now() - ping_started_at;
        ping_packet_sent = 0;
    }
}

static void process_ipv4_frame(const uint8_t *frame, size_t length) {
    chos_ipv4_view_t ip;
    chos_udp_view_t udp;
    chos_tcp_view_t tcp;
    if (!chos_net_parse_ipv4(frame, length, &ip)) return;
    if (ip.protocol == 1u) {
        process_icmp_payload(&ip);
        return;
    }
    if (ip.protocol == 6u) {
        if (chos_net_parse_tcp(&ip, &tcp)) chos_tcp_input(&ip, &tcp);
        return;
    }
    if (ip.protocol != 17u || !chos_net_parse_udp(&ip, &udp)) return;
    if (udp.source_port == DHCP_SERVER_PORT && udp.destination_port == DHCP_CLIENT_PORT)
        process_dhcp_payload(&udp, ip.source_address);
    else
        process_dns_payload(&ip, &udp);
}

void chos_net_stack_init(void) {
    chos_net_device_info_t driver;
    size_t i;
    net_state.dhcp_state = CHOS_NET_DHCP_OFFLINE;
    clear_configuration();
    chos_tcp_init(tcp_emit_callback, net_now);
    net_state.retries = 0u;
    net_state.rx_dhcp_replies = 0u;
    net_state.rejected_dhcp_replies = 0u;
    net_state.arp_requests_sent = 0u;
    net_state.arp_replies_received = 0u;
    net_state.transaction_id = 0u;
    net_state.driver_ready = 0;
    for (i = 0u; i < sizeof(net_state.mac); ++i) net_state.mac[i] = 0u;
    dns_state.state = CHOS_NET_DNS_IDLE;
    dns_state.hostname[0] = '\0';
    ping_state.state = CHOS_NET_PING_IDLE;
    ping_state.destination_ipv4 = 0u; ping_state.identifier = 0u; ping_state.sequence = 0u;
    ping_state.attempts = 0u; ping_state.elapsed_ticks = 0u;
    ping_started_at = 0u; ping_deadline = 0u; ping_arp_attempts = 0u; ping_packet_sent = 0;
    dns_state.ipv4_address = 0u; dns_state.ttl_seconds = 0u; dns_state.query_id = 0u; dns_state.retries = 0u;
    initialized = 1;dns_override=0;
    fallback_ticks = 0u;
    retry_interval = DHCP_RETRY_TICKS;
    offered_ipv4 = 0u; offered_server = 0u; selected_transaction = 0u;
    retry_deadline = 0u; dns_query_id = 0u; dns_server_address = 0u;
    dns_next_hop = 0u; dns_deadline = 0u; dns_query_attempts = 0u; dns_arp_attempts = 0u; dns_packet_sent = 0;
    if (!chos_net_device_ready()) return;
    chos_net_device_get_info(&driver);
    if (!driver.ready) return;
    for (i = 0u; i < sizeof(net_state.mac); ++i) net_state.mac[i] = driver.mac[i];
    net_state.driver_ready = 1;
    (void)send_discover();
}

void chos_net_stack_poll(void) {
    size_t frame_length;
    unsigned received;
    uint64_t now;
    if (!initialized) return;
    if (!timer_is_ready()) ++fallback_ticks;
    if (!net_state.driver_ready) return;
    if(!chos_net_device_ready()){
        net_state.driver_ready=0;net_state.dhcp_state=CHOS_NET_DHCP_OFFLINE;
        clear_configuration();dns_state.state=CHOS_NET_DNS_FAILED;return;
    }
    for (received = 0u; received < DHCP_MAX_RX_PER_POLL; ++received) {
        uint16_t ether_type;
        int result = chos_net_device_receive(rx_frame, sizeof(rx_frame), &frame_length);
        if (result <= 0) break;
        if (!chos_net_parse_ethernet(rx_frame, frame_length, &ether_type)) continue;
        if (ether_type == 0x0806u) process_arp_frame(rx_frame, frame_length);
        else if (ether_type == 0x0800u) process_ipv4_frame(rx_frame, frame_length);
    }
    chos_tcp_poll();
    now = net_now();
    if (net_state.dhcp_state == CHOS_NET_DHCP_BOUND) {
        if (now >= lease_deadline) {
            clear_configuration(); retry_interval = DHCP_RETRY_TICKS; (void)send_discover();
            return;
        }
        if (now >= renewal_deadline) { (void)send_rebind_request(); return; }
    } else if (net_state.dhcp_state == CHOS_NET_DHCP_RENEWING) {
        if (now >= lease_deadline) {
            clear_configuration(); retry_interval = DHCP_RETRY_TICKS; (void)send_discover(); return;
        }
        if (now >= retry_deadline) (void)send_rebind_request();
        if (dns_state.state == CHOS_NET_DNS_PENDING && !dns_packet_sent && now >= dns_deadline) (void)dns_send_attempt();
        return;
    } else if ((net_state.dhcp_state == CHOS_NET_DHCP_DISCOVERING ||
                net_state.dhcp_state == CHOS_NET_DHCP_REQUESTING ||
                net_state.dhcp_state == CHOS_NET_DHCP_FAILED) && now >= retry_deadline) {
        if (net_state.dhcp_state == CHOS_NET_DHCP_REQUESTING && offered_ipv4 != 0u && offered_server != 0u)
            (void)send_selecting_request();
        else (void)send_discover();
    }
    if (dns_state.state == CHOS_NET_DNS_PENDING) {
        if (dns_packet_sent && now >= dns_deadline) {
            if (dns_query_attempts > DNS_MAX_RETRIES) {
                dns_state.state = CHOS_NET_DNS_FAILED;
                dns_packet_sent = 0;
            } else {
                dns_packet_sent = 0;
                (void)dns_send_attempt();
            }
        } else if (!dns_packet_sent && now >= dns_deadline) {
            (void)dns_send_attempt();
        }
    }
    if (ping_state.state == CHOS_NET_PING_PENDING && now >= ping_deadline) {
        if (ping_packet_sent) ping_packet_sent = 0;
        if (ping_state.attempts >= PING_MAX_ATTEMPTS) {
            ping_state.state = CHOS_NET_PING_TIMED_OUT;
        } else {
            (void)send_ping_attempt();
        }
    }
}

void chos_net_stack_get_info(chos_net_stack_info_t *out) {
    size_t i;
    if (out == NULL) return;
    out->dhcp_state = net_state.dhcp_state;
    out->ipv4_address = net_state.ipv4_address;
    out->subnet_mask = net_state.subnet_mask;
    out->gateway_address = net_state.gateway_address;
    out->dns_address = net_state.dns_address;
    out->dhcp_server = net_state.dhcp_server;
    out->lease_seconds = net_state.lease_seconds;
    out->transaction_id = net_state.transaction_id;
    out->retries = net_state.retries;
    out->rx_dhcp_replies = net_state.rx_dhcp_replies;
    out->rejected_dhcp_replies = net_state.rejected_dhcp_replies;
    out->arp_requests_sent = net_state.arp_requests_sent;
    out->arp_replies_received = net_state.arp_replies_received;
    out->driver_ready = net_state.driver_ready;
    for (i = 0u; i < sizeof(net_state.mac); ++i) out->mac[i] = net_state.mac[i];
}

int chos_net_dns_set_server(uint32_t ipv4){
    unsigned first=ipv4>>24;
    if(!ipv4 || first==0 || first==127 || first>=224 || ipv4==UINT32_MAX ||
       net_state.dhcp_state!=CHOS_NET_DHCP_BOUND || dns_state.state==CHOS_NET_DNS_PENDING)return 0;
    dns_override=ipv4;net_state.dns_address=ipv4;dns_state.state=CHOS_NET_DNS_IDLE;return 1;
}

int chos_net_dns_lookup_start(const char *hostname) {
    size_t length = 0u, i;
    /* Do not replace an in-flight query: kernel shell and EL0 clients share
     * one bounded resolver slot until a DNS transaction table is implemented. */
    if (dns_state.state == CHOS_NET_DNS_PENDING) return 0;
    if (hostname == NULL || net_state.dhcp_state != CHOS_NET_DHCP_BOUND || net_state.dns_address == 0u)
        return 0;
    while (hostname[length] != '\0' && length + 1u < sizeof(dns_state.hostname)) ++length;
    if (length == 0u || hostname[length] != '\0') return 0;
    if (chos_net_build_dns_a_query(dns_payload, sizeof(dns_payload), 1u, hostname, 1) == 0u) return 0;
    for (i = 0u; i < sizeof(dns_state.hostname); ++i) dns_state.hostname[i] = '\0';
    for (i = 0u; i < length; ++i) dns_state.hostname[i] = hostname[i];
    dns_query_id = (uint32_t)((uint16_t)(net_now() ^ net_state.transaction_id ^ 0x4d3au));
    if ((uint16_t)dns_query_id == 0u) dns_query_id = 1u;
    dns_state.state = CHOS_NET_DNS_PENDING;
    dns_state.ipv4_address = 0u; dns_state.ttl_seconds = 0u; dns_state.query_id = dns_query_id; dns_state.retries = 0u;
    dns_server_address = net_state.dns_address;
    dns_query_attempts = 0u; dns_arp_attempts = 0u; dns_packet_sent = 0;
    dns_deadline = net_now();
    (void)dns_send_attempt();
    return dns_state.state == CHOS_NET_DNS_PENDING;
}

void chos_net_dns_get_info(chos_net_dns_info_t *out) {
    size_t i;
    if (out == NULL) return;
    out->state = dns_state.state;
    out->ipv4_address = dns_state.ipv4_address;
    out->ttl_seconds = dns_state.ttl_seconds;
    out->query_id = dns_state.query_id;
    out->retries = dns_state.retries;
    for (i = 0u; i < sizeof(dns_state.hostname); ++i) out->hostname[i] = dns_state.hostname[i];
}

const char *chos_net_dhcp_state_name(chos_net_dhcp_state_t state) {
    switch (state) {
        case CHOS_NET_DHCP_OFFLINE: return "offline";
        case CHOS_NET_DHCP_DISCOVERING: return "discovering";
        case CHOS_NET_DHCP_REQUESTING: return "requesting";
        case CHOS_NET_DHCP_BOUND: return "bound";
        case CHOS_NET_DHCP_RENEWING: return "renewing/rebinding";
        case CHOS_NET_DHCP_FAILED: return "retry pending";
        default: return "unknown";
    }
}

const char *chos_net_dns_state_name(chos_net_dns_state_t state) {
    switch (state) {
        case CHOS_NET_DNS_IDLE: return "idle";
        case CHOS_NET_DNS_PENDING: return "pending";
        case CHOS_NET_DNS_RESOLVED: return "resolved";
        case CHOS_NET_DNS_FAILED: return "failed";
        default: return "unknown";
    }
}


int chos_net_ping_start(uint32_t destination_ipv4) {
    if (destination_ipv4 == 0u || destination_ipv4 == 0xffffffffu ||
        net_state.dhcp_state != CHOS_NET_DHCP_BOUND || net_state.ipv4_address == 0u)
        return 0;
    ping_state.state = CHOS_NET_PING_PENDING;
    ping_state.destination_ipv4 = destination_ipv4;
    ping_state.identifier = ++ping_next_id;
    if (ping_state.identifier == 0u) ping_state.identifier = ++ping_next_id;
    ping_state.sequence = 1u;
    ping_state.attempts = 0u;
    ping_state.elapsed_ticks = 0u;
    ping_started_at = net_now();
    ping_deadline = net_now();
    ping_arp_attempts = 0u;
    ping_packet_sent = 0;
    (void)send_ping_attempt();
    return ping_state.state == CHOS_NET_PING_PENDING;
}

void chos_net_ping_get_info(chos_net_ping_info_t *out) {
    if (out == NULL) return;
    out->state = ping_state.state;
    out->destination_ipv4 = ping_state.destination_ipv4;
    out->identifier = ping_state.identifier;
    out->sequence = ping_state.sequence;
    out->attempts = ping_state.attempts;
    out->elapsed_ticks = ping_state.elapsed_ticks;
    if (ping_state.state == CHOS_NET_PING_PENDING && ping_packet_sent)
        out->elapsed_ticks = net_now() - ping_started_at;
}

const char *chos_net_ping_state_name(chos_net_ping_state_t state) {
    switch (state) {
        case CHOS_NET_PING_IDLE: return "idle";
        case CHOS_NET_PING_PENDING: return "pending";
        case CHOS_NET_PING_REPLIED: return "reply received";
        case CHOS_NET_PING_TIMED_OUT: return "timed out";
        case CHOS_NET_PING_FAILED: return "failed";
        default: return "unknown";
    }
}
