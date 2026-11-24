#ifndef CHOS_LIBC_NETINET_IN_H
#define CHOS_LIBC_NETINET_IN_H

#include <stdint.h>

#define AF_INET 2
#define AF_INET6 10
#define INADDR_LOOPBACK UINT32_C(0x7f000001)

typedef uint32_t in_addr_t;
struct in_addr { in_addr_t s_addr; };
struct in6_addr {
    union {
        uint8_t bytes[16];
        uint32_t words[4];
    } address;
};
#define s6_addr address.bytes

#ifdef __cplusplus
extern "C" {
#endif
uint16_t htons(uint16_t);
uint16_t ntohs(uint16_t);
uint32_t htonl(uint32_t);
uint32_t ntohl(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
