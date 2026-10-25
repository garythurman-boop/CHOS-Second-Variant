/* CHOS IPv6 control plane and bounded outbound streams over a pinned, unmodified lwIP core. IPv4 is separate. */
#include "chos/net6.h"
#include "chos/net_device.h"
#include "chos/spinlock.h"
#include "chos/timer.h"
#include "chos/entropy.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "chos/net_services.h"
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

/* Independent bounded IPv6 stream service. lwIP pointers stay behind this lock;
 * callbacks copy into OS-owned queues and never call user or GUI code. */
static void dns6_drop_owner_locked(uint32_t owner);
typedef struct {
 struct tcp_pcb *pcb;
 uint32_t owner,start_ms,close_ms;
 int allocated,closing;
 uint16_t head,count;
 uint8_t receive[CHOS_TCP6_RX_CAPACITY];
 chos_tcp6_socket_info_t info;
} tcp6_slot_t;
static tcp6_slot_t tcp6_slots[CHOS_TCP6_MAX_SOCKETS];
static uint32_t next_tcp_isn;
uint32_t chos_lwip_tcp_isn(void){return next_tcp_isn;}
static tcp6_slot_t *tcp6_find(uint32_t owner,int handle){
 if(handle<1 || handle>(int)CHOS_TCP6_MAX_SOCKETS)return 0;
 tcp6_slot_t *s=&tcp6_slots[handle-1];return s->allocated && s->owner==owner?s:0;
}
static void tcp6_state(tcp6_slot_t *s){
 if(!s->pcb)return;
 switch(s->pcb->state){
 case CLOSED:s->info.state=CHOS_TCP_CLOSED;break;
 case SYN_SENT:s->info.state=CHOS_TCP_SYN_SENT;break;
 case ESTABLISHED:s->info.state=CHOS_TCP_ESTABLISHED;break;
 case FIN_WAIT_1:s->info.state=CHOS_TCP_FIN_WAIT_1;break;
 case FIN_WAIT_2:s->info.state=CHOS_TCP_FIN_WAIT_2;break;
 case CLOSE_WAIT:s->info.state=CHOS_TCP_CLOSE_WAIT;break;
 case LAST_ACK:s->info.state=CHOS_TCP_LAST_ACK;break;
 case TIME_WAIT:s->info.state=CHOS_TCP_TIME_WAIT;break;
 default:s->info.state=CHOS_TCP_FAILED;break;
 }
 memcpy(s->info.local_address,ip_2_ip6(&s->pcb->local_ip)->addr,16);
 memcpy(s->info.remote_address,ip_2_ip6(&s->pcb->remote_ip)->addr,16);
 s->info.local_port=s->pcb->local_port;s->info.remote_port=s->pcb->remote_port;
 s->info.send_queued=TCP_SND_BUF-tcp_sndbuf(s->pcb);
}
static void tcp6_kill(tcp6_slot_t *s,uint32_t state,uint32_t error){
 if(s->pcb){
  struct tcp_pcb *p=s->pcb;s->pcb=0;
  tcp_arg(p,0);tcp_recv(p,0);tcp_sent(p,0);tcp_err(p,0);tcp_abort(p);
 }
 s->info.state=state;s->info.error=error;s->info.send_queued=0;
}
static void tcp6_error(void *arg,err_t error){
 tcp6_slot_t *s=arg;if(!s)return;s->pcb=0;s->info.send_queued=0;
 s->info.state=error==ERR_CLSD?CHOS_TCP_CLOSED:CHOS_TCP_FAILED;
 s->info.error=error==ERR_CLSD?CHOS_TCP_ERROR_NONE:error==ERR_RST?CHOS_TCP_ERROR_RESET:CHOS_TCP_ERROR_TIMEOUT;
}
static err_t tcp6_received(void *arg,struct tcp_pcb *pcb,struct pbuf *p,err_t error){
 tcp6_slot_t *s=arg;
 if(!s || s->pcb!=pcb)return ERR_ARG;
 if(!p){s->info.receive_eof=1;return ERR_OK;}
 if(error!=ERR_OK)return error;
 if(p->tot_len>CHOS_TCP6_RX_CAPACITY-s->count)return ERR_MEM;
 for(struct pbuf *q=p;q;q=q->next){
  const uint8_t *bytes=q->payload;
  for(unsigned i=0;i<q->len;i++)s->receive[(s->head+s->count++)%CHOS_TCP6_RX_CAPACITY]=bytes[i];
 }
 s->info.bytes_received+=p->tot_len;pbuf_free(p);return ERR_OK;
}
static err_t tcp6_sent(void *arg,struct tcp_pcb *pcb,u16_t bytes){
 tcp6_slot_t *s=arg;if(s && s->pcb==pcb)s->info.bytes_sent+=bytes;return ERR_OK;
}
static err_t tcp6_connected(void *arg,struct tcp_pcb *pcb,err_t error){
 tcp6_slot_t *s=arg;if(!s || s->pcb!=pcb)return ERR_ARG;
 if(error!=ERR_OK)return error;tcp6_state(s);return ERR_OK;
}
/* Invoke after input and before timers: a PCB moved to TIME_WAIT has completed
 * the FIN exchange and is no longer application-usable. Reclaim it here before
 * lwIP's timer can free it without an application callback. */
