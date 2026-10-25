/* Actual EL0 regression; never mutates a host disk. */
#include "chos/user.h"
static const char read_only[]="read only";
static int failure(void){const char *s="[FAIL] EL0 network service boundary\n";chos_write(s,chos_strlen(s));return 1;}
int main(int argc,char **argv){
 (void)argc;(void)argv;
 chos_net_status_t status;
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
 const char *s="[ OK ] EL0 read-only network status and protected profile passed\n";chos_write(s,chos_strlen(s));return 0;
}
