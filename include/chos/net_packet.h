#ifndef CHOS_NET_PACKET_H
#define CHOS_NET_PACKET_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *frame;
    const uint8_t *header;
    const uint8_t *payload;
    size_t frame_length;
    size_t header_length;
    size_t payload_length;
    uint32_t source_address; /* IPv4 bytes in network order */
    uint32_t destination_address;
    uint8_t protocol;
} chos_ipv4_view_t;

typedef struct {
    const uint8_t *payload;
    size_t payload_length;
    uint16_t source_port;
    uint16_t destination_port;
} chos_udp_view_t;

typedef struct {
    const uint8_t *payload;
    size_t payload_length;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t type; /* 8=request, 0=reply */
} chos_icmpv4_echo_view_t;

typedef struct {
    const uint8_t *payload;
    size_t payload_length;
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint16_t window;
    uint8_t flags;
    size_t header_length;
} chos_tcp_view_t;

/* Strict, allocation-free parsers suitable for untrusted frames from a driver. */
int chos_net_parse_ethernet(const void *frame, size_t length, uint16_t *ether_type);
int chos_net_validate_arp_ipv4(const void *frame, size_t length, uint16_t *operation);
int chos_net_parse_ipv4(const void *frame, size_t length, chos_ipv4_view_t *view);
int chos_net_parse_udp(const chos_ipv4_view_t *ip, chos_udp_view_t *udp);
int chos_net_parse_tcp(const chos_ipv4_view_t *ip, chos_tcp_view_t *tcp);
int chos_net_ipv4_checksum_valid(const uint8_t *header, size_t header_length);

/* Builders return the complete byte count, or zero for invalid inputs/capacity.
 * IPv4 address parameters are big-endian network-order integers; port, ID and
 * ICMP identifier/sequence parameters are ordinary host-order numbers. */
size_t chos_net_build_arp_ipv4_request(void *output, size_t capacity,
                                      const uint8_t source_mac[6],
                                      uint32_t source_ipv4, uint32_t target_ipv4);
size_t chos_net_build_udp_ipv4_frame(void *output, size_t capacity,
                                     const uint8_t source_mac[6],
                                     const uint8_t destination_mac[6],
                                     uint32_t source_ipv4, uint32_t destination_ipv4,
                                     uint16_t source_port, uint16_t destination_port,
                                     uint16_t identification, uint8_t ttl,
                                     const void *payload, size_t payload_length);
size_t chos_net_build_tcp_ipv4_frame(void *output, size_t capacity,
                                     const uint8_t source_mac[6],
                                     const uint8_t destination_mac[6],
                                     uint32_t source_ipv4, uint32_t destination_ipv4,
                                     uint16_t source_port, uint16_t destination_port,
                                     uint32_t sequence, uint32_t acknowledgement,
                                     uint8_t flags, uint16_t window,
                                     uint16_t identification, uint8_t ttl,
                                     const void *payload, size_t payload_length);
size_t chos_net_build_icmpv4_echo(void *output, size_t capacity, uint8_t type,
                                  uint16_t identifier, uint16_t sequence,
                                  const void *payload, size_t payload_length);
size_t chos_net_build_icmpv4_frame(void *output, size_t capacity,
                                   const uint8_t source_mac[6],
                                   const uint8_t destination_mac[6],
                                   uint32_t source_ipv4, uint32_t destination_ipv4,
                                   uint16_t identification, uint8_t ttl, uint8_t type,
                                   uint16_t echo_identifier, uint16_t echo_sequence,
                                   const void *payload, size_t payload_length);
int chos_net_parse_icmpv4_echo(const chos_ipv4_view_t *ip, chos_icmpv4_echo_view_t *view);

#endif