static void tcp6_collect(void){
 for(unsigned i=0;i<CHOS_TCP6_MAX_SOCKETS;i++){
  tcp6_slot_t *s=&tcp6_slots[i];if(!s->allocated || !s->pcb)continue;
  tcp6_state(s);
  if(s->pcb->state==TIME_WAIT)tcp6_kill(s,CHOS_TCP_CLOSED,CHOS_TCP_ERROR_NONE);
  else if((s->pcb->state==SYN_SENT && (uint32_t)(sys_now()-s->start_ms)>=5000u) ||
          (s->closing && (uint32_t)(sys_now()-s->close_ms)>=5000u))
   tcp6_kill(s,CHOS_TCP_FAILED,CHOS_TCP_ERROR_TIMEOUT);
 }
}
int chos_tcp6_open(uint32_t owner,uint16_t local_port){
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);int handle=-1;
 if(initialized && active)for(unsigned i=0;i<CHOS_TCP6_MAX_SOCKETS;i++)if(!tcp6_slots[i].allocated){
  struct tcp_pcb *p=tcp_new_ip_type(IPADDR_TYPE_V6);if(!p)break;
  if(tcp_bind(p,IP6_ADDR_ANY,local_port)!=ERR_OK){tcp_abort(p);break;}
  tcp_bind_netif(p,&interface);tcp_nagle_disable(p);
  tcp6_slot_t *s=&tcp6_slots[i];memset(s,0,sizeof(*s));s->pcb=p;s->allocated=1;s->owner=owner;
  s->info.version=CHOS_TCP6_API_VERSION;s->info.size_bytes=sizeof(s->info);s->info.handle=(int32_t)i+1;
  tcp_arg(p,s);tcp_recv(p,tcp6_received);tcp_sent(p,tcp6_sent);tcp_err(p,tcp6_error);
  tcp6_state(s);handle=(int)i+1;break;
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);return handle;
}
int chos_tcp6_connect(uint32_t owner,int handle,const uint8_t remote[16],uint16_t port){
 if(!remote || !port)return 0;
 ip_addr_t address;ip_addr_set_zero_ip6(&address);memcpy(ip_2_ip6(&address)->addr,remote,16);
 if(ip6_addr_isany(ip_2_ip6(&address)) || ip6_addr_ismulticast(ip_2_ip6(&address)) ||
    ip6_addr_isipv4mappedipv6(ip_2_ip6(&address)))return 0;
 /* No MAC/counter/timer fallback for off-path sequence guessing. Reading may
  * fail without side effects; no socket state is changed until all 4 bytes exist. */
 uint32_t random;if(chos_entropy_read(&random,sizeof(random))!=sizeof(random))return 0;
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);tcp6_slot_t *s=tcp6_find(owner,handle);int ok=0;
 if(s && s->pcb && s->pcb->state==CLOSED && active){
  next_tcp_isn=random;ip6_addr_assign_zone(ip_2_ip6(&address),IP6_UNICAST,&interface);
  s->start_ms=sys_now();ok=tcp_connect(s->pcb,&address,port,tcp6_connected)==ERR_OK;tcp6_state(s);
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);return ok;
}
int chos_tcp6_info(uint32_t owner,int handle,chos_tcp6_socket_info_t *out){
 if(!out)return 0;uint64_t irq=chos_spin_lock_irqsave(&net6_lock);tcp6_slot_t *s=tcp6_find(owner,handle);
 if(s){tcp6_state(s);s->info.receive_available=s->count;*out=s->info;}
 chos_spin_unlock_irqrestore(&net6_lock,irq);return s!=0;
}
int chos_tcp6_send(uint32_t owner,int handle,const void *data,size_t length){
 if((!data && length) || length>CHOS_TCP6_MAX_TRANSFER)return -1;
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);tcp6_slot_t *s=tcp6_find(owner,handle);int amount=-1;
 if(s){amount=0;if(!length)amount=0;
  else if(s->pcb && !s->closing && (s->pcb->state==ESTABLISHED || s->pcb->state==CLOSE_WAIT)){
   size_t n=length;if(n>tcp_sndbuf(s->pcb))n=tcp_sndbuf(s->pcb);
   if(n && tcp_write(s->pcb,data,(u16_t)n,TCP_WRITE_FLAG_COPY)==ERR_OK){amount=(int)n;(void)tcp_output(s->pcb);}
  }
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);return amount;
}
int chos_tcp6_recv(uint32_t owner,int handle,void *data,size_t capacity){
 if((!data && capacity) || capacity>CHOS_TCP6_MAX_TRANSFER)return -1;
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);tcp6_slot_t *s=tcp6_find(owner,handle);int amount=-1;
 if(s){
  size_t n=capacity;if(n>s->count)n=s->count;
  uint8_t *out=data;for(size_t i=0;i<n;i++)out[i]=s->receive[(s->head+i)%CHOS_TCP6_RX_CAPACITY];
  s->head=(uint16_t)((s->head+n)%CHOS_TCP6_RX_CAPACITY);s->count-=(uint16_t)n;amount=(int)n;
  if(n && s->pcb)tcp_recved(s->pcb,(u16_t)n);
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);return amount;
}
int chos_tcp6_close(uint32_t owner,int handle){
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);tcp6_slot_t *s=tcp6_find(owner,handle);int result=-1;
 if(s){
  result=1;
  if(s->pcb && (s->pcb->state==CLOSED || s->pcb->state==SYN_SENT))tcp6_kill(s,CHOS_TCP_CLOSED,CHOS_TCP_ERROR_NONE);
  else if(s->pcb && !s->closing){
   result=tcp_shutdown(s->pcb,0,1)==ERR_OK;
   if(result){s->closing=1;s->close_ms=sys_now();tcp6_state(s);}
  }
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);return result;
}
int chos_tcp6_abort(uint32_t owner,int handle){
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);tcp6_slot_t *s=tcp6_find(owner,handle);
 if(s){tcp6_kill(s,CHOS_TCP_CLOSED,CHOS_TCP_ERROR_NONE);memset(s,0,sizeof(*s));}
 chos_spin_unlock_irqrestore(&net6_lock,irq);return s?1:-1;
}
void chos_net6_drop_owner(uint32_t owner){
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);
 dns6_drop_owner_locked(owner);
 for(unsigned i=0;i<CHOS_TCP6_MAX_SOCKETS;i++)if(tcp6_slots[i].allocated && tcp6_slots[i].owner==owner){
  tcp6_kill(&tcp6_slots[i],CHOS_TCP_CLOSED,CHOS_TCP_ERROR_NONE);memset(&tcp6_slots[i],0,sizeof(tcp6_slots[i]));
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);
}


