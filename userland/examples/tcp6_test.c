/* Actual EL0 IPv6 socket regression. The remote fixture is host-owned loopback;
 * traffic still traverses guest IPv6/TCP, Ethernet queues and QEMU user net. */
#include "chos/user.h"
static void say(const char *s){chos_write(s,chos_strlen(s));}
static const char readonly[]="read-only";
static int fail(const char *where){say("[FAIL] EL0 TCP6 ");chos_write(where,chos_strlen(where));say("\n");return 1;}
int main(int argc,char **argv){
 chos_tcp6_socket_info_t info;uint8_t buffer[512],peer[16]={0xfe,0xc0};peer[15]=2;
 if(argc>1 && !chos_strcmp(argv[1],"foreign")){
  int h=argv[2][0]-'0';
  if(chos_net_tcp6_status(h,&info)!=-CHOS_ERRNO_EBADF || chos_net_tcp6_send(h,"x",1)!=-CHOS_ERRNO_EBADF ||
     chos_net_tcp6_recv(h,buffer,1)!=-CHOS_ERRNO_EBADF || chos_net_tcp6_close(h)!=-CHOS_ERRNO_EBADF ||
     chos_net_tcp6_abort(h)!=-CHOS_ERRNO_EBADF)return fail("foreign owner");
  say("[ OK ] EL0 TCP6 foreign owner denied\n");return 0;
 }
 uint16_t port=0;int missing=argc>1 && !chos_strcmp(argv[1],"missing");
 if(!missing && argc>1)for(unsigned i=0;argv[1][i];i++){if(argv[1][i]<'0'||argv[1][i]>'9')return fail("port");port=(uint16_t)(port*10u+argv[1][i]-'0');}
 if(!port)port=12345;
 for(unsigned n=0;n<100;n++){
  chos_net6_info_t net;if(chos_network_status6(&net))return fail("status");
  if(net.address_states[1]==CHOS_NET6_ADDRESS_PREFERRED)break;
  if(n==99)return fail("SLAAC");chos_sleep_ticks(10);
 }
 long h=chos_net_tcp6_open(0);if(h<=0)return fail("open");
 if(chos_net_tcp6_status((int)h,&info) || info.version!=1 || info.size_bytes!=sizeof(info) ||
    chos_syscall3(CHOS_SYS_NET6_STATUS,h,0,sizeof(info))!=-CHOS_ERRNO_EFAULT ||
    chos_syscall3(CHOS_SYS_NET6_STATUS,h,(long)(uintptr_t)readonly,sizeof(info))!=-CHOS_ERRNO_EFAULT ||
    chos_syscall3(CHOS_SYS_NET6_STATUS,h,(long)(uintptr_t)&info,sizeof(info)-1)!=-CHOS_ERRNO_EINVAL ||
    chos_net_tcp6_connect((int)h,0,port)!=-CHOS_ERRNO_EFAULT ||
    chos_net_tcp6_send((int)h,0,1)!=-CHOS_ERRNO_EFAULT ||
    chos_net_tcp6_recv((int)h,(void *)readonly,1)!=-CHOS_ERRNO_EFAULT ||
    chos_net_tcp6_send((int)h,buffer,513)!=-CHOS_ERRNO_E2BIG ||
    chos_net_tcp6_recv((int)h,buffer,513)!=-CHOS_ERRNO_E2BIG ||
    chos_net_tcp6_send((int)h,0,0)!=0)return fail("pointer/length contract");
 if(missing){
  for(unsigned i=0;i<4;i++)if(chos_net_tcp6_connect((int)h,peer,port)!=-CHOS_ERRNO_EAGAIN)return fail("missing entropy");
  if(chos_net_tcp6_status((int)h,&info) || info.state!=CHOS_TCP_CLOSED || chos_net_tcp6_abort((int)h))return fail("entropy side effects");
  say("[ OK ] EL0 TCP6 missing entropy fails closed\n");return 0;
 }
 char arguments[]="foreign 0";arguments[8]=(char)('0'+h);
 long child=chos_spawn_args("/BIN/TCP6TEST.CHX",arguments);if(child<=0 || chos_wait(child))return fail("owner child");
 long rc=-CHOS_ERRNO_EAGAIN;
 for(unsigned i=0;i<60;i++){rc=chos_net_tcp6_connect((int)h,peer,port);if(!rc)break;if(rc!=-CHOS_ERRNO_EAGAIN)return fail("connect");chos_sleep_ticks(2);}
 if(rc)return fail("entropy readiness");
 for(unsigned i=0;i<80;i++){
  if(chos_net_tcp6_status((int)h,&info))return fail("connect status");
  if(info.state==CHOS_TCP_ESTABLISHED)break;
  if(info.state==CHOS_TCP_FAILED || i==79)return fail("handshake");chos_sleep_ticks(5);
 }
 const char request[]="CHOS IPv6 stream fixture\n";
 for(unsigned i=0;i<60;i++){rc=chos_net_tcp6_send((int)h,request,sizeof(request)-1);if(rc>0)break;if(rc!=-CHOS_ERRNO_EAGAIN)return fail("send");chos_sleep_ticks(2);}
 if(rc!=(long)sizeof(request)-1)return fail("send count");
 unsigned count=0;int eof=0;
 for(unsigned i=0;i<500;i++){
  rc=chos_net_tcp6_recv((int)h,buffer,sizeof(buffer));
  if(rc>0){for(unsigned j=0;j<(unsigned)rc;j++)if(buffer[j]!=(uint8_t)((count++*17u+0x31u)&255u))return fail("data");}
  else if(!rc){eof=1;break;}
  else if(rc!=-CHOS_ERRNO_EAGAIN)return fail("receive");
  chos_sleep_ticks(1);
 }
 if(count!=6144 || !eof)return fail("bounded receive/EOF");
 if(chos_net_tcp6_close((int)h))return fail("close");
 for(unsigned i=0;i<100;i++){
  if(chos_net_tcp6_status((int)h,&info))return fail("close status");
  if(info.state==CHOS_TCP_CLOSED)break;
  if(info.state==CHOS_TCP_FAILED || i==99)return fail("FIN");chos_sleep_ticks(2);
 }
 if(chos_net_tcp6_abort((int)h) || chos_net_tcp6_status((int)h,&info)!=-CHOS_ERRNO_EBADF)return fail("release");
 /* Exit with all slots still open. The parent's second run requires the actual
  * process reaper to release their owner and lwIP PCBs, rather than app cleanup. */
 for(unsigned i=0;i<CHOS_TCP6_MAX_SOCKETS;i++)if(chos_net_tcp6_open(0)<=0)return fail("cleanup fixture");
 say("[ OK ] EL0 TCP6 wire data, EOF, FIN and syscall bounds passed\n");return 0;
}
