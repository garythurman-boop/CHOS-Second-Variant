/* Actual EL0 regression; never mutates a host disk. */
#include "chos/user.h"
static const char read_only[]="read only";
static int failure(void){const char *s="[FAIL] EL0 network service boundary\n";chos_write(s,chos_strlen(s));return 1;}
int main(int argc,char **argv){
 (void)argc;(void)argv;
 chos_net_status_t status;
 chos_net6_info_t v6;
 if(chos_network_status6(&v6)!=0 || v6.version!=CHOS_NET6_VERSION || v6.size_bytes!=128 ||
    chos_syscall2(CHOS_SYS_NET6_INFO,0,sizeof(v6))!=-CHOS_ERRNO_EFAULT ||
    chos_syscall2(CHOS_SYS_NET6_INFO,(long)(uintptr_t)read_only,sizeof(v6))!=-CHOS_ERRNO_EFAULT ||
    chos_syscall2(CHOS_SYS_NET6_INFO,(long)(uintptr_t)&v6,127)!=-CHOS_ERRNO_EINVAL ||
    chos_syscall1(CHOS_SYS_NET6_PING,0)!=-CHOS_ERRNO_EFAULT ||
    chos_ping6("::")!=-CHOS_ERRNO_EAGAIN)return failure();
 if(chos_network_status(&status)!=0 || status.version!=CHOS_NET_STATUS_VERSION || status.size_bytes!=64 ||
    !(status.flags&CHOS_NET_STATUS_ADAPTER) || !(status.capabilities&CHOS_NET_SUPPORT_IPV4))return failure();
 if(chos_syscall2(CHOS_SYS_NET_INFO,0,sizeof(status))!=-CHOS_ERRNO_EFAULT ||
    chos_syscall2(CHOS_SYS_NET_INFO,(long)(uintptr_t)read_only,sizeof(status))!=-CHOS_ERRNO_EFAULT ||
    chos_syscall2(CHOS_SYS_NET_INFO,(long)(uintptr_t)&status,63)!=-CHOS_ERRNO_EINVAL)return failure();
 if(chos_write_file("/cfg/network.cfg","x",1)!=-CHOS_ERRNO_EPERM ||
    chos_unlink("/CFG/NETWORK.CFG")!=-CHOS_ERRNO_EPERM ||
    chos_mkdir("/Cfg/Network.Cfg")!=-CHOS_ERRNO_EPERM ||
    chos_rmdir("/CFG")!=-CHOS_ERRNO_EPERM ||
    chos_rename("/DOCUMENTS/a.txt","/CFG/NETWORK.CFG")!=-CHOS_ERRNO_EPERM)return failure();
 if(argc>1 && chos_strcmp(argv[1],"ipv6")==0){
  unsigned attempt;
  for(attempt=0;attempt<80;attempt++){
   if(chos_network_status6(&v6)!=0)return failure();
   if(v6.address_states[1]==CHOS_NET6_ADDRESS_PREFERRED)break;
   chos_sleep_ticks(10);
  }
  if(attempt==80 || chos_ping6("fec0::2")!=0)return failure();
  for(attempt=0;attempt<60;attempt++){
   chos_sleep_ticks(10);if(chos_network_status6(&v6)!=0)return failure();
   if(v6.ping_state==CHOS_NET6_PING_REPLIED)break;
  }
  if(attempt==60)return failure();
  const char *ok="[ OK ] EL0 IPv6 status and actual router echo passed\n";chos_write(ok,chos_strlen(ok));
 }
 const char *s="[ OK ] EL0 read-only network status and protected profile passed\n";chos_write(s,chos_strlen(s));return 0;
}
