/* CHOS IPv6 control plane over a pinned, unmodified lwIP core. IPv4 is separate. */
#include "chos/net6.h"
#include "chos/net_device.h"
#include "chos/spinlock.h"
#include "chos/timer.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ethip6.h"
#include "netif/ethernet.h"
#include "lwip/timeouts.h"
#include "lwip/raw.h"
#include "lwip/ip6.h"
#include "lwip/inet_chksum.h"
#include "lwip/priv/nd6_priv.h"
#include <string.h>
static struct netif interface;
static struct raw_pcb *echo;
static int initialized,active;
static chos_spinlock_t net6_lock=CHOS_SPINLOCK_INITIALIZER;
static chos_net6_info_t status;
static uint8_t transmit[CHOS_NET_MAX_ETHERNET_FRAME];
static ip_addr_t ping_peer;
static uint16_t ping_sequence;
static uint32_t ping_start;
uint32_t sys_now(void) {
 uint64_t f=timer_frequency_hz(),t=timer_counter();
 return f?(uint32_t)((t/f)*1000u+((t%f)*1000u)/f):0;
}
/* Protocol timing jitter only: never exposed as cryptographic entropy. */
unsigned chos_lwip_protocol_jitter(void){return (unsigned)(timer_counter()^(timer_counter()>>32));}
void chos_lwip_assert(const char *message,unsigned line){(void)message;(void)line;__builtin_trap();}
size_t chos_lwip_strlen(const char *s){size_t n=0;while(s[n])++n;return n;}
int chos_lwip_strncmp(const char *a,const char *b,size_t n){for(size_t i=0;i<n;i++){unsigned char x=a[i],y=b[i];if(x!=y)return x<y?-1:1;if(!x)return 0;}return 0;}
int chos_lwip_atoi(const char *s){int n=0;unsigned i=0;while(s[i]>='0'&&s[i]<='9'&&i<3){n=n*10+s[i++]-'0';}return n;}
static err_t output(struct netif *n,struct pbuf *p){
 (void)n;
 if(!active || !p || p->tot_len>sizeof(transmit) || p->tot_len<14u)return ERR_IF;
 if(pbuf_copy_partial(p,transmit,p->tot_len,0)!=p->tot_len)return ERR_BUF;
 if(chos_net_device_send(transmit,p->tot_len)!=1){++status.dropped_frames;return ERR_IF;}
 ++status.tx_frames;return ERR_OK;
}
static err_t setup(struct netif *n){
 n->name[0]='c';n->name[1]='6';n->hwaddr_len=6;n->mtu=1500;
 n->output_ip6=ethip6_output;n->linkoutput=output;
 n->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHERNET|NETIF_FLAG_MLD6;
 return ERR_OK;
}
static u8_t received_echo(void *arg,struct raw_pcb *pcb,struct pbuf *p,const ip_addr_t *from){
 (void)arg;(void)pcb;
 const uint16_t h=ip_current_header_tot_len();uint8_t bytes[16];
 if(status.ping_state!=CHOS_NET6_PING_PENDING || !IP_IS_V6(from) ||
    !ip6_addr_cmp_zoneless(ip_2_ip6(from),ip_2_ip6(&ping_peer)) || p->tot_len!=h+sizeof(bytes) ||
    pbuf_copy_partial(p,bytes,sizeof(bytes),h)!=sizeof(bytes))return 0;
 if(bytes[0]!=129 || bytes[1] || bytes[4]!=0x43 || bytes[5]!=0x48 ||
    bytes[6]!=(uint8_t)(ping_sequence>>8) || bytes[7]!=(uint8_t)ping_sequence)return 0;
 struct pbuf check={0};check.payload=bytes;check.len=check.tot_len=sizeof(bytes);
 if(ip6_chksum_pseudo(&check,IP6_NEXTH_ICMP6,sizeof(bytes),ip6_current_src_addr(),ip6_current_dest_addr()))return 0;
 for(unsigned i=8;i<sizeof(bytes);i++)if(bytes[i]!=(uint8_t)(i^0x53u))return 0;
 status.ping_state=CHOS_NET6_PING_REPLIED;status.ping_elapsed_ms=(uint32_t)(sys_now()-ping_start);
 return 0; /* lwIP still owns and validates the packet */
}
void chos_net6_init(const uint8_t mac[6]){
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);
 if(!initialized){
  lwip_init();status.version=CHOS_NET6_VERSION;status.size_bytes=sizeof(status);
  if(netif_add_noaddr(&interface,0,setup,ethernet_input)){
   memcpy(interface.hwaddr,mac,6);netif_set_default(&interface);
   interface.ip6_autoconfig_enabled=1;
   echo=raw_new_ip_type(IPADDR_TYPE_V6,IP6_NEXTH_ICMP6);
   if(echo){raw_bind_netif(echo,&interface);raw_recv(echo,received_echo,0);echo->chksum_reqd=1;echo->chksum_offset=2;}
   initialized=1;status.flags=CHOS_NET6_READY;
  }
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);
}
void chos_net6_poll(int enabled,int link_up){
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);
 if(initialized){
  int up=enabled&&link_up;
  status.flags=CHOS_NET6_READY|(enabled?CHOS_NET6_ADMIN_ENABLED:0)|(link_up?CHOS_NET6_LINK_UP:0);
  if(up&&!active){
   active=1;netif_create_ip6_linklocal_address(&interface,1);
   netif_set_up(&interface);netif_set_link_up(&interface);
  }else if(!up&&active){
   active=0;netif_set_link_down(&interface);netif_set_down(&interface);
   for(unsigned i=0;i<LWIP_IPV6_NUM_ADDRESSES;i++)netif_ip6_addr_set_state(&interface,i,IP6_ADDR_INVALID);
   if(status.ping_state==CHOS_NET6_PING_PENDING)status.ping_state=CHOS_NET6_PING_FAILED;
  }
  sys_check_timeouts();
  if(status.ping_state==CHOS_NET6_PING_PENDING && (uint32_t)(sys_now()-ping_start)>=5000u)
   status.ping_state=CHOS_NET6_PING_TIMED_OUT;
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);
}
void chos_net6_receive(const void *frame,size_t bytes){
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);
 const uint8_t *f=frame;
 if(!initialized || !active || !f || bytes<54 || bytes>CHOS_NET_MAX_ETHERNET_FRAME || f[12]!=0x86 || f[13]!=0xdd){++status.dropped_frames;}
 else {
  struct pbuf *p=pbuf_alloc(PBUF_RAW,(u16_t)bytes,PBUF_POOL);
  if(!p || pbuf_take(p,frame,(u16_t)bytes)!=ERR_OK){if(p)pbuf_free(p);++status.dropped_frames;}
  else {++status.rx_frames;if(ethernet_input(p,&interface)!=ERR_OK){pbuf_free(p);++status.dropped_frames;}}
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);
}
void chos_net6_get_info(chos_net6_info_t *out){
 if(!out)return;
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);*out=status;
 if(initialized){
  for(unsigned i=0;i<3;i++){
   uint8_t s=netif_ip6_addr_state(&interface,i);
   out->address_states[i]=ip6_addr_ispreferred(s)?CHOS_NET6_ADDRESS_PREFERRED:
    ip6_addr_isdeprecated(s)?CHOS_NET6_ADDRESS_DEPRECATED:
    ip6_addr_isduplicated(s)?CHOS_NET6_ADDRESS_DUPLICATED:
    ip6_addr_istentative(s)?CHOS_NET6_ADDRESS_TENTATIVE:CHOS_NET6_ADDRESS_INVALID;
   if(!ip6_addr_isinvalid(s))memcpy(out->addresses[i],netif_ip6_addr(&interface,i)->addr,16);
  }
  for(unsigned i=0;i<LWIP_ND6_NUM_ROUTERS;i++)if(default_router_list[i].neighbor_entry &&
     default_router_list[i].invalidation_timer && default_router_list[i].neighbor_entry->netif==&interface){
    memcpy(out->router,default_router_list[i].neighbor_entry->next_hop_address.addr,16);break;
  }
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);
}
int chos_net6_ping(const char *s){
 if(!s)return 0;size_t n=0;while(n<46&&s[n])n++;if(n==0||n>=46)return 0;
 ip6_addr_t address;if(!ip6addr_aton(s,&address)||ip6_addr_isany(&address)||ip6_addr_ismulticast(&address))return 0;
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);int ok=0;
 if(initialized && active && echo && status.ping_state!=CHOS_NET6_PING_PENDING){
  struct pbuf *p=pbuf_alloc(PBUF_IP,16,PBUF_RAM);
  if(p){
   uint8_t bytes[16]={128,0,0,0,0x43,0x48};++ping_sequence;
   bytes[6]=(uint8_t)(ping_sequence>>8);bytes[7]=(uint8_t)ping_sequence;
   for(unsigned i=8;i<16;i++)bytes[i]=(uint8_t)(i^0x53u);
   pbuf_take(p,bytes,sizeof(bytes));ip_addr_copy_from_ip6(ping_peer,address);
   status.ping_state=CHOS_NET6_PING_PENDING;ping_start=sys_now();status.ping_elapsed_ms=0;
   ok=raw_sendto(echo,p,&ping_peer)==ERR_OK;if(!ok)status.ping_state=CHOS_NET6_PING_FAILED;
   pbuf_free(p);
  }
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);return ok;
}
int chos_net6_format(const uint8_t bytes[16],char *out,size_t capacity){
 if(!bytes||!out||capacity<46||capacity>0x7fffffffu)return 0;
 ip6_addr_t a={0};memcpy(a.addr,bytes,16);return ip6addr_ntoa_r(&a,out,(int)capacity)!=0;
}
