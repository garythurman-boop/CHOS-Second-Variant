#ifndef CHOS_NET_STACK_H
#define CHOS_NET_STACK_H

#include <stdint.h>

#define CHOS_NET_DNS_HOST_MAX 256u

typedef enum {
    CHOS_NET_DHCP_OFFLINE = 0,
    CHOS_NET_DHCP_DISCOVERING,
    CHOS_NET_DHCP_REQUESTING,
    CHOS_NET_DHCP_BOUND,
    CHOS_NET_DHCP_RENEWING,
    CHOS_NET_DHCP_FAILED
} chos_net_dhcp_state_t;

typedef enum {
    CHOS_NET_DNS_IDLE = 0,
    CHOS_NET_DNS_PENDING,
    CHOS_NET_DNS_RESOLVED,
    CHOS_NET_DNS_FAILED
} chos_net_dns_state_t;

typedef struct {
    chos_net_dhcp_state_t dhcp_state;
    uint8_t mac[6];
    uint32_t ipv4_address;
    uint32_t subnet_mask;
    uint32_t gateway_address;
    uint32_t dns_address;
    uint32_t dhcp_server;
    uint32_t lease_seconds;
    uint32_t transaction_id;
    uint32_t retries;
    uint64_t rx_dhcp_replies;
    uint64_t rejected_dhcp_replies;
    uint64_t arp_requests_sent;
    uint64_t arp_replies_received;
    int driver_ready;
} chos_net_stack_info_t;

typedef enum {
    CHOS_NET_PING_IDLE = 0,
    CHOS_NET_PING_PENDING,
    CHOS_NET_PING_REPLIED,
    CHOS_NET_PING_TIMED_OUT,
    CHOS_NET_PING_FAILED
} chos_net_ping_state_t;

typedef struct {
    chos_net_dns_state_t state;
    char hostname[CHOS_NET_DNS_HOST_MAX];
    uint32_t ipv4_address;
    uint32_t ttl_seconds;
    uint32_t query_id;
    uint32_t retries;
} chos_net_dns_info_t;

typedef struct {
    chos_net_ping_state_t state;
    uint32_t destination_ipv4;
    uint16_t identifier;
    uint16_t sequence;
    uint32_t attempts;
    uint64_t elapsed_ticks;
} chos_net_ping_info_t;

/* Initializes the non-blocking DHCP client if the VirtIO-net driver is ready. */
void chos_net_stack_init(void);
/* Call repeatedly from the shell/runtime poll loop. Never busy-waits. */
void chos_net_stack_poll(void);
void chos_net_stack_get_info(chos_net_stack_info_t *out);
const char *chos_net_dhcp_state_name(chos_net_dhcp_state_t state);
/* Start one asynchronous DNS A/IN lookup. Use get_dns_info to poll the result. */
int chos_net_dns_lookup_start(const char *hostname);
void chos_net_dns_get_info(chos_net_dns_info_t *out);
const char *chos_net_dns_state_name(chos_net_dns_state_t state);
/* Starts one ICMPv4 echo request to a dotted-decimal IPv4 address. */
int chos_net_ping_start(uint32_t destination_ipv4);
void chos_net_ping_get_info(chos_net_ping_info_t *out);
const char *chos_net_ping_state_name(chos_net_ping_state_t state);

#endif
