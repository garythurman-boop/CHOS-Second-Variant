#ifndef CHOS_NET6_H
#define CHOS_NET6_H
#include "chos/net6_api.h"
#include <stddef.h>
void chos_net6_init(const uint8_t mac[6]);
void chos_net6_poll(int enabled,int link_up);
void chos_net6_receive(const void *,size_t);
void chos_net6_get_info(chos_net6_info_t *);
int chos_net6_ping(const char *);
int chos_net6_format(const uint8_t[16],char *,size_t);
#endif
