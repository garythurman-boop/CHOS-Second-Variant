#include "chos/user.h"
static uint32_t lock_word,never_word=0,timeout_word=7;
static unsigned shared_count;
static volatile unsigned started,inside;
typedef struct {_Alignas(16) uint64_t tls[4];uint64_t stack;} argument_t;
static argument_t args[3];
static void say(const char *s){chos_write(s,chos_strlen(s));}
static int fail(unsigned line){(void)line;say("[FAIL] native futex\n");return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static int lock(void){
 uint32_t zero=0;
 if(__atomic_compare_exchange_n(&lock_word,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
 for(;;){
  if(__atomic_exchange_n(&lock_word,2,__ATOMIC_ACQUIRE)==0)return 0;
  long rc=chos_wait_word(&lock_word,2,60000);
  if(rc&&rc!=-CHOS_ERRNO_EAGAIN&&rc!=-CHOS_ERRNO_ETIMEDOUT)return (int)rc;
 }
}
static int unlock(void){
 if(__atomic_fetch_sub(&lock_word,1,__ATOMIC_RELEASE)==1)return 0;
 __atomic_store_n(&lock_word,0,__ATOMIC_RELEASE);
 long n=chos_wake_word(&lock_word,1);return n<0?(int)n:0;
}
static void worker(void *p){
 argument_t *a=p;if(chos_thread_tls()!=a->tls)chos_thread_exit(-1);
 __atomic_fetch_add(&started,1,__ATOMIC_RELEASE);
 for(unsigned i=0;i<200;i++){
  if(lock())chos_thread_exit(-2);
  if(inside++)chos_thread_exit(-3);
  ++shared_count;if(!(i&15u))chos_yield();
  if(inside!=1||chos_thread_tls()!=a->tls)chos_thread_exit(-4);
  --inside;if(unlock())chos_thread_exit(-5);
 }
 chos_thread_exit(0);
}
static void forever_waiter(void *p){
 (void)p;__atomic_fetch_add(&started,1,__ATOMIC_RELEASE);
 for(;;)chos_wait_word(&never_word,0,60000);
}
static long create(unsigned i,void (*entry)(void *)){
 long base=chos_map_memory(32768);if(base<0)return base;args[i].stack=(uint64_t)base;
 chos_thread_request_t r={.version=1,.size_bytes=sizeof(r),.entry=(uint64_t)(uintptr_t)entry,.stack_base=(uint64_t)base,.stack_bytes=32768,
  .argument=(uint64_t)(uintptr_t)&args[i],.tls=(uint64_t)(uintptr_t)args[i].tls};
 long id=chos_thread_create(&r);if(id<0)chos_unmap_memory((void *)(uintptr_t)base,32768);return id;
}
int main(int argc,char **argv){
 if(argc>1&&!chos_strcmp(argv[1],"foreign")){
  CHECK(chos_wake_word(&lock_word,8)==0);say("[ OK ] private futex foreign process cannot wake matching virtual address\n");return 0;
 }
 CHECK(chos_wait_word((uint32_t *)(uintptr_t)1,0,1)==-CHOS_ERRNO_EINVAL);
 CHECK(chos_wait_word(0,0,1)==-CHOS_ERRNO_EFAULT);
 CHECK(chos_wait_word((uint32_t *)((uintptr_t)worker&~(uintptr_t)3),0,1)==-CHOS_ERRNO_EFAULT);
 CHECK(chos_wait_word(&lock_word,1,60000)==-CHOS_ERRNO_EAGAIN);
 CHECK(chos_wait_word(&lock_word,0,0)==-CHOS_ERRNO_ETIMEDOUT);
 CHECK(chos_wait_word(&timeout_word,7,2)==-CHOS_ERRNO_ETIMEDOUT);
 CHECK(chos_wake_word(&timeout_word,1)==0);
 CHECK(chos_wait_word(&lock_word,0,60001)==-CHOS_ERRNO_EINVAL);
 CHECK(chos_wake_word(&lock_word,9)==-CHOS_ERRNO_EINVAL);
 CHECK(chos_syscall3(CHOS_SYS_FUTEX_WAIT,(long)(uintptr_t)&lock_word,(long)UINT64_C(0x100000000),1)==-CHOS_ERRNO_EINVAL);
 if(argc>1){
  CHECK(create(0,forever_waiter)>0);CHECK(create(1,forever_waiter)>0);
  while(__atomic_load_n(&started,__ATOMIC_ACQUIRE)<2)chos_sleep_ticks(1);
  chos_sleep_ticks(5);say("[ OK ] private futex process exit cancels sleeping waiters\n");return 77;
 }
 for(unsigned lifecycle=0;lifecycle<3;lifecycle++){
  lock_word=1;started=inside=shared_count=0;uint32_t ids[3];
  for(unsigned i=0;i<3;i++){long id=create(i,worker);CHECK(id>0);ids[i]=(uint32_t)id;}
  for(unsigned n=0;__atomic_load_n(&started,__ATOMIC_ACQUIRE)<3;n++){CHECK(n<500);chos_sleep_ticks(1);}
  chos_sleep_ticks(5);
  long foreign=chos_spawn_args("/BIN/FUTEX.ELF","foreign");CHECK(foreign>0&&chos_wait((uint32_t)foreign)==0);
  CHECK(chos_wake_word(&lock_word,2)==2);chos_sleep_ticks(5);
  CHECK(chos_wake_word(&lock_word,1)==1);chos_sleep_ticks(5);
  CHECK(!unlock());
  for(unsigned i=0;i<3;i++){
   chos_thread_info_t info;
   for(unsigned n=0;;n++){
    CHECK(n<2000&&chos_thread_status(ids[i],&info)==0);
    if(info.state==CHOS_THREAD_EXITED||info.state==CHOS_THREAD_FAILED)break;
    chos_sleep_ticks(1);
   }
   CHECK(info.state==CHOS_THREAD_EXITED&&info.exit_status==0);CHECK(!chos_thread_collect(ids[i]));
   CHECK(!chos_unmap_memory((void *)(uintptr_t)args[i].stack,32768));
  }
  CHECK(shared_count==600&&inside==0&&lock_word==0&&chos_wake_word(&lock_word,8)==0);
 }
 say("[ OK ] private futex mutex, bounded wake, timeout and repeated lifecycle passed\n");return 0;
}
