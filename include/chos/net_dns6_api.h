#ifndef CHOS_NET_DNS6_API_H
#define CHOS_NET_DNS6_API_H
#include <stdint.h>
#define CHOS_DNS6_VERSION 1u
#define CHOS_DNS6_MAX_OWNERS 4u
#define CHOS_DNS6_HOST_MAX 256u
#define CHOS_DNS6_IDLE 0u
#define CHOS_DNS6_PENDING 1u
#define CHOS_DNS6_RESOLVED 2u
#define CHOS_DNS6_FAILED 3u
#define CHOS_DNS6_ERROR_NONE 0u
#define CHOS_DNS6_ERROR_NETWORK 1u
#define CHOS_DNS6_ERROR_NO_SERVER 2u
#define CHOS_DNS6_ERROR_ENTROPY 3u
#define CHOS_DNS6_ERROR_TIMEOUT 4u
#define CHOS_DNS6_ERROR_RESPONSE 5u
/* RDNSS learned from validated RA, never a fabricated or public fallback DNS.
 * Results belong to the caller and are not a privileged configuration interface. */
typedef struct {
 uint32_t version,size_bytes,state,error;
 char hostname[CHOS_DNS6_HOST_MAX];
 uint8_t address[16],server[16];
 uint32_t ttl_seconds,retries,rcode,reserved;
} chos_dns6_info_t;
_Static_assert(sizeof(chos_dns6_info_t)==320u,"DNS6 status ABI");
#endif
