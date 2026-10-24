#include <stddef.h>
#include <stdint.h>
#include "chos/net_packet.h"

#define ETH_HEADER_BYTES 14u
#define ETHERTYPE_ARP 0x0806u
#define ETHERTYPE_IPV4 0x0800u
#define ARP_IPV4_BYTES 28u
#define IPV4_MIN_HEADER_BYTES 20u
#define UDP_HEADER_BYTES 8u
#define ICMP_HEADER_BYTES 8u
#define TCP_MIN_HEADER_BYTES 20u
#define ETH_HEADER_BYTES_MAX_PAYLOAD 1500u
#define IPV4_UDP_MAX_PAYLOAD (ETH_HEADER_BYTES_MAX_PAYLOAD - IPV4_MIN_HEADER_BYTES - UDP_HEADER_BYTES)
#define IPV4_TCP_MAX_PAYLOAD (ETH_HEADER_BYTES_MAX_PAYLOAD - IPV4_MIN_HEADER_BYTES - TCP_MIN_HEADER_BYTES)

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void write_be16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void write_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t checksum_add(uint32_t sum, const uint8_t *bytes, size_t length) {
    size_t i = 0u;
    while (i + 1u < length) {
        sum += ((uint32_t)bytes[i] << 8) | bytes[i + 1u];
        while (sum > 0xffffu) sum = (sum & 0xffffu) + (sum >> 16);
        i += 2u;
    }
    if (i < length) {
        sum += (uint32_t)bytes[i] << 8;
        while (sum > 0xffffu) sum = (sum & 0xffffu) + (sum >> 16);
    }
    return sum;
}

