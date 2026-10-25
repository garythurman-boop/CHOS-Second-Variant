#include "chos/net_manager.h"
#include "chos/storage.h"
#include "chos/timer.h"
#define PROFILE_PATH "/CFG/NETWORK.CFG"
static chos_net_config_t current,previous;
static int initialized,trial;
static uint64_t trial_start;
static const char *message="Automatic Ethernet configuration.";
static int same_mac(const uint8_t *a,const uint8_t *b){for(unsigned i=0;i<6;i++)if(a[i]!=b[i])return 0;return 1;}
static void set_mac(chos_net_config_t *c){chos_net_device_info_t d;chos_net_device_get_info(&d);for(unsigned i=0;i<6;i++)c->mac[i]=d.mac[i];}
void chos_net_manager_init(void){
 if(initialized)return;
 initialized=1;set_mac(&current);
 uint8_t data[64];size_t bytes=0;chos_net_config_t saved;
 chos_chfs_entry_t entry;
 if(storage_lookup(PROFILE_PATH,&entry)){
  if(entry.size_bytes!=sizeof(data) || !storage_read_file(PROFILE_PATH,data,sizeof(data),&bytes)){message="Saved network profile size/read invalid; DHCP retained.";previous=current;return;}
  if(!chos_net_profile_decode(data,bytes,&saved))message="Saved network profile invalid; automatic DHCP retained.";
  else if(!same_mac(saved.mac,current.mac))message="Saved adapter MAC differs; automatic DHCP retained.";
  else if(chos_net_stack_configure(&saved)){current=saved;message="Saved network profile loaded.";}
 }
 previous=current;
}
int chos_net_manager_apply(const chos_net_config_t *c){
 const char *invalid=chos_net_config_error(c);
 if(invalid){message=invalid;return 0;}
 if(!chos_net_device_ready()){message="No operational Ethernet adapter; inspect Diagnostics.";return 0;}
 if(!timer_is_ready()||!timer_frequency_hz()){message="Configuration trial requires a working timer.";return 0;}
 if(!trial)previous=current;
 if(!chos_net_stack_configure(c)){message="Network service rejected configuration.";return 0;}
 current=*c;set_mac(&current);trial=1;trial_start=timer_counter();message="Trial active: Keep or Revert; automatic rollback in 30 seconds.";return 1;
}
int chos_net_manager_keep(int save){
 if(!trial){message="No pending configuration trial.";return 0;}
 if(save){
  uint8_t encoded[64],readback[64];size_t n;
  (void)storage_make_directory("/CFG");
  if(!chos_net_profile_encode(&current,encoded)||!storage_write_file(PROFILE_PATH,encoded,sizeof(encoded))||!storage_sync()||
     !storage_read_file(PROFILE_PATH,readback,sizeof(readback),&n)||n!=sizeof(encoded)){
   message="Profile save failed; trial remains reversible.";return 0;
  }
  for(unsigned i=0;i<64;i++)if(encoded[i]!=readback[i]){message="Profile readback failed; trial remains reversible.";return 0;}
 }
 trial=0;previous=current;message=save?"Network profile saved and verified.":"Network configuration kept for this session.";return 1;
}
void chos_net_manager_revert(void){
 if(trial){(void)chos_net_stack_configure(&previous);current=previous;trial=0;message="Previous network configuration restored.";}
 else message="No pending configuration trial.";
}
void chos_net_manager_poll(void){if(trial){uint64_t f=timer_frequency_hz();if(f && (timer_counter()-trial_start)/f>=30u)chos_net_manager_revert();}}
void chos_net_manager_get_info(chos_net_manager_info_t *out){
 if(!out)return;
 chos_net_device_info_t d;chos_net_device_get_info(&d);
 *out=(chos_net_manager_info_t){.configured=current,.adapter_ready=d.ready,.link_known=d.link_status_known,.link_up=d.link_up,.trial=trial,
  .adapter=chos_net_device_name(),.driver=chos_net_device_error(),.message=message};
 chos_net_stack_get_info(&out->ipv4);
 if(trial){uint64_t f=timer_frequency_hz(),elapsed=f?(timer_counter()-trial_start)/f:30;out->trial_seconds=elapsed<30?(uint32_t)(30-elapsed):0;}
}

void chos_net_manager_status(chos_net_status_t *out){
 if(!out)return;
 chos_net_manager_info_t i;chos_net_manager_get_info(&i);
 chos_net_device_info_t d;chos_net_device_get_info(&d);
 *out=(chos_net_status_t){.version=CHOS_NET_STATUS_VERSION,.size_bytes=sizeof(*out),
  .state=i.ipv4.dhcp_state,.mode=i.configured.mode,.ipv4=i.ipv4.ipv4_address,.netmask=i.ipv4.subnet_mask,
  .gateway=i.ipv4.gateway_address,.dns=i.ipv4.dns_address,
  .capabilities=CHOS_NET_SUPPORT_IPV4|CHOS_NET_SUPPORT_DHCP4|CHOS_NET_SUPPORT_STATIC4,.rx_frames=d.rx_frames,.tx_frames=d.tx_frames};
 if(d.ready)out->flags|=CHOS_NET_STATUS_ADAPTER;
 if(d.link_status_known)out->flags|=CHOS_NET_STATUS_LINK_KNOWN;
 if(d.link_up)out->flags|=CHOS_NET_STATUS_LINK_UP;
 if(i.ipv4.ipv4_address)out->flags|=CHOS_NET_STATUS_CONFIGURED;
 if(i.configured.mode!=CHOS_NET_DISABLED)out->flags|=CHOS_NET_STATUS_ADMIN_ENABLED;
 for(unsigned n=0;n<6;n++)out->mac[n]=d.mac[n];
}
