#ifndef CHOS_NET6_H
#define CHOS_NET6_H
#include "chos/net6_api.h"
#include "chos/net_tcp6_api.h"
#include "chos/net_dns6_api.h"
#include <stddef.h>
void chos_net6_init(const uint8_t mac[6]);
void chos_net6_poll(int enabled,int link_up);
void chos_net6_receive(const void *,size_t);
void chos_net6_get_info(chos_net6_info_t *);
int chos_net6_ping(const char *);
int chos_net6_format(const uint8_t[16],char *,size_t);
/* Owner 0 is reserved for privileged diagnostics, never an EL0 caller. All
 * operations are nonblocking. TCP6 connect requires hardware-backed entropy. */
int chos_tcp6_open(uint32_t owner,uint16_t local_port);
int chos_tcp6_connect(uint32_t owner,int handle,const uint8_t remote[16],uint16_t port);
int chos_tcp6_info(uint32_t owner,int handle,chos_tcp6_socket_info_t *out);
int chos_tcp6_send(uint32_t owner,int handle,const void *data,size_t length);
int chos_tcp6_recv(uint32_t owner,int handle,void *data,size_t capacity);
int chos_tcp6_close(uint32_t owner,int handle);
int chos_tcp6_abort(uint32_t owner,int handle);
void chos_net6_drop_owner(uint32_t owner);
int chos_net6_dns_start(uint32_t owner,const char *hostname);
int chos_net6_dns_info(uint32_t owner,chos_dns6_info_t *out);
#endif
