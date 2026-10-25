/* Real EL0 owner-bound AAAA service against the disposable Ethernet fixture. */
#include "chos/user.h"
static const char readonly[]="read-only";
static void say(const char *s){chos_write(s,chos_strlen(s));}
static int fail(const char *s){say("[FAIL] EL0 DNS6 ");say(s);say("\n");return 1;}
static int await(chos_dns6_info_t *out){
 for(unsigned i=0;i<1000;i++){
  if(chos_net_dns6_status(out))return 0;
  if(out->state!=CHOS_DNS6_PENDING)return 1;
  chos_sleep_ticks(1);
 }
 return 0;
}
int main(int argc,char **argv){
 chos_dns6_info_t info;int missing=argc>1 && !chos_strcmp(argv[1],"missing");
 if(chos_net_dns6_status(&info) || info.version!=1 || info.size_bytes!=sizeof(info) || info.state!=CHOS_DNS6_IDLE ||
    chos_syscall2(CHOS_SYS_NET6_DNS_STATUS,0,sizeof(info))!=-CHOS_ERRNO_EFAULT ||
    chos_syscall2(CHOS_SYS_NET6_DNS_STATUS,(long)(uintptr_t)readonly,sizeof(info))!=-CHOS_ERRNO_EFAULT ||
    chos_syscall2(CHOS_SYS_NET6_DNS_STATUS,(long)(uintptr_t)&info,sizeof(info)-1)!=-CHOS_ERRNO_EINVAL ||
    chos_net_dns6_lookup(0)!=-CHOS_ERRNO_EFAULT || chos_net_dns6_lookup("bad..name")!=-CHOS_ERRNO_EAGAIN)return fail("ABI validation");
 for(unsigned i=0;i<150;i++){
  chos_net6_info_t n;if(chos_network_status6(&n))return fail("status");
  if(n.address_states[1]==CHOS_NET6_ADDRESS_PREFERRED)break;
  if(i==149)return fail("SLAAC");chos_sleep_ticks(10);
 }
 long rc=-CHOS_ERRNO_EAGAIN;
 for(unsigned i=0;i<100;i++){
  rc=chos_net_dns6_lookup("valid.chos.test");if(!rc)break;
  if(chos_net_dns6_status(&info))return fail("lookup status");
  if(missing && info.error==CHOS_DNS6_ERROR_ENTROPY){say("[ OK ] EL0 DNS6 hardware entropy absence fails closed\n");return 0;}
  if(rc!=-CHOS_ERRNO_EAGAIN)return fail("lookup");chos_sleep_ticks(2);
 }
 if(missing || rc)return fail("RDNSS/entropy readiness");
 if(chos_net_dns6_lookup("other.chos.test")!=-CHOS_ERRNO_EBUSY)return fail("pending owner");
 if(!await(&info) || info.state!=CHOS_DNS6_RESOLVED || info.address[0]!=0x20 || info.address[1]!=1 ||
    info.address[2]!=0x0d || info.address[3]!=0xb8 || info.address[15]!=7 || info.ttl_seconds!=30 ||
    info.server[0]!=0xfe || info.server[1]!=0x80 || info.server[15]!=1)return fail("AAAA response");
 if(chos_net_dns6_lookup("missing.chos.test") || !await(&info) || info.state!=CHOS_DNS6_FAILED ||
    info.error!=CHOS_DNS6_ERROR_RESPONSE || info.rcode!=3)return fail("NXDOMAIN");
 if(chos_net_dns6_lookup("timeout.chos.test") || !await(&info) || info.state!=CHOS_DNS6_FAILED ||
    info.error!=CHOS_DNS6_ERROR_TIMEOUT || info.retries!=3)return fail("retry timeout");
 if(chos_net_dns6_lookup("withdraw.chos.test") || !await(&info) || info.state!=CHOS_DNS6_FAILED ||
    info.error!=CHOS_DNS6_ERROR_NETWORK)return fail("RDNSS withdrawal");
 say("[ OK ] EL0 DNS6 real AAAA, malformed rejection, NXDOMAIN and retry timeout passed\n");return 0;
}