/* IPv6 DNS resolver: one bounded query per owner and four total. RDNSS state
 * has a finite lifetime and is cleared on link/admin loss. No trusted-server
 * assertion is made: ordinary DNS remains unauthenticated, as on IPv4. */
typedef struct {
 struct udp_pcb *pcb;
 uint32_t owner,start_ms,last_send_ms;
 uint16_t query_id,query_bytes;
 uint8_t query[288];
 int allocated;
 chos_dns6_info_t info;
} dns6_slot_t;
static dns6_slot_t dns6_slots[CHOS_DNS6_MAX_OWNERS];
static ip_addr_t dns6_server;
static uint32_t dns6_server_received,dns6_server_lifetime;
static uint8_t dns6_response[512],dns6_ra[1500];
static unsigned net6_be16(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static uint32_t net6_be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void dns6_finish(dns6_slot_t *s,uint32_t state,uint32_t error){
 if(s->pcb){udp_remove(s->pcb);s->pcb=0;}s->info.state=state;s->info.error=error;
}
static int dns6_server_valid(void){
 return dns6_server_lifetime && (uint32_t)(sys_now()-dns6_server_received)<dns6_server_lifetime;
}
static void dns6_observe_ra(struct pbuf *p,uint16_t h,const ip_addr_t *from){
 if(!active || !p || !IP_IS_V6(from) || !ip6_addr_islinklocal(ip_2_ip6(from)) ||
    IP6H_HOPLIM(ip6_current_header())!=255 || h>p->len || p->tot_len<h+16u || (size_t)(p->tot_len-h)>sizeof(dns6_ra))return;
 struct pbuf view=*p;view.payload=(uint8_t *)p->payload+h;view.len-=h;view.tot_len-=h;
 if(ip6_chksum_pseudo(&view,IP6_NEXTH_ICMP6,view.tot_len,ip6_current_src_addr(),ip6_current_dest_addr()) ||
    pbuf_copy_partial(p,dns6_ra,view.tot_len,h)!=view.tot_len || dns6_ra[0]!=134 || dns6_ra[1])return;
 ip_addr_t selected;ip_addr_set_zero_ip6(&selected);uint32_t lifetime=0;int seen=0,remove_existing=0;
 /* Validate every option before accepting any server from this RA. */
 for(unsigned at=16;at<view.tot_len;){
  if(view.tot_len-at<2u)return;unsigned n=(unsigned)dns6_ra[at+1]*8u;
  if(!n || n>view.tot_len-at)return;
  if(dns6_ra[at]==25){
   if(n<24u || ((n-8u)%16u))return;
   uint32_t seconds=net6_be32(dns6_ra+at+4);
   for(unsigned j=8;j<n;j+=16){
    ip_addr_t a;ip_addr_set_zero_ip6(&a);memcpy(ip_2_ip6(&a)->addr,dns6_ra+at+j,16);
    if(ip6_addr_isany(ip_2_ip6(&a)) || ip6_addr_ismulticast(ip_2_ip6(&a)) || ip6_addr_isipv4mappedipv6(ip_2_ip6(&a)))continue;
    if(!seconds){
     if(ip6_addr_cmp_zoneless(ip_2_ip6(&a),ip_2_ip6(&dns6_server)))remove_existing=1;
     if(seen && ip6_addr_cmp_zoneless(ip_2_ip6(&a),ip_2_ip6(&selected)))seen=0;
    }else if(!seen){selected=a;lifetime=seconds>86400u?86400000u:seconds*1000u;seen=1;}
   }
  }
  at+=n;
 }
 if(remove_existing)dns6_server_lifetime=0;
 if(seen){
  ip6_addr_assign_zone(ip_2_ip6(&selected),IP6_UNICAST,&interface);
  dns6_server=selected;dns6_server_received=sys_now();dns6_server_lifetime=lifetime;
 }
}
static int dns6_emit(dns6_slot_t *s){
 struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,s->query_bytes,PBUF_RAM);if(!p)return 0;
 err_t e=pbuf_take(p,s->query,s->query_bytes);if(e==ERR_OK)e=udp_send(s->pcb,p);pbuf_free(p);
 if(e==ERR_OK)s->last_send_ms=sys_now();return e==ERR_OK;
}
static void dns6_received(void *arg,struct udp_pcb *pcb,struct pbuf *p,const ip_addr_t *from,u16_t port){
 dns6_slot_t *s=arg;
 if(s && s->pcb==pcb && s->info.state==CHOS_DNS6_PENDING && p && p->tot_len<=sizeof(dns6_response) &&
    port==53 && IP_IS_V6(from) && !memcmp(ip_2_ip6(from)->addr,s->info.server,16) &&
    pbuf_copy_partial(p,dns6_response,p->tot_len,0)==p->tot_len){
  uint32_t ttl;uint8_t address[16];int result=chos_net_parse_dns_aaaa_response(dns6_response,p->tot_len,s->query_id,s->info.hostname,address,&ttl);
  if(result==1){memcpy(s->info.address,address,16);s->info.ttl_seconds=ttl;dns6_finish(s,CHOS_DNS6_RESOLVED,CHOS_DNS6_ERROR_NONE);}
  else if(result<0){s->info.rcode=(uint32_t)-result;dns6_finish(s,CHOS_DNS6_FAILED,CHOS_DNS6_ERROR_RESPONSE);}
 }
 if(p)pbuf_free(p);
}
static void dns6_poll(void){
 if(!dns6_server_valid())dns6_server_lifetime=0;
 for(unsigned i=0;i<CHOS_DNS6_MAX_OWNERS;i++){
  dns6_slot_t *s=&dns6_slots[i];if(!s->allocated || s->info.state!=CHOS_DNS6_PENDING)continue;
  if(!active || !dns6_server_valid() || memcmp(s->info.server,ip_2_ip6(&dns6_server)->addr,16))
   dns6_finish(s,CHOS_DNS6_FAILED,CHOS_DNS6_ERROR_NETWORK);
  else if((uint32_t)(sys_now()-s->start_ms)>=7000u)dns6_finish(s,CHOS_DNS6_FAILED,CHOS_DNS6_ERROR_TIMEOUT);
  else if(s->info.retries<3u && (uint32_t)(sys_now()-s->last_send_ms)>=2000u){
   ++s->info.retries;(void)dns6_emit(s);s->last_send_ms=sys_now();
  }
 }
}
int chos_net6_dns_start(uint32_t owner,const char *host){
 if(!host)return 0;uint8_t query[288];
 size_t bytes=chos_net_build_dns_aaaa_query(query,sizeof(query),0,host);if(!bytes)return 0;
 uint8_t random[4];int has_entropy=chos_entropy_read(random,sizeof(random))==sizeof(random);
 uint64_t irq=chos_spin_lock_irqsave(&net6_lock);dns6_slot_t *s=0;
 for(unsigned i=0;i<CHOS_DNS6_MAX_OWNERS;i++)if(dns6_slots[i].allocated && dns6_slots[i].owner==owner){s=&dns6_slots[i];break;}
 if(s && s->info.state==CHOS_DNS6_PENDING){chos_spin_unlock_irqrestore(&net6_lock,irq);return 0;}
 if(!s)for(unsigned i=0;i<CHOS_DNS6_MAX_OWNERS;i++)if(!dns6_slots[i].allocated){s=&dns6_slots[i];break;}
 if(!s){chos_spin_unlock_irqrestore(&net6_lock,irq);return 0;}
 memset(s,0,sizeof(*s));s->allocated=1;s->owner=owner;s->info.version=CHOS_DNS6_VERSION;s->info.size_bytes=sizeof(s->info);
 unsigned n=0;while(host[n] && n<CHOS_DNS6_HOST_MAX-1){s->info.hostname[n]=host[n];n++;}
 if(n && s->info.hostname[n-1]=='.')s->info.hostname[n-1]=0;
 int ok=0;
 if(!active)dns6_finish(s,CHOS_DNS6_FAILED,CHOS_DNS6_ERROR_NETWORK);
 else if(!dns6_server_valid())dns6_finish(s,CHOS_DNS6_FAILED,CHOS_DNS6_ERROR_NO_SERVER);
 else if(!has_entropy)dns6_finish(s,CHOS_DNS6_FAILED,CHOS_DNS6_ERROR_ENTROPY);
 else {
  s->pcb=udp_new_ip_type(IPADDR_TYPE_V6);
  if(s->pcb){
   uint16_t port=(uint16_t)(49152u+(net6_be16(random+2)&16383u));
   if(udp_bind(s->pcb,IP6_ADDR_ANY,port)==ERR_OK && udp_connect(s->pcb,&dns6_server,53)==ERR_OK){
    udp_bind_netif(s->pcb,&interface);udp_recv(s->pcb,dns6_received,s);
    s->query_id=(uint16_t)net6_be16(random);query[0]=random[0];query[1]=random[1];
    memcpy(s->query,query,bytes);s->query_bytes=(uint16_t)bytes;memcpy(s->info.server,ip_2_ip6(&dns6_server)->addr,16);
    s->info.state=CHOS_DNS6_PENDING;s->start_ms=s->last_send_ms=sys_now();ok=dns6_emit(s);
   }
  }
  if(!ok)dns6_finish(s,CHOS_DNS6_FAILED,CHOS_DNS6_ERROR_NETWORK);
 }
 chos_spin_unlock_irqrestore(&net6_lock,irq);return ok;
}
int chos_net6_dns_info(uint32_t owner,chos_dns6_info_t *out){
 if(!out)return 0;uint64_t irq=chos_spin_lock_irqsave(&net6_lock);int found=0;
 for(unsigned i=0;i<CHOS_DNS6_MAX_OWNERS;i++)if(dns6_slots[i].allocated && dns6_slots[i].owner==owner){*out=dns6_slots[i].info;found=1;break;}
 if(!found){memset(out,0,sizeof(*out));out->version=CHOS_DNS6_VERSION;out->size_bytes=sizeof(*out);}
 chos_spin_unlock_irqrestore(&net6_lock,irq);return 1;
}

