#include "chos/user.h"
static int fail(unsigned line){const char *s="[FAIL] native IPC runtime\n";chos_write(s,chos_strlen(s));return (int)line;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static uint64_t number(const char *p){uint64_t n=0;for(unsigned i=0;p[i]&&i<20;i++){if(p[i]<'0'||p[i]>'9'||n>(UINT64_MAX-(unsigned)(p[i]-'0'))/10)return 0;n=n*10+(unsigned)(p[i]-'0');}return n;}
static void arguments(char *out,const char *kind,uint64_t value){unsigned n=0;while(*kind)out[n++]=*kind++;out[n++]=' ';char digits[24];unsigned k=0;do{digits[k++]=(char)('0'+value%10);value/=10;}while(value);while(k)out[n++]=digits[--k];out[n]=0;}
static int await_read(uint64_t handle,void *out,size_t capacity){
 for(unsigned i=0;i<500;i++){long n=chos_ipc_read(handle,out,capacity);if(n!=-CHOS_ERRNO_EAGAIN)return (int)n;(void)chos_sleep_ticks(1);}
 return -CHOS_ERRNO_EIO;
}
static int await_send(uint64_t handle,const void *out,size_t bytes){
 for(unsigned i=0;i<500;i++){long n=chos_ipc_write(handle,out,bytes);if(n!=-CHOS_ERRNO_EAGAIN)return (int)n;(void)chos_sleep_ticks(1);}
 return -CHOS_ERRNO_EIO;
}
int main(int argc,char **argv){
 if(argc==3&&chos_strcmp(argv[1],"intruder")==0){
  uint64_t stolen=number(argv[2]);chos_ipc_info_t info;char b[4];
  CHECK(chos_ipc_get_status(stolen,&info)==-CHOS_ERRNO_EBADF);
  CHECK(chos_ipc_write(stolen,"bad",3)==-CHOS_ERRNO_EBADF);
  CHECK(chos_ipc_read(stolen,b,sizeof(b))==-CHOS_ERRNO_EBADF);
  CHECK(chos_ipc_release(stolen)==-CHOS_ERRNO_EBADF);
  const char *s="[ OK ] foreign IPC handle ownership denied\n";chos_write(s,chos_strlen(s));return 0;
 }
 if(argc==3&&chos_strcmp(argv[1],"child")==0){
  long h=chos_ipc_connect((uint32_t)number(argv[2]));CHECK(h>0);
  unsigned char data[512];CHECK(await_read((uint64_t)h,data,512)==512);
  for(unsigned i=0;i<512;i++)CHECK(data[i]==(unsigned char)i);
  CHECK(await_send((uint64_t)h,data,512)==512);
  /* Intentionally exit without closing: peer must drain queued data then EOF. */
  return 0;
 }
 CHECK(argc==1);CHECK(chos_ipc_connect(0)==-CHOS_ERRNO_ESRCH);
 CHECK(chos_ipc_connect((uint32_t)chos_getpid())==-CHOS_ERRNO_EINVAL);
 CHECK(chos_syscall1(CHOS_SYS_IPC_OPEN,(long)UINT64_MAX)==-CHOS_ERRNO_ESRCH);
 for(unsigned round=0;round<6;round++){
  char args[64];arguments(args,"child",(uint64_t)chos_getpid());
  long child=chos_spawn_args("/BIN/IPCTEST.ELF",args);CHECK(child>0);
  long h=chos_ipc_connect((uint32_t)child);CHECK(h>0);
  chos_ipc_info_t info;
  CHECK(chos_syscall3(CHOS_SYS_IPC_STATUS,h,0,sizeof(info))==-CHOS_ERRNO_EFAULT);
  CHECK(chos_syscall3(CHOS_SYS_IPC_STATUS,h,(long)(uintptr_t)&info,sizeof(info)-1)==-CHOS_ERRNO_EINVAL);
  arguments(args,"intruder",(uint64_t)h);
  long intruder=chos_spawn_args("/BIN/IPCTEST.ELF",args);CHECK(intruder>0&&chos_wait((uint32_t)intruder)==0);
  unsigned char input[512];for(unsigned i=0;i<512;i++)input[i]=(unsigned char)i;
  CHECK(await_send((uint64_t)h,input,512)==512);
  for(unsigned i=0;i<500;i++){CHECK(!chos_ipc_get_status((uint64_t)h,&info));if(info.queued_messages)break;(void)chos_sleep_ticks(1);}
  CHECK(info.queued_messages==1&&info.next_message_bytes==512);
  CHECK(chos_syscall3(CHOS_SYS_IPC_RECV,h,(long)(uintptr_t)main,512)==-CHOS_ERRNO_EFAULT);
  unsigned char out[514];out[0]=0x43;out[513]=0x53;
  CHECK(chos_ipc_read((uint64_t)h,out+1,511)==-CHOS_ERRNO_EMSGSIZE);
  CHECK(chos_ipc_read((uint64_t)h,out+1,512)==512&&out[0]==0x43&&out[513]==0x53);
  for(unsigned i=0;i<512;i++)CHECK(out[i+1]==(unsigned char)i);
  CHECK(chos_wait((uint32_t)child)==0);
  CHECK(await_read((uint64_t)h,out,512)==0);
  CHECK(chos_ipc_write((uint64_t)h,input,1)==-CHOS_ERRNO_EPIPE);
  CHECK(!chos_ipc_release((uint64_t)h));CHECK(chos_ipc_get_status((uint64_t)h,&info)==-CHOS_ERRNO_EBADF);
 }
 const char *s="[ OK ] native IPC mutual ownership, message transfer, pointer safety and peer-exit cleanup passed\n";
 chos_write(s,chos_strlen(s));return 0;
}
