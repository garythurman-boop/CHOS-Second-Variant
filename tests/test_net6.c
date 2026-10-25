#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "chos/net6.h"
#include "chos/net_device.h"
#include "chos/spinlock.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip6_addr.h"
#include "lwip/pbuf.h"
extern int puts(const char *);
static uint64_t now;
static unsigned tx_count;
static uint8_t last_tx[1518];static size_t last_tx_bytes;
extern int entropy_available;
size_t chos_entropy_read(void *p,size_t n){if(!entropy_available)return 0;memset(p,0x53,n);return n;}
uint64_t timer_frequency_hz(void){return 1000;}
uint64_t timer_counter(void){return now;}
uint64_t chos_spin_lock_irqsave(chos_spinlock_t *p){assert(!p->held);p->held=1;return 0;}
void chos_spin_unlock_irqrestore(chos_spinlock_t *p,uint64_t irq){(void)irq;assert(p->held);p->held=0;}
int chos_net_device_send(const void *p,size_t n){assert(n<=sizeof(last_tx));memcpy(last_tx,p,n);last_tx_bytes=n;tx_count++;return 1;}
static void be16(uint8_t *p,unsigned n){p[0]=n>>8;p[1]=n;}
static void checksum(uint8_t *f){
 ip6_addr_t a={0},b={0};memcpy(a.addr,f+22,16);memcpy(b.addr,f+38,16);
 struct pbuf p={0};p.payload=f+54;p.len=p.tot_len=(f[18]<<8)|f[19];
 f[56]=f[57]=0;uint16_t c=ip6_chksum_pseudo(&p,58,p.tot_len,&a,&b);memcpy(f+56,&c,2);
}
int entropy_available=1;
static void be32(uint8_t *p,uint32_t n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static size_t tcp_frame(uint8_t *f,const uint8_t remote[16],const uint8_t local[16],uint16_t local_port,
                       uint32_t sequence,uint32_t ack,uint8_t flags,const void *payload,size_t bytes){
 memset(f,0,1518);const uint8_t mac[6]={2,0x12,0x34,0x56,0x78,0x9a};memcpy(f,mac,6);f[6]=2;f[11]=1;
 f[12]=0x86;f[13]=0xdd;f[14]=0x60;be16(f+18,(unsigned)bytes+20);f[20]=6;f[21]=64;
 memcpy(f+22,remote,16);memcpy(f+38,local,16);be16(f+54,80);be16(f+56,local_port);
 be32(f+58,sequence);be32(f+62,ack);f[66]=0x50;f[67]=flags;be16(f+68,2048);
 if(bytes)memcpy(f+74,payload,bytes);
 ip6_addr_t a={0},b={0};memcpy(a.addr,remote,16);memcpy(b.addr,local,16);
 struct pbuf p={0};p.payload=f+54;p.len=p.tot_len=(u16_t)(bytes+20);
 uint16_t c=ip6_chksum_pseudo(&p,6,p.tot_len,&a,&b);memcpy(f+70,&c,2);return bytes+74;
}
static void test_tcp6(const chos_net6_info_t *net){
 uint8_t remote[16]={0xfe,0x80};remote[15]=1;uint8_t frame[1518],payload[512];
 memset(payload,0x71,sizeof(payload));chos_tcp6_socket_info_t info;
 int h=chos_tcp6_open(42,0);assert(h>0&&chos_tcp6_info(42,h,&info));uint16_t port=info.local_port;
 assert(!chos_tcp6_info(43,h,&info)&&chos_tcp6_send(43,h,"x",1)==-1&&chos_tcp6_abort(43,h)==-1);
 assert(!chos_tcp6_connect(42,h,(uint8_t[16]){0},80));
 assert(!chos_tcp6_connect(42,h,(uint8_t[16]){0xff,2},80));
 entropy_available=0;unsigned tx=tx_count;assert(!chos_tcp6_connect(42,h,remote,80));assert(tx_count==tx);entropy_available=1;
 assert(chos_tcp6_connect(42,h,remote,80));assert(last_tx[20]==6 && (last_tx[67]&2));
 uint32_t seq=get32(last_tx+58);assert(seq+1u==0x53535353u);
 size_t n=tcp_frame(frame,remote,net->addresses[0],port,100,seq+2,0x12,0,0);
 chos_net6_receive(frame,n);assert(chos_tcp6_info(42,h,&info)&&info.state==CHOS_TCP_SYN_SENT);
 n=tcp_frame(frame,remote,net->addresses[0],port,100,seq+1,0x12,0,0);frame[70]^=1;
 chos_net6_receive(frame,n);assert(chos_tcp6_info(42,h,&info)&&info.state==CHOS_TCP_SYN_SENT);
 frame[70]^=1;chos_net6_receive(frame,n);assert(chos_tcp6_info(42,h,&info)&&info.state==CHOS_TCP_ESTABLISHED);
 assert(chos_tcp6_send(42,h,0,1)==-1&&chos_tcp6_send(42,h,payload,513)==-1);
 assert(chos_tcp6_send(42,h,"request",7)==7);assert(last_tx[20]==6&&memcmp(last_tx+74,"request",7)==0);
 for(unsigned i=0;i<4;i++){
  n=tcp_frame(frame,remote,net->addresses[0],port,101+i*512,seq+8,0x18,payload,512);chos_net6_receive(frame,n);
 }
 assert(chos_tcp6_info(42,h,&info)&&info.receive_available==2048&&info.bytes_sent==7);
 n=tcp_frame(frame,remote,net->addresses[0],port,2149,seq+8,0x18,payload,512);chos_net6_receive(frame,n);
 assert(chos_tcp6_info(42,h,&info)&&info.receive_available==2048);
 for(unsigned i=0;i<4;i++){uint8_t out[512];assert(chos_tcp6_recv(42,h,out,512)==512);assert(!memcmp(out,payload,512));}
 assert(chos_tcp6_info(42,h,&info)&&info.receive_available==0&&info.bytes_received==2048);
 assert(chos_tcp6_close(42,h)==1);assert(chos_tcp6_info(42,h,&info)&&info.state==CHOS_TCP_FIN_WAIT_1);
 n=tcp_frame(frame,remote,net->addresses[0],port,2149,seq+9,0x11,0,0);chos_net6_receive(frame,n);
 assert(chos_tcp6_info(42,h,&info)&&info.state==CHOS_TCP_CLOSED&&info.receive_eof);
 assert(chos_tcp6_abort(42,h)==1&&!chos_tcp6_info(42,h,&info));
 /* RST is confined to its owner; timeout, full table and process cleanup reclaim. */
 h=chos_tcp6_open(42,0);assert(h>0);assert(chos_tcp6_info(42,h,&info));port=info.local_port;
 assert(chos_tcp6_connect(42,h,remote,80));seq=get32(last_tx+58);
 n=tcp_frame(frame,remote,net->addresses[0],port,100,seq+1,0x14,0,0);chos_net6_receive(frame,n);
 assert(chos_tcp6_info(42,h,&info)&&info.state==CHOS_TCP_FAILED&&info.error==CHOS_TCP_ERROR_RESET);
 chos_net6_drop_owner(43);assert(chos_tcp6_info(42,h,&info));chos_net6_drop_owner(42);assert(!chos_tcp6_info(42,h,&info));
 h=chos_tcp6_open(42,0);assert(h>0&&chos_tcp6_connect(42,h,remote,80));
 for(unsigned i=0;i<6;i++){now+=1000;chos_net6_poll(1,1);}
 assert(chos_tcp6_info(42,h,&info)&&info.state==CHOS_TCP_FAILED&&info.error==CHOS_TCP_ERROR_TIMEOUT);
 chos_net6_drop_owner(42);
 for(unsigned i=0;i<CHOS_TCP6_MAX_SOCKETS;i++)assert(chos_tcp6_open(42,0)>0);
 assert(chos_tcp6_open(43,0)==-1);chos_net6_drop_owner(42);assert(chos_tcp6_open(43,0)>0);chos_net6_drop_owner(43);
 puts("IPv6 TCP: real checksummed handshake/data/window/FIN/RST, owner isolation, bounded queues, entropy refusal, timeout and owner cleanup passed");
}

static size_t ra(uint8_t f[128],unsigned hop){
 memset(f,0,128);f[0]=f[1]=0x33;f[5]=1;f[6]=2;f[11]=1;f[12]=0x86;f[13]=0xdd;
 f[14]=0x60;be16(f+18,56);f[20]=58;f[21]=hop;f[22]=0xfe;f[23]=0x80;f[37]=1;f[38]=0xff;f[39]=2;f[53]=1;
 uint8_t *p=f+54;p[0]=134;p[4]=64;be16(p+6,1800);p[16]=3;p[17]=4;p[18]=64;p[19]=0xc0;
 p[22]=0x0e;p[23]=0x10;p[26]=7;p[27]=8;p[32]=0x20;p[33]=1;p[34]=0x0d;p[35]=0xb8;p[37]=1;
 p[48]=1;p[49]=1;memcpy(p+50,f+6,6);checksum(f);return 110;
}
static unsigned test_be16(const uint8_t *p){return (unsigned)p[0]<<8|p[1];}
static size_t rdnss_ra(uint8_t *f,uint32_t seconds){
 size_t n=ra(f,255);be16(f+18,80);f[n]=25;f[n+1]=3;f[n+2]=f[n+3]=0;be32(f+n+4,seconds);
 memset(f+n+8,0,16);f[n+8]=0xfe;f[n+9]=0x80;f[n+23]=1;checksum(f);return n+24;
}
static size_t udp_dns_reply(uint8_t *f,const chos_net6_info_t *net,uint16_t port,const uint8_t *dns,size_t n){
 memset(f,0,1518);const uint8_t mac[6]={2,0x12,0x34,0x56,0x78,0x9a};memcpy(f,mac,6);f[6]=2;f[11]=1;
 f[12]=0x86;f[13]=0xdd;f[14]=0x60;be16(f+18,(unsigned)n+8);f[20]=17;f[21]=64;
 f[22]=0xfe;f[23]=0x80;f[37]=1;memcpy(f+38,net->addresses[0],16);
 be16(f+54,53);be16(f+56,port);be16(f+58,(unsigned)n+8);memcpy(f+62,dns,n);
 ip6_addr_t a={0},b={0};memcpy(a.addr,f+22,16);memcpy(b.addr,f+38,16);
 struct pbuf p={0};p.payload=f+54;p.len=p.tot_len=(u16_t)n+8;
 uint16_t c=ip6_chksum_pseudo(&p,17,p.tot_len,&a,&b);memcpy(f+60,&c,2);return n+62;
}
static void test_dns6(const chos_net6_info_t *net){
 uint8_t f[1518],dns[512],expected[16]={0x20,1,0x0d,0xb8};expected[15]=7;chos_dns6_info_t info;
 assert(!chos_net6_dns_start(42,"example.com"));assert(chos_net6_dns_info(42,&info)&&info.error==CHOS_DNS6_ERROR_NO_SERVER);
 size_t n=rdnss_ra(f,120);f[21]=254;checksum(f);chos_net6_receive(f,n);
 assert(!chos_net6_dns_start(42,"wrong-hop.example.com"));
 n=rdnss_ra(f,120);f[56]^=1;chos_net6_receive(f,n);assert(!chos_net6_dns_start(42,"bad-checksum.example.com"));
 n=rdnss_ra(f,120);f[111]=0;checksum(f);chos_net6_receive(f,n);assert(!chos_net6_dns_start(42,"bad-option.example.com"));
 n=rdnss_ra(f,120);chos_net6_receive(f,n);
 /* A withdrawal followed by an invalid option must not partially commit. */
 n=rdnss_ra(f,0);f[n]=99;f[n+1]=0;be16(f+18,82);checksum(f);chos_net6_receive(f,n+2);
 entropy_available=0;assert(!chos_net6_dns_start(42,"example.com"));assert(chos_net6_dns_info(42,&info)&&info.error==CHOS_DNS6_ERROR_ENTROPY);entropy_available=1;
 assert(chos_net6_dns_start(42,"example.com"));assert(last_tx[20]==17&&test_be16(last_tx+56)==53);
 assert(!chos_net6_dns_start(42,"second.example.com"));assert(chos_net6_dns_info(43,&info)&&info.state==CHOS_DNS6_IDLE);
 uint16_t port=(uint16_t)test_be16(last_tx+54);size_t q=test_be16(last_tx+58)-8;assert(q<sizeof(dns));memcpy(dns,last_tx+62,q);
 be16(dns+2,0x8180);be16(dns+6,1);size_t end=q;dns[end++]=0xc0;dns[end++]=12;be16(dns+end,28);be16(dns+end+2,1);be32(dns+end+4,300);be16(dns+end+8,16);end+=10;memcpy(dns+end,expected,16);end+=16;
 dns[0]^=1;n=udp_dns_reply(f,net,port,dns,end);chos_net6_receive(f,n);assert(chos_net6_dns_info(42,&info)&&info.state==CHOS_DNS6_PENDING);dns[0]^=1;
 n=udp_dns_reply(f,net,port,dns,end);f[60]^=1;chos_net6_receive(f,n);assert(chos_net6_dns_info(42,&info)&&info.state==CHOS_DNS6_PENDING);
 n=udp_dns_reply(f,net,port,dns,end);chos_net6_receive(f,n);
 assert(chos_net6_dns_info(42,&info)&&info.state==CHOS_DNS6_RESOLVED&&!memcmp(info.address,expected,16)&&info.ttl_seconds==300);
 assert(chos_net6_dns_start(42,"notfound.example.com"));q=test_be16(last_tx+58)-8;memcpy(dns,last_tx+62,q);be16(dns+2,0x8183);
 n=udp_dns_reply(f,net,port,dns,q);chos_net6_receive(f,n);assert(chos_net6_dns_info(42,&info)&&info.state==CHOS_DNS6_FAILED&&info.rcode==3);
 assert(chos_net6_dns_start(42,"timeout.example.com"));for(unsigned i=0;i<8;i++){now+=1000;chos_net6_poll(1,1);}
 assert(chos_net6_dns_info(42,&info)&&info.error==CHOS_DNS6_ERROR_TIMEOUT&&info.retries==3);
 /* Lifetimes and zero-lifetime withdrawal are honored, malformed options cannot
  * modify existing DNS state, and a changed/link-lost server cancels requests. */
 n=rdnss_ra(f,1);chos_net6_receive(f,n);now+=1001;chos_net6_poll(1,1);
 assert(!chos_net6_dns_start(42,"expired.example.com"));assert(chos_net6_dns_info(42,&info)&&info.error==CHOS_DNS6_ERROR_NO_SERVER);
 n=rdnss_ra(f,120);chos_net6_receive(f,n);n=rdnss_ra(f,0);chos_net6_receive(f,n);
 assert(!chos_net6_dns_start(42,"withdrawn.example.com"));
 n=rdnss_ra(f,120);chos_net6_receive(f,n);assert(chos_net6_dns_start(42,"offline.example.com"));
 chos_net6_poll(1,0);assert(chos_net6_dns_info(42,&info)&&info.state==CHOS_DNS6_FAILED&&info.error==CHOS_DNS6_ERROR_NETWORK);
 chos_net6_drop_owner(42);assert(chos_net6_dns_info(42,&info)&&info.state==CHOS_DNS6_IDLE);
 chos_net6_poll(1,1);for(unsigned i=0;i<3;i++){now+=1000;chos_net6_poll(1,1);}n=ra(f,255);chos_net6_receive(f,n);for(unsigned i=0;i<3;i++){now+=1000;chos_net6_poll(1,1);}
 puts("IPv6 DNS: actual UDP6 AAAA, checksum/ID/owner validation, RDNSS expiry/withdrawal, entropy refusal, retry timeout and disconnect cancellation passed");
}

static void tick(unsigned n){for(unsigned i=0;i<n;i++){now+=1000;chos_net6_poll(1,1);}}
int main(void){
 const uint8_t mac[6]={2,0x12,0x34,0x56,0x78,0x9a};uint8_t f[128];chos_net6_info_t s;
 chos_net6_init(mac);chos_net6_poll(1,1);tick(3);chos_net6_get_info(&s);
 assert(s.flags==7&&s.address_states[0]==CHOS_NET6_ADDRESS_PREFERRED&&tx_count);
 assert(s.addresses[0][0]==0xfe&&s.addresses[0][1]==0x80);
 char text[46];assert(chos_net6_format(s.addresses[0],text,sizeof(text)));assert(!chos_net6_format(s.addresses[0],text,2));
 size_t n=ra(f,254);chos_net6_receive(f,n);chos_net6_get_info(&s);assert(s.address_states[1]==0);
 n=ra(f,255);f[56]^=1;chos_net6_receive(f,n);chos_net6_get_info(&s);assert(s.address_states[1]==0);
 n=ra(f,255);chos_net6_receive(f,n);tick(3);chos_net6_get_info(&s);
 assert(s.address_states[1]==CHOS_NET6_ADDRESS_PREFERRED&&s.addresses[1][0]==0x20&&s.addresses[1][5]==1);
 assert(s.router[0]==0xfe&&s.router[15]==1);
 test_tcp6(&s);
 test_dns6(&s);
 assert(!chos_net6_ping("garbage")&&!chos_net6_ping("::")&&!chos_net6_ping("ff02::1"));
 assert(chos_net6_ping("2001:db8:1::2"));assert(!chos_net6_ping("2001:db8:1::3"));
 /* Correctly scoped, checksummed echo reply to the owned global address. */
 memset(f,0,sizeof(f));memcpy(f,mac,6);f[6]=2;f[11]=2;f[12]=0x86;f[13]=0xdd;f[14]=0x60;be16(f+18,16);f[20]=58;f[21]=64;
 f[22]=0x20;f[23]=1;f[24]=0x0d;f[25]=0xb8;f[27]=1;f[37]=2;memcpy(f+38,s.addresses[1],16);
 f[54]=129;f[58]=0x43;f[59]=0x48;f[61]=1;for(unsigned i=8;i<16;i++)f[54+i]=i^0x53u;checksum(f);
 f[56]^=1;chos_net6_receive(f,70);chos_net6_get_info(&s);assert(s.ping_state==CHOS_NET6_PING_PENDING);
 f[56]^=1;f[37]=3;checksum(f);chos_net6_receive(f,70);chos_net6_get_info(&s);assert(s.ping_state==CHOS_NET6_PING_PENDING);
 f[37]=2;f[69]^=1;checksum(f);chos_net6_receive(f,70);chos_net6_get_info(&s);assert(s.ping_state==CHOS_NET6_PING_PENDING);
 f[69]^=1;f[61]=2;checksum(f);chos_net6_receive(f,70);chos_net6_get_info(&s);assert(s.ping_state==CHOS_NET6_PING_PENDING);
 f[61]=1;checksum(f);chos_net6_receive(f,70);chos_net6_get_info(&s);assert(s.ping_state==CHOS_NET6_PING_REPLIED);
 assert(chos_net6_ping("2001:db8:1::3"));tick(6);chos_net6_get_info(&s);assert(s.ping_state==CHOS_NET6_PING_TIMED_OUT);
 assert(chos_net6_ping("2001:db8:1::4"));chos_net6_poll(1,0);chos_net6_get_info(&s);assert(s.ping_state==CHOS_NET6_PING_FAILED&&s.flags==5);
 for(unsigned i=0;i<3;i++)assert(s.address_states[i]==0);for(unsigned i=0;i<16;i++)assert(s.router[i]==0);
 for(unsigned i=0;i<3;i++)for(unsigned j=0;j<16;j++)assert(s.addresses[i][j]==0);
 chos_net6_poll(1,1);tick(3);n=ra(f,255);chos_net6_receive(f,n);
 chos_net6_get_info(&s);assert(s.address_states[1]==CHOS_NET6_ADDRESS_TENTATIVE);
 /* Another node claiming the tentative target must prevent its use. */
 uint8_t address[16];memcpy(address,s.addresses[1],16);memset(f,0,sizeof(f));f[0]=f[1]=0x33;f[5]=1;f[6]=2;f[11]=2;f[12]=0x86;f[13]=0xdd;
 f[14]=0x60;be16(f+18,24);f[20]=58;f[21]=255;f[22]=0xfe;f[23]=0x80;f[37]=2;f[38]=0xff;f[39]=2;f[53]=1;
 f[54]=136;memcpy(f+62,address,16);checksum(f);chos_net6_receive(f,78);tick(3);chos_net6_get_info(&s);assert(s.address_states[1]==CHOS_NET6_ADDRESS_DUPLICATED);
 chos_net6_poll(0,1);chos_net6_get_info(&s);assert(s.flags==3);
 chos_net6_poll(1,1);tick(3);
 /* Malformed length/option traffic must remain bounded and must not configure. */
 n=ra(f,255);f[71]=0;checksum(f);chos_net6_receive(f,n);
 for(unsigned i=0;i<1000;i++){n=ra(f,255);f[18]=(uint8_t)i;f[19]=(uint8_t)(i*7);chos_net6_receive(f,(i%110)+1);}
 puts("lwIP IPv6: DAD/SLAAC/RA hop-limit/checksum/duplicate address, echo validation, timeout and link/admin cleanup passed");
 return last_tx_bytes?0:1;
}
