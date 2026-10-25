#ifndef CHOS_LWIP_ARCH_CC_H
#define CHOS_LWIP_ARCH_CC_H
#include <stdint.h>
#include <stddef.h>
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_UNISTD_H 1
#define LWIP_NO_CTYPE_H 1
#define LWIP_PLATFORM_DIAG(x) ((void)0)
void chos_lwip_assert(const char *,unsigned);
#define LWIP_PLATFORM_ASSERT(x) chos_lwip_assert((x),__LINE__)
unsigned chos_lwip_protocol_jitter(void);
uint32_t chos_lwip_tcp_isn(void);
#define LWIP_RAND() chos_lwip_protocol_jitter()
#endif
