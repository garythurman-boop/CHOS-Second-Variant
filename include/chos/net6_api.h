#ifndef CHOS_NET6_API_H
#define CHOS_NET6_API_H
#include <stdint.h>
#define CHOS_NET6_VERSION 1u
#define CHOS_NET6_READY 1u
#define CHOS_NET6_LINK_UP 2u
#define CHOS_NET6_ADMIN_ENABLED 4u
#define CHOS_NET6_ADDRESS_INVALID 0u
#define CHOS_NET6_ADDRESS_TENTATIVE 1u
#define CHOS_NET6_ADDRESS_PREFERRED 2u
#define CHOS_NET6_ADDRESS_DEPRECATED 3u
#define CHOS_NET6_ADDRESS_DUPLICATED 4u
#define CHOS_NET6_PING_IDLE 0u
#define CHOS_NET6_PING_PENDING 1u
#define CHOS_NET6_PING_REPLIED 2u
#define CHOS_NET6_PING_TIMED_OUT 3u
#define CHOS_NET6_PING_FAILED 4u
/* Read-only diagnostic control-plane status; no TCP6/DNS6/DHCP6 claim. */
typedef struct {
 uint32_t version,size_bytes,flags,ping_state;
 uint8_t addresses[3][16];
 uint32_t address_states[3],reserved;
 uint8_t router[16];
 uint64_t rx_frames,tx_frames,dropped_frames,ping_elapsed_ms;
} chos_net6_info_t;
_Static_assert(sizeof(chos_net6_info_t)==128,"IPv6 read-only status ABI");
static inline const char *chos_net6_ping_name(uint32_t state){
 switch(state){case CHOS_NET6_PING_PENDING:return "pending";case CHOS_NET6_PING_REPLIED:return "reply received";
 case CHOS_NET6_PING_TIMED_OUT:return "timed out";case CHOS_NET6_PING_FAILED:return "failed";default:return "idle";}
}
/* Fixed-width notation for tiny clients; requires 40 bytes including NUL. */
static inline void chos_net6_address_text(const uint8_t address[16],char out[40]){
 static const char hex[]="0123456789abcdef";unsigned n=0;
 for(unsigned i=0;i<16;i++){if(i && !(i&1u))out[n++]=':';out[n++]=hex[address[i]>>4];out[n++]=hex[address[i]&15];}out[n]=0;
}
#endif
