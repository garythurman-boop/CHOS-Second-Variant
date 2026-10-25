#ifndef CHOS_NET_SERVICES_H
#define CHOS_NET_SERVICES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t transaction_id;
    uint32_t offered_address; /* IPv4 bytes in network order. */
    uint32_t server_address;
    uint32_t subnet_mask;
    uint32_t router_address;
    uint32_t dns_address;
    uint32_t lease_seconds;
    uint8_t message_type; /* DHCP 2=OFFER, 5=ACK, 6=NAK, etc. */
    uint8_t has_server_address;
    uint8_t has_subnet_mask;
    uint8_t has_router;
    uint8_t has_dns;
    uint8_t has_lease;
} chos_dhcpv4_message_t;

/* Build the BOOTP/DHCP Discover payload (not an Ethernet/IP/UDP frame).
 * Returns its byte count, or 0 for invalid inputs/capacity. */
size_t chos_net_build_dhcpv4_discover(void *output, size_t capacity,
                                      uint32_t transaction_id,
                                      const uint8_t mac_address[6]);

/* Build the selecting-state DHCPREQUEST payload after a matching OFFER.
 * offered_address and server_address are big-endian network-order integers. */
size_t chos_net_build_dhcpv4_request(void *output, size_t capacity,
                                     uint32_t transaction_id,
                                     const uint8_t mac_address[6],
                                     uint32_t offered_address,
                                     uint32_t server_address);

/* Parse a bounded DHCPv4 reply payload. expected_xid must match the reply's
 * transaction ID; only a valid BOOTREPLY with Ethernet hardware and a valid
 * DHCP cookie/options is accepted. Address values are network-order integers. */
int chos_net_parse_dhcpv4_reply(const void *payload, size_t length,
                                uint32_t expected_xid,
                                chos_dhcpv4_message_t *message);

/* Parse a DNS response for one expected A/IN question and return the first
 * IPv4 A/IN answer. Names are compared ASCII case-insensitively. TTL is
 * optional; all packet offsets and compression pointers are bounds-checked. */
/* Build a bounded DNS A/IN query for a dotted host name (optional final dot).
 * Returns wire length, or zero for an invalid name or inadequate capacity. */
size_t chos_net_build_dns_a_query(void *output, size_t capacity, uint16_t query_id,
                                  const char *hostname, int recursion_desired);

int chos_net_parse_dns_a_response(const void *payload, size_t length,
                                  uint16_t expected_id, const char *expected_name,
                                  uint32_t *address_out, uint32_t *ttl_out);

/* Bounded AAAA query/response. Result 1=address, 0=malformed/mismatched,
 * negative=matching DNS RCODE; only expected owner/bounded CNAME chain accepted. */
size_t chos_net_build_dns_aaaa_query(void *,size_t,uint16_t,const char *);
int chos_net_parse_dns_aaaa_response(const void *,size_t,uint16_t,const char *,uint8_t[16],uint32_t *);
#endif
