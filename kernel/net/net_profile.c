#include "chos/net_profile.h"
static int unicast(uint32_t a){unsigned first=a>>24;return first && first!=127 && first<224 && a!=UINT32_MAX;}
int chos_net_ipv4_parse(const char *s,uint32_t *out){
 if(!s||!out)return 0;
 uint32_t value=0;
 for(unsigned octet=0;octet<4;octet++){
  unsigned n=0,digits=0;
  while(*s>='0'&&*s<='9'){if(++digits>3)return 0;n=n*10u+(unsigned)(*s++-'0');if(n>255)return 0;}
  if(!digits || (octet<3?*s++!='.':*s!=0))return 0;
  value=(value<<8)|n;
 }
 *out=value;return 1;
}
void chos_net_ipv4_format(uint32_t address,char out[16]){
 unsigned at=0;
 for(unsigned i=0;i<4;i++){
  unsigned octet=(address>>(24-8*i))&255u;
  if(octet>=100)out[at++]=(char)('0'+octet/100);
  if(octet>=10)out[at++]=(char)('0'+(octet/10)%10);
  out[at++]=(char)('0'+octet%10);if(i<3)out[at++]='.';
 }
 out[at]=0;
}
const char *chos_net_config_error(const chos_net_config_t *c){
 if(!c||c->mode>CHOS_NET_DISABLED)return "Invalid network mode.";
 if(c->dns&&!unicast(c->dns))return "DNS must be a unicast IPv4 address.";
 if(c->mode!=CHOS_NET_STATIC){
  if(c->ipv4||c->netmask||c->gateway || (c->mode==CHOS_NET_DISABLED && c->dns))return "Automatic/disconnected mode has unexpected static fields.";
  return 0;
 }
 if(!unicast(c->ipv4))return "Address must be a unicast IPv4 address.";
 uint32_t host=~c->netmask;
 if(!c->netmask || (host&(host+1u)))return "Subnet mask must have contiguous leading bits.";
 if(host>1u && (!(c->ipv4&host)||(c->ipv4&host)==host))return "Address is a subnet or broadcast address.";
 if(c->gateway){
  if(!unicast(c->gateway)||c->gateway==c->ipv4 || ((c->gateway^c->ipv4)&c->netmask))return "Gateway must be a different unicast address in this subnet.";
  if(host>1u && (!(c->gateway&host)||(c->gateway&host)==host))return "Gateway is a subnet or broadcast address.";
 }
 return 0;
}
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static uint32_t get(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
int chos_net_profile_encode(const chos_net_config_t *c,uint8_t out[64]){
 if(!out||chos_net_config_error(c))return 0;
 for(unsigned i=0;i<64;i++)out[i]=0;
 const char *magic="CHOSNET1";for(unsigned i=0;i<8;i++)out[i]=(uint8_t)magic[i];
 put(out+8,c->mode);put(out+12,c->ipv4);put(out+16,c->netmask);put(out+20,c->gateway);put(out+24,c->dns);
 for(unsigned i=0;i<6;i++)out[28+i]=c->mac[i];
 put(out+60,crc(out,60));return 1;
}
int chos_net_profile_decode(const uint8_t *in,size_t bytes,chos_net_config_t *out){
 if(!in||!out||bytes!=64)return 0;
 const char *magic="CHOSNET1";for(unsigned i=0;i<8;i++)if(in[i]!=(uint8_t)magic[i])return 0;
 if(get(in+60)!=crc(in,60))return 0;
 for(unsigned i=34;i<60;i++)if(in[i])return 0;
 chos_net_config_t c={.mode=get(in+8),.ipv4=get(in+12),.netmask=get(in+16),.gateway=get(in+20),.dns=get(in+24)};
 for(unsigned i=0;i<6;i++)c.mac[i]=in[28+i];
 if(chos_net_config_error(&c))return 0;
 *out=c;return 1;
}
