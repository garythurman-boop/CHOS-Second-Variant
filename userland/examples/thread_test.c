#include "chos/user.h"
static volatile unsigned release_workers,started,completed,file_opened;
static unsigned shared_count;
typedef struct { _Alignas(16) uint64_t tls[4];uint64_t stack;uint32_t tid,index;uint64_t observed_pid; } worker_t;
static worker_t workers[3];
static void say(const char *s){chos_write(s,chos_strlen(s));}
static int fail(unsigned line){(void)line;say("[FAIL] native threads\n");return 1;}
#define CHECK(x) do {if(!(x))return fail(__LINE__);}while(0)
static void worker(void *argument){
 worker_t *w=argument;int64_t status=0x12340000u+w->index;
 if(chos_thread_tls()!=w->tls||chos_thread_id()<=0||chos_getpid()!=(long)w->observed_pid)chos_thread_exit(-11);
 w->tid=(uint32_t)chos_thread_id();__atomic_fetch_add(&started,1u,__ATOMIC_RELEASE);
 while(!__atomic_load_n(&release_workers,__ATOMIC_ACQUIRE))chos_sleep_ticks(1);
 long fd=chos_open("/BIN/THREAD.ELF");if(fd<0)chos_thread_exit(-15);
 __atomic_fetch_add(&file_opened,1u,__ATOMIC_RELEASE);
 while(__atomic_load_n(&file_opened,__ATOMIC_ACQUIRE)<3)chos_sleep_ticks(1);
 unsigned char header[256];long got=chos_read((int)fd,header,sizeof(header));
 if(got!=256||header[0]!=0x7f||header[1]!='E'||header[2]!='L'||header[3]!='F'||chos_close((int)fd))chos_thread_exit(-16);
 for(unsigned round=0;round<3;round++){
  for(unsigned i=0;i<40000;i++)__atomic_fetch_add(&shared_count,1u,__ATOMIC_RELAXED);
  for(volatile unsigned busy=0;busy<3000000u;busy++){}
  if(chos_thread_tls()!=w->tls||w->tls[0]!=(0xabc00000u+w->index))chos_thread_exit(-12);
  chos_yield();chos_sleep_ticks(1);
  if(chos_thread_tls()!=w->tls||chos_thread_id()!=w->tid)chos_thread_exit(-13);
 }
 __atomic_fetch_add(&completed,1u,__ATOMIC_RELEASE);chos_thread_exit(status);
}
static void sleeper(void *argument){
 (void)argument;__atomic_fetch_add(&started,1u,__ATOMIC_RELEASE);
 for(;;)chos_sleep_ticks(60000);
}
static void spinner(void *argument){
 (void)argument;__atomic_fetch_add(&started,1u,__ATOMIC_RELEASE);
 for(;;)__asm__ volatile("nop");
}
static void fault_worker(void *argument){
 (void)argument;__atomic_fetch_add(&started,1u,__ATOMIC_RELEASE);
 *(volatile uint64_t *)(uintptr_t)0x1000=1;chos_thread_exit(-14);
}
static long launch(unsigned i,void (*entry)(void *)){
 worker_t *w=&workers[i];w->index=i;w->observed_pid=(uint64_t)chos_getpid();w->tls[0]=0xabc00000u+i;
 long stack=chos_map_memory(32768);if(stack<0)return stack;w->stack=(uint64_t)stack;
 chos_thread_request_t r={.version=CHOS_THREAD_API_VERSION,.size_bytes=sizeof(r),.entry=(uint64_t)(uintptr_t)entry,
  .stack_base=(uint64_t)stack,.stack_bytes=32768,.argument=(uint64_t)(uintptr_t)w,.tls=(uint64_t)(uintptr_t)w->tls};
 long id=chos_thread_create(&r);if(id<=0)chos_unmap_memory((void *)(uintptr_t)stack,32768);return id;
}
int main(int argc,char **argv){
 if(argc>1&&!chos_strcmp(argv[1],"foreign")){
  unsigned id=0;for(const char *p=argv[2];*p;p++)id=id*10u+(unsigned)(*p-'0');chos_thread_info_t info;
  CHECK(chos_thread_status(id,&info)==-CHOS_ERRNO_ESRCH&&chos_thread_collect(id)==-CHOS_ERRNO_ESRCH);
  say("[ OK ] native thread foreign-owner handles denied\n");return 0;
 }
 CHECK(chos_thread_id()>0&&chos_thread_tls()==0);
 if(argc>1){
  CHECK(launch(0,sleeper)>0);CHECK(launch(1,spinner)>0);
  if(!chos_strcmp(argv[1],"fault")){
   CHECK(launch(2,fault_worker)>0);for(;;)chos_sleep_ticks(60000);
  }
  for(unsigned n=0;__atomic_load_n(&started,__ATOMIC_ACQUIRE)<2;n++){CHECK(n<500);chos_sleep_ticks(1);}
  say("[ OK ] native thread process exit cancels CPU and sleeping workers\n");return 77;
 }
 CHECK(chos_syscall2(CHOS_SYS_THREAD_CREATE,0,sizeof(chos_thread_request_t))==-CHOS_ERRNO_EFAULT);
 CHECK(chos_syscall2(CHOS_SYS_THREAD_CREATE,0,sizeof(chos_thread_request_t)-1)==-CHOS_ERRNO_EINVAL);
 for(unsigned lifecycle=0;lifecycle<3;lifecycle++){
 release_workers=started=completed=file_opened=shared_count=0;
 uint32_t ids[3];
 for(unsigned i=0;i<3;i++){
  long id=launch(i,worker);CHECK(id>0);ids[i]=(uint32_t)id;
  if(i==0){
   chos_thread_request_t duplicate={.version=1,.size_bytes=64,.entry=(uint64_t)(uintptr_t)worker,
    .stack_base=workers[0].stack,.stack_bytes=32768,.tls=(uint64_t)(uintptr_t)workers[0].tls};
   CHECK(chos_thread_create(&duplicate)==-CHOS_ERRNO_EBUSY);
   duplicate.entry=workers[0].stack;CHECK(chos_thread_create(&duplicate)==-CHOS_ERRNO_EFAULT);
   duplicate.entry=(uint64_t)(uintptr_t)worker;duplicate.tls=(uint64_t)(uintptr_t)worker&~UINT64_C(15);
   CHECK(chos_thread_create(&duplicate)==-CHOS_ERRNO_EFAULT);
  }
 }
 for(unsigned i=0;i<3;i++){
  CHECK(chos_unmap_memory((void *)(uintptr_t)workers[i].stack,32768)==-CHOS_ERRNO_EBUSY);
  CHECK(chos_unmap_range((void *)(uintptr_t)workers[i].stack,4096)==-CHOS_ERRNO_EBUSY);
  CHECK(chos_memory_control((void *)(uintptr_t)workers[i].stack,4096,CHOS_MEMORY_PROTECT_NONE)==-CHOS_ERRNO_EBUSY);
  CHECK(chos_memory_control((void *)(uintptr_t)workers[i].stack,4096,CHOS_MEMORY_DISCARD)==-CHOS_ERRNO_EBUSY);
  chos_thread_request_t r={.version=CHOS_THREAD_API_VERSION,.size_bytes=sizeof(r),.entry=(uint64_t)(uintptr_t)worker,
   .stack_base=workers[i].stack,.stack_bytes=32768,.tls=(uint64_t)(uintptr_t)workers[i].tls};
  CHECK(chos_thread_create(&r)==-CHOS_ERRNO_EAGAIN); /* join-record quota, no stack reuse */
 }
 char args[32]="foreign ";unsigned id=ids[0],n=0;char reverse[12];do{reverse[n++]=(char)('0'+id%10);id/=10;}while(id);
 unsigned off=8;while(n)args[off++]=reverse[--n];args[off]=0;
 long foreign=chos_spawn_args("/BIN/THREAD.ELF",args);CHECK(foreign>0&&chos_wait((uint32_t)foreign)==0);
 __atomic_store_n(&release_workers,1u,__ATOMIC_RELEASE);
 for(unsigned i=0;i<3;i++){
  chos_thread_info_t info;
  CHECK(chos_thread_status(ids[i],(chos_thread_info_t *)(uintptr_t)worker)==-CHOS_ERRNO_EFAULT);
  CHECK(chos_syscall3(CHOS_SYS_THREAD_INFO,ids[i],(long)(uintptr_t)&info,sizeof(info)-1)==-CHOS_ERRNO_EINVAL);
  for(unsigned j=0;;j++){CHECK(j<1000);CHECK(!chos_thread_status(ids[i],&info));if(info.state==CHOS_THREAD_EXITED||info.state==CHOS_THREAD_FAILED)break;chos_sleep_ticks(1);}
  CHECK(info.state==CHOS_THREAD_EXITED&&info.exit_status==(int64_t)(0x12340000u+i)&&info.reserved==0);
  CHECK(!chos_thread_collect(ids[i]));CHECK(chos_thread_status(ids[i],&info)==-CHOS_ERRNO_ESRCH);
  CHECK(!chos_unmap_memory((void *)(uintptr_t)workers[i].stack,32768));
 }
 CHECK(shared_count==3u*3u*40000u&&completed==3u&&chos_thread_tls()==0);
 }
 say("[ OK ] native shared-AS threads, atomic data, private TLS, joins and stack pins passed\n");return 0;
}
