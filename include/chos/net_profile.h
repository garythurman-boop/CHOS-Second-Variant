#ifndef CHOS_NET_PROFILE_H
#define CHOS_NET_PROFILE_H
#include <stdint.h>
#include <stddef.h>
#define CHOS_NET_PROFILE_BYTES 64u
#define CHOS_NET_DHCP 0u
#define CHOS_NET_STATIC 1u
#define CHOS_NET_DISABLED 2u
typedef struct {
 uint32_t mode,ipv4,netmask,gateway,dns;
 uint8_t mac[6];
} chos_net_config_t;
int chos_net_ipv4_parse(const char *,uint32_t *);
void chos_net_ipv4_format(uint32_t,char out[16]);
const char *chos_net_config_error(const chos_net_config_t *);
int chos_net_profile_encode(const chos_net_config_t *,uint8_t out[CHOS_NET_PROFILE_BYTES]);
int chos_net_profile_decode(const uint8_t *,size_t,chos_net_config_t *);
#endif
