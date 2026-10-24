#ifndef CHOS_NET_DNS_API_H
#define CHOS_NET_DNS_API_H
#include <stdint.h>
#define CHOS_NET_DNS_API_HOST_MAX 256u
typedef enum {
    CHOS_DNS_API_IDLE = 0,
    CHOS_DNS_API_PENDING,
    CHOS_DNS_API_RESOLVED,
    CHOS_DNS_API_FAILED
} chos_dns_api_state_t;
typedef struct {
    chos_dns_api_state_t state;
    char hostname[CHOS_NET_DNS_API_HOST_MAX];
    uint32_t ipv4_address;
    uint32_t ttl_seconds;
    uint32_t query_id;
    uint32_t retries;
} chos_dns_api_info_t;
_Static_assert(sizeof(chos_dns_api_info_t) == 276u, "DNS syscall ABI layout changed");
#endif
