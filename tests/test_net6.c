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
static size_t ra(uint8_t f[128],unsigned hop){
 memset(f,0,128);f[0]=f[1]=0x33;f[5]=1;f[6]=2;f[11]=1;f[12]=0x86;f[13]=0xdd;
 f[14]=0x60;be16(f+18,56);f[20]=58;f[21]=hop;f[22]=0xfe;f[23]=0x80;f[37]=1;f[38]=0xff;f[39]=2;f[53]=1;
 uint8_t *p=f+54;p[0]=134;p[4]=64;be16(p+6,1800);p[16]=3;p[17]=4;p[18]=64;p[19]=0xc0;
 p[22]=0x0e;p[23]=0x10;p[26]=7;p[27]=8;p[32]=0x20;p[33]=1;p[34]=0x0d;p[35]=0xb8;p[37]=1;
 p[48]=1;p[49]=1;memcpy(p+50,f+6,6);checksum(f);return 110;
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
