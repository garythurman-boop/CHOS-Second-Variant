#ifndef CHOS_NET_MANAGER_H
#define CHOS_NET_MANAGER_H
#include "chos/net_profile.h"
#include "chos/net_status_api.h"
#include "chos/net_stack.h"
#include "chos/net_device.h"
typedef struct {
 chos_net_config_t configured;
 chos_net_stack_info_t ipv4;
 int adapter_ready,link_known,link_up,trial;
 uint32_t trial_seconds;
 const char *adapter,*driver,*message;
} chos_net_manager_info_t;
void chos_net_manager_init(void);
void chos_net_manager_poll(void);
void chos_net_manager_get_info(chos_net_manager_info_t *);
void chos_net_manager_status(chos_net_status_t *);
/* Kernel-trusted configuration boundary; no application setter syscall exists. */
int chos_net_manager_apply(const chos_net_config_t *);
int chos_net_manager_keep(int save);
void chos_net_manager_revert(void);
#endif
