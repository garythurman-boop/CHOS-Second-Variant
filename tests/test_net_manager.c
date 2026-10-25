#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chos/network_policy.h"
#include "../kernel/net/net_manager.c"
static uint64_t now;
static int adapter=1,timer=1,save_ok=1,read_ok=1,profile_exists;
static uint8_t profile[64];static uint32_t stored_size=64;
static chos_net_config_t active;
uint64_t timer_counter(void){return now;}
uint64_t timer_frequency_hz(void){return 100;}
int timer_is_ready(void){return timer;}
int chos_net_device_ready(void){return adapter;}
void chos_net_device_get_info(chos_net_device_info_t *out){*out=(chos_net_device_info_t){.ready=adapter,.link_status_known=1,.link_up=1,.mac={2,3,4,5,6,7}};}
const char *chos_net_device_name(void){return "fixture adapter";}
const char *chos_net_device_error(void){return "fixture ready";}
void chos_net_stack_get_info(chos_net_stack_info_t *out){*out=(chos_net_stack_info_t){.ipv4_address=active.ipv4};}
int chos_net_stack_configure(const chos_net_config_t *c){if(chos_net_config_error(c))return 0;active=*c;return 1;}
int storage_lookup(const char *path,chos_chfs_entry_t *e){assert(!strcmp(path,PROFILE_PATH));e->size_bytes=stored_size;return profile_exists;}
int storage_make_directory(const char *path){assert(!strcmp(path,"/CFG"));return save_ok;}
int storage_write_file(const char *path,const void *data,size_t size){assert(!strcmp(path,PROFILE_PATH)&&size==64);if(!save_ok)return 0;memcpy(profile,data,64);profile_exists=1;return 1;}
int storage_read_file(const char *path,void *data,size_t cap,size_t *size){assert(!strcmp(path,PROFILE_PATH)&&cap==64);if(!profile_exists||!read_ok)return 0;memcpy(data,profile,64);*size=64;return 1;}
int storage_sync(void){return save_ok;}
int main(void){
 assert(chos_network_profile_reserved("/cfg/Network.cfg"));assert(chos_network_profile_reserved("/CFG"));assert(chos_network_profile_reserved("/CFG/NETWORK.CFG/x"));assert(!chos_network_profile_reserved("/DOCUMENTS/a.txt"));assert(!chos_network_profile_reserved("/CFG/NETWORK.CFGx"));
 uint32_t ip;char text[16];assert(chos_net_ipv4_parse("10.0.2.25",&ip)&&ip==0x0a000219);
 assert(!chos_net_ipv4_parse("1.2.3.256",&ip));assert(!chos_net_ipv4_parse("1.2.3.4x",&ip));assert(!chos_net_ipv4_parse("1.2.3",&ip));
 chos_net_ipv4_format(0x0a000219,text);assert(!strcmp(text,"10.0.2.25"));
 chos_net_config_t c={.mode=CHOS_NET_STATIC,.ipv4=0x0a000219,.netmask=0xffffff00,.gateway=0x0a000202,.dns=0x0a000203};
 assert(!chos_net_config_error(&c));uint8_t encoded[64];assert(chos_net_profile_encode(&c,encoded));
 chos_net_config_t decoded;assert(chos_net_profile_decode(encoded,64,&decoded)&&decoded.ipv4==c.ipv4);
 assert(!chos_net_profile_decode(encoded,63,&decoded));encoded[19]^=1;assert(!chos_net_profile_decode(encoded,64,&decoded));
 decoded=c;decoded.netmask=0xff00ff00;assert(chos_net_config_error(&decoded));decoded=c;decoded.ipv4=0x0a0002ff;assert(chos_net_config_error(&decoded));
 decoded=c;decoded.gateway=0x0a000301;assert(chos_net_config_error(&decoded));
 chos_net_manager_init();assert(!trial);
 adapter=0;assert(!chos_net_manager_apply(&c));adapter=1;timer=0;assert(!chos_net_manager_apply(&c));timer=1;
 assert(chos_net_manager_apply(&c)&&trial);now=2999;chos_net_manager_poll();assert(trial);
 now=3000;chos_net_manager_poll();assert(!trial&&active.mode==CHOS_NET_DHCP);
 assert(chos_net_manager_apply(&c));save_ok=0;assert(!chos_net_manager_keep(1)&&trial);
 save_ok=1;assert(chos_net_manager_keep(1)&&!trial);
 initialized=0;current=(chos_net_config_t){0};chos_net_manager_init();assert(current.ipv4==c.ipv4);
 c.mac[0]=10;assert(chos_net_profile_encode(&c,profile));initialized=0;current=(chos_net_config_t){0};chos_net_manager_init();assert(current.mode==CHOS_NET_DHCP);c.mac[0]=2;
 stored_size=65;initialized=0;current=(chos_net_config_t){0};chos_net_manager_init();assert(current.mode==CHOS_NET_DHCP);
 stored_size=64;profile[0]^=1;initialized=0;chos_net_manager_init();assert(current.mode==CHOS_NET_DHCP);
 c=(chos_net_config_t){0};assert(chos_net_manager_apply(&c));assert(chos_net_manager_keep(0)&&!trial);
 puts("network profiles/config: IPv4 validation, CRC/bounds, 30s rollback, save failure, readback and reboot load passed");
}