static void dns6_drop_owner_locked(uint32_t owner){
 for(unsigned i=0;i<CHOS_DNS6_MAX_OWNERS;i++)if(dns6_slots[i].allocated && dns6_slots[i].owner==owner){
  dns6_finish(&dns6_slots[i],CHOS_DNS6_IDLE,CHOS_DNS6_ERROR_NONE);memset(&dns6_slots[i],0,sizeof(dns6_slots[i]));
 }
}

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
 dns6_observe_ra(p,h,from);
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
   active=0;dns6_server_lifetime=0;dns6_poll();
   for(unsigned i=0;i<CHOS_TCP6_MAX_SOCKETS;i++)if(tcp6_slots[i].allocated)tcp6_kill(&tcp6_slots[i],CHOS_TCP_FAILED,CHOS_TCP_ERROR_NETWORK);
   netif_set_link_down(&interface);netif_set_down(&interface);
   for(unsigned i=0;i<LWIP_IPV6_NUM_ADDRESSES;i++)netif_ip6_addr_set_state(&interface,i,IP6_ADDR_INVALID);
   if(status.ping_state==CHOS_NET6_PING_PENDING)status.ping_state=CHOS_NET6_PING_FAILED;
  }
  tcp6_collect();dns6_poll();sys_check_timeouts();tcp6_collect();
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
 tcp6_collect();
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