static uint16_t checksum_finish(uint32_t sum) {
    while (sum > 0xffffu) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t checksum_bytes(const uint8_t *bytes, size_t length) {
    return checksum_finish(checksum_add(0u, bytes, length));
}

int chos_net_parse_ethernet(const void *frame, size_t length, uint16_t *ether_type) {
    const uint8_t *bytes = (const uint8_t *)frame;
    if (frame == NULL || ether_type == NULL || length < ETH_HEADER_BYTES) return 0;
    *ether_type = read_be16(&bytes[12]);
    return 1;
}

int chos_net_validate_arp_ipv4(const void *frame, size_t length, uint16_t *operation) {
    const uint8_t *bytes = (const uint8_t *)frame;
    uint16_t type;
    const uint8_t *arp;
    if (!chos_net_parse_ethernet(frame, length, &type) || type != ETHERTYPE_ARP ||
        length < ETH_HEADER_BYTES + ARP_IPV4_BYTES) return 0;
    arp = &bytes[ETH_HEADER_BYTES];
    if (read_be16(&arp[0]) != 1u || read_be16(&arp[2]) != ETHERTYPE_IPV4 ||
        arp[4] != 6u || arp[5] != 4u) return 0;
    if (read_be16(&arp[6]) != 1u && read_be16(&arp[6]) != 2u) return 0;
    if (operation != NULL) *operation = read_be16(&arp[6]);
    return 1;
}

int chos_net_ipv4_checksum_valid(const uint8_t *header, size_t header_length) {
    uint32_t sum = 0u;
    size_t i;
    if (header == NULL || header_length < IPV4_MIN_HEADER_BYTES ||
        (header_length & 1u) != 0u) return 0;
    for (i = 0u; i < header_length; i += 2u) {
        sum += ((uint32_t)header[i] << 8) | header[i + 1u];
        while (sum > 0xffffu) sum = (sum & 0xffffu) + (sum >> 16);
    }
    return sum == 0xffffu;
}

int chos_net_parse_ipv4(const void *frame, size_t length, chos_ipv4_view_t *view) {
    const uint8_t *bytes = (const uint8_t *)frame;
    const uint8_t *ip;
    uint16_t type, total_length;
    size_t header_length, available_payload;
    if (view == NULL || !chos_net_parse_ethernet(frame, length, &type) || type != ETHERTYPE_IPV4 ||
        length < ETH_HEADER_BYTES + IPV4_MIN_HEADER_BYTES) return 0;
    ip = &bytes[ETH_HEADER_BYTES];
    if ((ip[0] >> 4) != 4u) return 0;
    header_length = (size_t)(ip[0] & 0x0fu) * 4u;
    if (header_length < IPV4_MIN_HEADER_BYTES || header_length > 60u ||
        length < ETH_HEADER_BYTES + header_length) return 0;
    total_length = read_be16(&ip[2]);
    if ((size_t)total_length < header_length) return 0;
    available_payload = length - ETH_HEADER_BYTES - header_length;
    if ((size_t)total_length - header_length > available_payload) return 0;
    /* Reject IPv4 frames with invalid checksums before handing them to protocols. */
    if (!chos_net_ipv4_checksum_valid(ip, header_length)) return 0;
    view->frame = bytes;
    view->header = ip;
    view->payload = ip + header_length;
    view->frame_length = length;
    view->header_length = header_length;
    view->payload_length = (size_t)total_length - header_length;
    view->source_address = ((uint32_t)ip[12] << 24) | ((uint32_t)ip[13] << 16) |
                           ((uint32_t)ip[14] << 8) | ip[15];
    view->destination_address = ((uint32_t)ip[16] << 24) | ((uint32_t)ip[17] << 16) |
                                ((uint32_t)ip[18] << 8) | ip[19];
    view->protocol = ip[9];
    return 1;
}

int chos_net_parse_udp(const chos_ipv4_view_t *ip, chos_udp_view_t *udp) {
    uint16_t udp_length, udp_checksum;
    if (ip == NULL || udp == NULL || ip->protocol != 17u || ip->payload == NULL ||
        ip->header == NULL || ip->payload_length < UDP_HEADER_BYTES) return 0;
    udp_length = read_be16(&ip->payload[4]);
    if (udp_length < UDP_HEADER_BYTES || (size_t)udp_length > ip->payload_length) return 0;
    udp_checksum = read_be16(&ip->payload[6]);
    if (udp_checksum != 0u) {
        uint8_t pseudo[12];
        uint32_t sum;
        pseudo[0] = ip->header[12]; pseudo[1] = ip->header[13];
        pseudo[2] = ip->header[14]; pseudo[3] = ip->header[15];
        pseudo[4] = ip->header[16]; pseudo[5] = ip->header[17];
        pseudo[6] = ip->header[18]; pseudo[7] = ip->header[19];
        pseudo[8] = 0u; pseudo[9] = 17u;
        pseudo[10] = (uint8_t)(udp_length >> 8); pseudo[11] = (uint8_t)udp_length;
        sum = checksum_add(0u, pseudo, sizeof(pseudo));
        sum = checksum_add(sum, ip->payload, (size_t)udp_length);
        if (checksum_finish(sum) != 0u) return 0;
    }
    udp->source_port = read_be16(&ip->payload[0]);
    udp->destination_port = read_be16(&ip->payload[2]);
    udp->payload = &ip->payload[UDP_HEADER_BYTES];
    udp->payload_length = (size_t)udp_length - UDP_HEADER_BYTES;
    return 1;
}

int chos_net_parse_tcp(const chos_ipv4_view_t *ip, chos_tcp_view_t *tcp) {
    const uint8_t *segment;
    size_t header_length, segment_length;
    uint32_t sum;
    uint8_t pseudo[12];
    if (ip == NULL || tcp == NULL || ip->protocol != 6u || ip->header == NULL ||
        ip->payload == NULL || ip->payload_length < TCP_MIN_HEADER_BYTES) return 0;
    segment = ip->payload;
    segment_length = ip->payload_length;
    header_length = (size_t)(segment[12] >> 4) * 4u;
    if (header_length < TCP_MIN_HEADER_BYTES || header_length > 60u ||
        header_length > segment_length) return 0;
    for (size_t i = 0u; i < 4u; ++i) {
        pseudo[i] = ip->header[12u + i];
        pseudo[4u + i] = ip->header[16u + i];
    }
    pseudo[8] = 0u; pseudo[9] = 6u;
    write_be16(&pseudo[10], (uint16_t)segment_length);
    sum = checksum_add(0u, pseudo, sizeof(pseudo));
    sum = checksum_add(sum, segment, segment_length);
    if (checksum_finish(sum) != 0u) return 0;
    tcp->source_port = read_be16(&segment[0]);
    tcp->destination_port = read_be16(&segment[2]);
    tcp->sequence = ((uint32_t)segment[4] << 24) | ((uint32_t)segment[5] << 16) |
                    ((uint32_t)segment[6] << 8) | segment[7];
    tcp->acknowledgement = ((uint32_t)segment[8] << 24) | ((uint32_t)segment[9] << 16) |
                           ((uint32_t)segment[10] << 8) | segment[11];
    tcp->flags = segment[13];
    tcp->window = read_be16(&segment[14]);
    tcp->header_length = header_length;
    tcp->payload = segment + header_length;
    tcp->payload_length = segment_length - header_length;
    return 1;
}

size_t chos_net_build_arp_ipv4_request(void *output, size_t capacity,
                                      const uint8_t source_mac[6],
                                      uint32_t source_ipv4, uint32_t target_ipv4) {
    uint8_t *frame = (uint8_t *)output;
    size_t i;
    if (frame == NULL || source_mac == NULL || capacity < ETH_HEADER_BYTES + ARP_IPV4_BYTES ||
        target_ipv4 == 0u) return 0u;
    for (i = 0u; i < 6u; ++i) {
        frame[i] = 0xffu;
        frame[6u + i] = source_mac[i];
        frame[22u + i] = source_mac[i];
        frame[32u + i] = 0u;
    }
    write_be16(&frame[12], ETHERTYPE_ARP);
    write_be16(&frame[14], 1u); /* Ethernet hardware */
    write_be16(&frame[16], ETHERTYPE_IPV4);
    frame[18] = 6u;
    frame[19] = 4u;
    write_be16(&frame[20], 1u); /* ARP request */
    write_be32(&frame[28], source_ipv4);
    write_be32(&frame[38], target_ipv4);
    return ETH_HEADER_BYTES + ARP_IPV4_BYTES;
}

size_t chos_net_build_udp_ipv4_frame(void *output, size_t capacity,
                                     const uint8_t source_mac[6],
                                     const uint8_t destination_mac[6],
                                     uint32_t source_ipv4, uint32_t destination_ipv4,
                                     uint16_t source_port, uint16_t destination_port,
                                     uint16_t identification, uint8_t ttl,
                                     const void *payload, size_t payload_length) {
    uint8_t *frame = (uint8_t *)output;
    uint8_t *ip, *udp;
    const uint8_t *payload_bytes = (const uint8_t *)payload;
    size_t ip_length, udp_length, frame_length, i;
    uint32_t sum;
    uint16_t checksum;
    if (frame == NULL || source_mac == NULL || destination_mac == NULL || ttl == 0u ||
        (payload_length != 0u && payload_bytes == NULL) || payload_length > IPV4_UDP_MAX_PAYLOAD)
        return 0u;
    ip_length = IPV4_MIN_HEADER_BYTES + UDP_HEADER_BYTES + payload_length;
    udp_length = UDP_HEADER_BYTES + payload_length;
    frame_length = ETH_HEADER_BYTES + ip_length;
    if (capacity < frame_length) return 0u;
    for (i = 0u; i < 6u; ++i) { frame[i] = destination_mac[i]; frame[6u + i] = source_mac[i]; }
    write_be16(&frame[12], ETHERTYPE_IPV4);
    ip = &frame[ETH_HEADER_BYTES];
    for (i = 0u; i < IPV4_MIN_HEADER_BYTES + UDP_HEADER_BYTES; ++i) ip[i] = 0u;
    ip[0] = 0x45u;
    write_be16(&ip[2], (uint16_t)ip_length);
    write_be16(&ip[4], identification);
    write_be16(&ip[6], 0x4000u); /* Don't fragment. */
    ip[8] = ttl;
    ip[9] = 17u;
    write_be32(&ip[12], source_ipv4);
    write_be32(&ip[16], destination_ipv4);
    checksum = checksum_bytes(ip, IPV4_MIN_HEADER_BYTES);
    write_be16(&ip[10], checksum);
    udp = &ip[IPV4_MIN_HEADER_BYTES];
    write_be16(&udp[0], source_port);
    write_be16(&udp[2], destination_port);
    write_be16(&udp[4], (uint16_t)udp_length);
    write_be16(&udp[6], 0u);
    if (payload_length != 0u) {
        for (i = 0u; i < payload_length; ++i) udp[UDP_HEADER_BYTES + i] = payload_bytes[i];
    }
    sum = checksum_add(0u, &ip[12], 8u);
    sum += 17u + (uint32_t)udp_length;
    while (sum > 0xffffu) sum = (sum & 0xffffu) + (sum >> 16);
    sum = checksum_add(sum, udp, udp_length);
    checksum = checksum_finish(sum);
    if (checksum == 0u) checksum = 0xffffu;
    write_be16(&udp[6], checksum);
    return frame_length;
}

size_t chos_net_build_tcp_ipv4_frame(void *output, size_t capacity,
                                     const uint8_t source_mac[6],
                                     const uint8_t destination_mac[6],
                                     uint32_t source_ipv4, uint32_t destination_ipv4,
                                     uint16_t source_port, uint16_t destination_port,
                                     uint32_t sequence, uint32_t acknowledgement,
                                     uint8_t flags, uint16_t window,
                                     uint16_t identification, uint8_t ttl,
                                     const void *payload, size_t payload_length) {
    uint8_t *frame = (uint8_t *)output;
    uint8_t *ip, *tcp;
    size_t tcp_length, ip_length, frame_length, i;
    uint32_t sum;
    uint16_t checksum;
    uint8_t pseudo[12];
    const uint8_t *payload_bytes = (const uint8_t *)payload;
    if (frame == NULL || source_mac == NULL || destination_mac == NULL || ttl == 0u ||
        (payload_length != 0u && payload_bytes == NULL) || payload_length > IPV4_TCP_MAX_PAYLOAD)
        return 0u;
    tcp_length = TCP_MIN_HEADER_BYTES + payload_length;
    ip_length = IPV4_MIN_HEADER_BYTES + tcp_length;
    frame_length = ETH_HEADER_BYTES + ip_length;
    if (capacity < frame_length) return 0u;
    for (i = 0u; i < 6u; ++i) {
        frame[i] = destination_mac[i];
        frame[6u + i] = source_mac[i];
    }
    write_be16(&frame[12], ETHERTYPE_IPV4);
    ip = &frame[ETH_HEADER_BYTES];
    for (i = 0u; i < IPV4_MIN_HEADER_BYTES; ++i) ip[i] = 0u;
    ip[0] = 0x45u;
    write_be16(&ip[2], (uint16_t)ip_length);
    write_be16(&ip[4], identification);
    write_be16(&ip[6], 0x4000u);
    ip[8] = ttl;
    ip[9] = 6u;
    write_be32(&ip[12], source_ipv4);
    write_be32(&ip[16], destination_ipv4);
    checksum = checksum_bytes(ip, IPV4_MIN_HEADER_BYTES);
    write_be16(&ip[10], checksum);
    tcp = &ip[IPV4_MIN_HEADER_BYTES];
    for (i = 0u; i < TCP_MIN_HEADER_BYTES; ++i) tcp[i] = 0u;
    write_be16(&tcp[0], source_port);
    write_be16(&tcp[2], destination_port);
    write_be32(&tcp[4], sequence);
    write_be32(&tcp[8], acknowledgement);
    tcp[12] = 0x50u; /* Five 32-bit words, no options. */
    tcp[13] = flags;
    write_be16(&tcp[14], window);
    if (payload_length != 0u)
        for (i = 0u; i < payload_length; ++i) tcp[TCP_MIN_HEADER_BYTES + i] = payload_bytes[i];
    for (i = 0u; i < 4u; ++i) {
        pseudo[i] = ip[12u + i];
        pseudo[4u + i] = ip[16u + i];
    }
    pseudo[8] = 0u; pseudo[9] = 6u;
    write_be16(&pseudo[10], (uint16_t)tcp_length);
    sum = checksum_add(0u, pseudo, sizeof(pseudo));
    sum = checksum_add(sum, tcp, tcp_length);
    checksum = checksum_finish(sum);
    write_be16(&tcp[16], checksum);
    return frame_length;
}

size_t chos_net_build_icmpv4_echo(void *output, size_t capacity, uint8_t type,
                                  uint16_t identifier, uint16_t sequence,
                                  const void *payload, size_t payload_length) {
    uint8_t *icmp = (uint8_t *)output;
    const uint8_t *data = (const uint8_t *)payload;
    size_t length, i;
    uint16_t checksum;
    if (icmp == NULL || (type != 0u && type != 8u) ||
        (payload_length != 0u && data == NULL) || payload_length > 65535u - ICMP_HEADER_BYTES)
        return 0u;
    length = ICMP_HEADER_BYTES + payload_length;
    if (capacity < length) return 0u;
    for (i = 0u; i < ICMP_HEADER_BYTES; ++i) icmp[i] = 0u;
    icmp[0] = type;
    write_be16(&icmp[4], identifier);
    write_be16(&icmp[6], sequence);
    for (i = 0u; i < payload_length; ++i) icmp[ICMP_HEADER_BYTES + i] = data[i];
    checksum = checksum_bytes(icmp, length);
    write_be16(&icmp[2], checksum);
    return length;
}


size_t chos_net_build_icmpv4_frame(void *output, size_t capacity,
                                   const uint8_t source_mac[6],
                                   const uint8_t destination_mac[6],
                                   uint32_t source_ipv4, uint32_t destination_ipv4,
                                   uint16_t identification, uint8_t ttl, uint8_t type,
                                   uint16_t echo_identifier, uint16_t echo_sequence,
                                   const void *payload, size_t payload_length) {
    uint8_t *frame = (uint8_t *)output;
    uint8_t *ip;
    size_t icmp_length, ip_length, frame_length, i;
    uint16_t checksum;
    if (frame == NULL || source_mac == NULL || destination_mac == NULL || ttl == 0u ||
        (payload_length != 0u && payload == NULL) ||
        payload_length > ETH_HEADER_BYTES_MAX_PAYLOAD - IPV4_MIN_HEADER_BYTES - ICMP_HEADER_BYTES)
        return 0u;
    icmp_length = ICMP_HEADER_BYTES + payload_length;
    ip_length = IPV4_MIN_HEADER_BYTES + icmp_length;
    frame_length = ETH_HEADER_BYTES + ip_length;
    if (capacity < frame_length) return 0u;
    for (i = 0u; i < 6u; ++i) {
        frame[i] = destination_mac[i];
        frame[6u + i] = source_mac[i];
    }
    write_be16(&frame[12], ETHERTYPE_IPV4);
    ip = &frame[ETH_HEADER_BYTES];
    for (i = 0u; i < IPV4_MIN_HEADER_BYTES; ++i) ip[i] = 0u;
    ip[0] = 0x45u;
    write_be16(&ip[2], (uint16_t)ip_length);
    write_be16(&ip[4], identification);
    write_be16(&ip[6], 0x4000u);
    ip[8] = ttl;
    ip[9] = 1u;
    write_be32(&ip[12], source_ipv4);
    write_be32(&ip[16], destination_ipv4);
    checksum = checksum_bytes(ip, IPV4_MIN_HEADER_BYTES);
    write_be16(&ip[10], checksum);
    if (chos_net_build_icmpv4_echo(&ip[IPV4_MIN_HEADER_BYTES], icmp_length, type,
            echo_identifier, echo_sequence, payload, payload_length) != icmp_length)
        return 0u;
    return frame_length;
}

int chos_net_parse_icmpv4_echo(const chos_ipv4_view_t *ip, chos_icmpv4_echo_view_t *view) {
    const uint8_t *icmp;
    uint16_t fragment;
    if (ip == NULL || view == NULL || ip->header == NULL || ip->payload == NULL ||
        ip->protocol != 1u || ip->payload_length < ICMP_HEADER_BYTES) return 0;
    fragment = read_be16(&ip->header[6]);
    if ((fragment & 0x3fffu) != 0u) return 0; /* No fragmented echo datagrams. */
    icmp = ip->payload;
    if ((icmp[0] != 0u && icmp[0] != 8u) || icmp[1] != 0u ||
        checksum_bytes(icmp, ip->payload_length) != 0u) return 0;
    view->type = icmp[0];
    view->identifier = read_be16(&icmp[4]);
    view->sequence = read_be16(&icmp[6]);
    view->payload = &icmp[ICMP_HEADER_BYTES];
    view->payload_length = ip->payload_length - ICMP_HEADER_BYTES;
    return 1;
}
