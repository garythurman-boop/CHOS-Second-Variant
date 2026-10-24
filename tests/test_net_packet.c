#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "chos/net_packet.h"
#include "chos/virtio_net_abi.h"
#include <string.h>

static uint16_t sum_header(uint8_t *h, size_t n) {
    uint32_t sum = 0u;
    size_t i;
    for (i = 0u; i < n; i += 2u) {
        sum += ((uint32_t)h[i] << 8) | h[i + 1u];
        while (sum > 0xffffu) sum = (sum & 0xffffu) + (sum >> 16);
    }
    while (sum > 0xffffu) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}

int main(void) {
    uint8_t frame[1518] = {0};
    uint8_t tx_chain[CHOS_VIRTIO_NET_HEADER_V1_SIZE + 1518] = {0};
    chos_virtio_net_header_v1_t virtio_header = {0};
    uint8_t source_mac[6] = {0x52u, 0x54u, 0x00u, 0x12u, 0x34u, 0x56u};
    uint8_t destination_mac[6] = {0x52u, 0x55u, 0x00u, 0x65u, 0x43u, 0x21u};
    const uint8_t udp_payload[] = {0x43u, 0x48u, 0x4fu, 0x53u};
    const uint8_t echo_payload[] = {0x43u, 0x48u, 0x4fu, 0x53u};
    uint8_t icmp[32];
    uint16_t ethertype = 0u, operation = 0u;
    chos_ipv4_view_t ip;
    chos_udp_view_t udp;
    chos_icmpv4_echo_view_t echo;
    chos_tcp_view_t tcp;
    const uint8_t tcp_payload[] = {'C','H','O','S'};
    size_t i, frame_length, icmp_length;

    frame_length = chos_net_build_udp_ipv4_frame(frame, sizeof(frame), source_mac, destination_mac,
                                                  0x0a00020fu, 0x0a000202u,
                                                  12345u, 53u, 0x4567u, 64u,
                                                  udp_payload, sizeof(udp_payload));
    assert(frame_length == 46u);
    /* Modern VirtIO TX prepends the full 12-byte v1 header. Assert the Ethernet
     * destination and EtherType remain aligned after the transport header. */
    assert(sizeof(virtio_header) == 12u);
    assert(virtio_header.num_buffers == 0u);
    memcpy(tx_chain, &virtio_header, sizeof(virtio_header));
    memcpy(tx_chain + sizeof(virtio_header), frame, frame_length);
    assert(memcmp(tx_chain + CHOS_VIRTIO_NET_HEADER_V1_SIZE, destination_mac, 6u) == 0);
    assert(memcmp(tx_chain + CHOS_VIRTIO_NET_HEADER_V1_SIZE + 6u, source_mac, 6u) == 0);
    assert(tx_chain[CHOS_VIRTIO_NET_HEADER_V1_SIZE + 12u] == 0x08u);
    assert(tx_chain[CHOS_VIRTIO_NET_HEADER_V1_SIZE + 13u] == 0x00u);
    assert(chos_net_parse_ethernet(frame, frame_length, &ethertype) && ethertype == 0x0800u);
    assert(chos_net_parse_ipv4(frame, frame_length, &ip));
    assert(ip.header_length == 20u && ip.payload_length == 12u && ip.protocol == 17u);
    assert(ip.source_address == 0x0a00020fu && ip.destination_address == 0x0a000202u);
    assert(chos_net_parse_udp(&ip, &udp));
    assert(udp.source_port == 12345u && udp.destination_port == 53u && udp.payload_length == sizeof(udp_payload));
    assert(memcmp(udp.payload, udp_payload, sizeof(udp_payload)) == 0);
    assert(frame[14u + 10u] != 0u || frame[14u + 11u] != 0u);
    assert(frame[14u + 20u + 6u] != 0u || frame[14u + 20u + 7u] != 0u);
    frame[14u + 20u + 6u] ^= 0x01u;
    assert(!chos_net_parse_udp(&ip, &udp)); /* Nonzero but invalid UDP checksum is rejected. */
    frame[14u + 20u + 6u] ^= 0x01u;
    assert(chos_net_parse_udp(&ip, &udp));
    /* Every truncation before the complete Ethernet/IP datagram is rejected. */
    for (i = 0u; i < frame_length; ++i) assert(!chos_net_parse_ipv4(frame, i, &ip));
    assert(chos_net_build_udp_ipv4_frame(frame, 45u, source_mac, destination_mac,
                                         0x0a00020fu, 0x0a000202u, 1u, 2u, 3u, 64u,
                                         udp_payload, sizeof(udp_payload)) == 0u);
    assert(chos_net_build_udp_ipv4_frame(frame, sizeof(frame), source_mac, destination_mac,
                                         0x0a00020fu, 0x0a000202u, 1u, 2u, 3u, 0u,
                                         udp_payload, sizeof(udp_payload)) == 0u);

    /* Build and then validate an Ethernet/IPv4 ARP request. */
    frame_length = chos_net_build_arp_ipv4_request(frame, sizeof(frame), source_mac,
                                                    0xc0a80102u, 0xc0a80101u);
    assert(frame_length == 42u);
    for (i = 0u; i < 6u; ++i) assert(frame[i] == 0xffu);
    assert(memcmp(&frame[6], source_mac, 6u) == 0);
    assert(chos_net_validate_arp_ipv4(frame, frame_length, &operation) && operation == 1u);
    assert(frame[28] == 192u && frame[29] == 168u && frame[30] == 1u && frame[31] == 2u);
    assert(frame[38] == 192u && frame[39] == 168u && frame[40] == 1u && frame[41] == 1u);
    assert(chos_net_build_arp_ipv4_request(frame, 41u, source_mac, 1u, 2u) == 0u);

    /* ICMPv4 echo builder/parser round-trip, including checksum and payload. */
    icmp_length = chos_net_build_icmpv4_echo(icmp, sizeof(icmp), 8u, 0x1234u, 7u,
                                              echo_payload, sizeof(echo_payload));
    assert(icmp_length == 12u && icmp[0] == 8u);
    memset(frame, 0, sizeof(frame));
    memcpy(frame, destination_mac, 6u); memcpy(&frame[6], source_mac, 6u);
    frame[12] = 0x08u; frame[13] = 0x00u;
    frame[14] = 0x45u; frame[16] = 0u; frame[17] = (uint8_t)(20u + icmp_length);
    frame[22] = 64u; frame[23] = 1u;
    frame[26] = 192u; frame[27] = 168u; frame[28] = 1u; frame[29] = 2u;
    frame[30] = 192u; frame[31] = 168u; frame[32] = 1u; frame[33] = 1u;
    {
        uint16_t checksum = sum_header(&frame[14], 20u);
        frame[24] = (uint8_t)(checksum >> 8); frame[25] = (uint8_t)checksum;
    }
    memcpy(&frame[34], icmp, icmp_length);
    assert(chos_net_parse_ipv4(frame, 34u + icmp_length, &ip));
    assert(chos_net_parse_icmpv4_echo(&ip, &echo));
    assert(echo.type == 8u && echo.identifier == 0x1234u && echo.sequence == 7u);
    assert(echo.payload_length == sizeof(echo_payload) && memcmp(echo.payload, echo_payload, sizeof(echo_payload)) == 0);
    frame[36] ^= 1u;
    assert(chos_net_parse_ipv4(frame, 34u + icmp_length, &ip));
    assert(!chos_net_parse_icmpv4_echo(&ip, &echo));

    /* TCP SYN/ACK packet construction includes an IPv4 pseudo-header checksum.
     * Parsing validates the checksum, data offset, fields and payload bounds. */
    frame_length = chos_net_build_tcp_ipv4_frame(frame, sizeof(frame), source_mac, destination_mac,
        0xc0a80164u, 0xc0a80101u, 49152u, 443u, 0x10203040u, 0x50607080u,
        0x12u, 64240u, 0x7788u, 64u, tcp_payload, sizeof(tcp_payload));
    assert(frame_length == 58u);
    assert(chos_net_parse_ipv4(frame, frame_length, &ip));
    assert(ip.protocol == 6u && ip.source_address == 0xc0a80164u);
    assert(chos_net_parse_tcp(&ip, &tcp));
    assert(tcp.source_port == 49152u && tcp.destination_port == 443u);
    assert(tcp.sequence == 0x10203040u && tcp.acknowledgement == 0x50607080u);
    assert(tcp.flags == 0x12u && tcp.window == 64240u && tcp.header_length == 20u);
    assert(tcp.payload_length == sizeof(tcp_payload));
    assert(memcmp(tcp.payload, tcp_payload, sizeof(tcp_payload)) == 0);
    frame[14u + 20u + 16u] ^= 0x01u;
    assert(chos_net_parse_ipv4(frame, frame_length, &ip));
    assert(!chos_net_parse_tcp(&ip, &tcp));
    assert(chos_net_build_tcp_ipv4_frame(frame, 53u, source_mac, destination_mac,
        1u, 2u, 1u, 2u, 0u, 0u, 0x02u, 4096u, 1u, 64u, NULL, 0u) == 0u);

    puts("network packets: ARP, IPv4/UDP, ICMP echo and TCP checksum/bounds builders/parsers passed");
    return 0;
}
