#include <assert.h>
#include <string.h>
#include "chos/ipc.h"
#include "chos/syscall.h"
int main(void){
 unsigned char out[512],input[512];for(unsigned i=0;i<512;i++)input[i]=(unsigned char)i;
 assert(chos_ipc_open(0,1)==-CHOS_ERRNO_EINVAL&&chos_ipc_open(1,1)==-CHOS_ERRNO_EINVAL);
 int64_t a=chos_ipc_open(1,2);assert(a>0&&chos_ipc_open(1,2)==a);
 chos_ipc_info_t info;assert(!chos_ipc_status(1,(uint64_t)a,&info)&&!info.flags&&!info.queued_messages&&!info.reserved[0]&&!info.reserved[1]);
 assert(chos_ipc_send(1,(uint64_t)a,input,1)==-CHOS_ERRNO_EAGAIN);
 int64_t b=chos_ipc_open(2,1);assert(b>0&&b!=a);
 assert(chos_ipc_status(3,(uint64_t)a,&info)==-CHOS_ERRNO_EBADF);
 assert(chos_ipc_send(3,(uint64_t)a,input,1)==-CHOS_ERRNO_EBADF);
 assert(chos_ipc_close(3,(uint64_t)a)==-CHOS_ERRNO_EBADF);
 assert(chos_ipc_send(1,(uint64_t)a,input,513)==-CHOS_ERRNO_EINVAL);
 for(unsigned i=0;i<4;i++)assert(chos_ipc_send(1,(uint64_t)a,input,512)==512);
 assert(chos_ipc_send(1,(uint64_t)a,input,1)==-CHOS_ERRNO_EAGAIN);
 assert(chos_ipc_receive(2,(uint64_t)b,out,511)==-CHOS_ERRNO_EMSGSIZE);
 assert(!chos_ipc_status(2,(uint64_t)b,&info)&&info.queued_messages==4&&info.next_message_bytes==512);
 for(unsigned i=0;i<4;i++){memset(out,0,512);assert(chos_ipc_receive(2,(uint64_t)b,out,512)==512&&!memcmp(out,input,512));}
 assert(chos_ipc_receive(2,(uint64_t)b,out,512)==-CHOS_ERRNO_EAGAIN);
 assert(chos_ipc_send(1,(uint64_t)a,input,3)==3);assert(!chos_ipc_close(1,(uint64_t)a));
 assert(chos_ipc_receive(2,(uint64_t)b,out,512)==3);assert(chos_ipc_receive(2,(uint64_t)b,out,512)==0);
 assert(chos_ipc_send(2,(uint64_t)b,input,1)==-CHOS_ERRNO_EPIPE);
 assert(!chos_ipc_status(2,(uint64_t)b,&info)&&info.flags==CHOS_IPC_PEER_CLOSED&&info.received_bytes==2051);
 assert(chos_ipc_open(1,2)==-CHOS_ERRNO_EBUSY);assert(!chos_ipc_close(2,(uint64_t)b));
 int64_t newer=chos_ipc_open(1,2);assert(newer>0&&newer!=a);
 assert(chos_ipc_status(1,(uint64_t)a,&info)==-CHOS_ERRNO_EBADF);chos_ipc_drop_owner(2);
 assert(!chos_ipc_status(1,(uint64_t)newer,&info)&&info.flags==CHOS_IPC_PEER_CLOSED);chos_ipc_drop_owner(1);
 for(unsigned i=0;i<4;i++)assert(chos_ipc_open(10,20+i)>0);
 assert(chos_ipc_open(10,24)==-CHOS_ERRNO_EMFILE);assert(chos_ipc_open(25,10)>0);
 assert(chos_ipc_open(10,25)==-CHOS_ERRNO_EMFILE);chos_ipc_drop_owner(10);chos_ipc_drop_owner(25);
 for(unsigned i=0;i<10000;i++){
  a=chos_ipc_open(1,2);b=chos_ipc_open(2,1);assert(a>0&&b>0);
  assert(chos_ipc_send(1,(uint64_t)a,input,(i%512)+1)==(int)(i%512)+1);
  chos_ipc_drop_owner(1);assert(chos_ipc_receive(2,(uint64_t)b,out,512)==(int)(i%512)+1);
  assert(chos_ipc_receive(2,(uint64_t)b,out,512)==0);chos_ipc_drop_owner(2);
 }
 for(unsigned i=0;i<8;i++)assert(chos_ipc_open(100+i*2,101+i*2)>0);
 assert(chos_ipc_open(200,201)==-CHOS_ERRNO_ENOSPC);
 for(unsigned i=0;i<16;i++)chos_ipc_drop_owner(100+i);
 assert(chos_ipc_status(1,UINT64_MAX,&info)==-CHOS_ERRNO_EBADF);
 return 0;
}
