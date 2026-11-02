#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include "chos/user.h"
#include "chos/tls_runtime.h"
#include "chos/resource_profile_user.h"
#define ATTR_MAGIC UINT32_C(0x50544154)
#define KEY_INDEX_BITS 6u
#define KEY_GENERATION_MAX (UINT32_MAX>>KEY_INDEX_BITS)
/* Kept in a GPR-only assembly shim so native libc C retains its integer ABI. */
extern void __chos_pthread_get_fp(uint64_t *,uint64_t *);
extern void __chos_pthread_set_fp(uint64_t,uint64_t);
typedef struct {uint32_t generation,live,late;void (*destroy)(void *);} key_t;
static key_t keys[PTHREAD_KEYS_MAX];
static pthread_mutex_t keys_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t active_c_threads=1;
enum { JOINABLE=0,JOINING=1,DETACHED=2 };
/* This lock deliberately has no pthread/TLS owner. A detached worker must
 * release it after its original TCB has been freed. No user callback or heap
 * allocation runs while acquiring it. Heap backends never acquire this lock. */
static uint32_t lifetime_word;
static _Alignas(16) chos_libc_tcb_t finalizer_tcb;
static void lifetime_lock(void){
 uint32_t zero=0;
 if(__atomic_compare_exchange_n(&lifetime_word,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
 for(;;){
  if(!__atomic_exchange_n(&lifetime_word,2,__ATOMIC_ACQUIRE))return;
  long rc=chos_wait_word(&lifetime_word,2,60000);
  if(rc&&rc!=-EAGAIN&&rc!=-ETIMEDOUT)chos_exit(127);
 }
}
static void lifetime_unlock(void){
 if(__atomic_exchange_n(&lifetime_word,0,__ATOMIC_RELEASE)==2&&chos_wake_word(&lifetime_word,1)<0)chos_exit(127);
}
pthread_t pthread_self(void){return (pthread_t)(uintptr_t)__chos_libc_tls();}
int pthread_equal(pthread_t a,pthread_t b){return a==b;}
int pthread_attr_init(pthread_attr_t *a){if(!a)return EINVAL;*a=(pthread_attr_t){ATTR_MAGIC,chos_native_worker_stack_bytes(),0};return 0;}
int pthread_attr_destroy(pthread_attr_t *a){if(!a||a->magic!=ATTR_MAGIC)return EINVAL;a->magic=0;return 0;}
int pthread_attr_setstacksize(pthread_attr_t *a,size_t n){if(!a||a->magic!=ATTR_MAGIC||n<PTHREAD_STACK_MIN||n>CHOS_THREAD_STACK_MAX)return EINVAL;a->stack_bytes=(n+4095u)&~(size_t)4095u;return 0;}
int pthread_attr_getstacksize(const pthread_attr_t *a,size_t *out){if(!a||a->magic!=ATTR_MAGIC||!out)return EINVAL;*out=a->stack_bytes;return 0;}
int pthread_attr_setdetachstate(pthread_attr_t *a,int state){if(!a||a->magic!=ATTR_MAGIC||(state!=PTHREAD_CREATE_JOINABLE&&state!=PTHREAD_CREATE_DETACHED))return EINVAL;a->detached=state;return 0;}
int pthread_attr_getdetachstate(const pthread_attr_t *a,int *out){if(!a||a->magic!=ATTR_MAGIC||!out)return EINVAL;*out=a->detached;return 0;}
static int key_lookup(pthread_key_t key,unsigned *index){
 unsigned slot=key&((1u<<KEY_INDEX_BITS)-1u),generation=key>>KEY_INDEX_BITS;
 if(slot>=PTHREAD_KEYS_MAX||!generation||!keys[slot].live||keys[slot].generation!=generation)return 0;
 *index=slot;return 1;
}
static int key_create(pthread_key_t *out,void (*destroy)(void *),unsigned late){
 if(!out)return EINVAL;int rc=pthread_mutex_lock(&keys_lock);if(rc)return rc;
 unsigned i;for(i=0;i<PTHREAD_KEYS_MAX;i++)if(!keys[i].live&&keys[i].generation<KEY_GENERATION_MAX)break;
 if(i==PTHREAD_KEYS_MAX){pthread_mutex_unlock(&keys_lock);return EAGAIN;}
 ++keys[i].generation;keys[i].live=1;keys[i].late=late;keys[i].destroy=destroy;*out=(keys[i].generation<<KEY_INDEX_BITS)|i;
 pthread_mutex_unlock(&keys_lock);return 0;
}
int pthread_key_create(pthread_key_t *out,void (*destroy)(void *)){return key_create(out,destroy,0);}
/* Compiler TLS storage must survive every ordinary pthread destructor pass. */
int __chos_pthread_key_create_late(pthread_key_t *out,void (*destroy)(void *)){return key_create(out,destroy,1);}
/* libc++ exit notifications follow both C++ TLS and ordinary key destructors. */
int __chos_pthread_key_create_final(pthread_key_t *out,void (*destroy)(void *)){return key_create(out,destroy,2);}
int pthread_key_delete(pthread_key_t key){
 int rc=pthread_mutex_lock(&keys_lock);if(rc)return rc;unsigned slot;
 if(!key_lookup(key,&slot)){pthread_mutex_unlock(&keys_lock);return EINVAL;}
 keys[slot].live=0;keys[slot].destroy=0;pthread_mutex_unlock(&keys_lock);return 0;
}
int pthread_setspecific(pthread_key_t key,const void *value){
 chos_libc_tcb_t *t=__chos_libc_tls();int rc=pthread_mutex_lock(&keys_lock);if(rc)return rc;unsigned slot;
 if(!key_lookup(key,&slot)){pthread_mutex_unlock(&keys_lock);return EINVAL;}
 t->specific[slot]=(chos_specific_t){(void *)value,keys[slot].generation};pthread_mutex_unlock(&keys_lock);return 0;
}
void *pthread_getspecific(pthread_key_t key){
 chos_libc_tcb_t *t=__chos_libc_tls();if(pthread_mutex_lock(&keys_lock))return 0;unsigned slot;void *value=0;
 if(key_lookup(key,&slot)&&t->specific[slot].generation==keys[slot].generation)value=t->specific[slot].value;
 pthread_mutex_unlock(&keys_lock);return value;
}
static void destructors(chos_libc_tcb_t *t){
 for(unsigned iteration=0;iteration<PTHREAD_DESTRUCTOR_ITERATIONS;iteration++){
  int called=0;
  for(unsigned i=0;i<PTHREAD_KEYS_MAX;i++){
   void (*destroy)(void *)=0;void *value=0;
   if(pthread_mutex_lock(&keys_lock))return;
   if(keys[i].live&&!keys[i].late&&t->specific[i].generation==keys[i].generation&&t->specific[i].value){
    destroy=keys[i].destroy;value=t->specific[i].value;t->specific[i].value=0;
   }
   pthread_mutex_unlock(&keys_lock);
   if(destroy){called=1;destroy(value);}
  }
  if(!called)break;
 }
}
static void phase_destructors(chos_libc_tcb_t *t,unsigned phase){
 for(unsigned i=0;i<PTHREAD_KEYS_MAX;i++){
  void (*destroy)(void *)=0;void *value=0;
  if(pthread_mutex_lock(&keys_lock))abort();
  if(keys[i].live&&keys[i].late==phase&&t->specific[i].generation==keys[i].generation&&t->specific[i].value){
   destroy=keys[i].destroy;value=t->specific[i].value;t->specific[i].value=0;
  }
  pthread_mutex_unlock(&keys_lock);if(destroy)destroy(value);
 }
}
void pthread_exit(void *result){
 chos_libc_tcb_t *t=__chos_libc_tls();
 if(t->process_exiting||t->process_finalizer_depth||t->cxx_worker_active||t->finalizing_keys||t->terminating)abort();
 if(!t->exiting){t->exiting=1;__chos_once_abandon(t);__chos_cxx_guard_abandon(t);__chos_cxx_thread_finalize(t);destructors(t);}
 /* A key/TLS destructor may itself exit during a new guarded/once initializer. */
 __chos_once_abandon(t);__chos_cxx_guard_abandon(t);
 /* Key destructors can construct previously unused C++ TLS objects. Their
  * callbacks must run while compiler TLS storage is still retained. */
 __chos_cxx_thread_finalize(t);
 t->finalizing_keys=1;phase_destructors(t,2);t->finalizing_keys=0;
 if(!t->exit_counted){
  t->exit_counted=1;
  if(__atomic_fetch_sub(&active_c_threads,1,__ATOMIC_ACQ_REL)==1)exit(0);
 }
 phase_destructors(t,1);
 unsigned main_thread=t->main_thread;
 lifetime_lock();
 t->result=result;__atomic_store_n(&t->done,1,__ATOMIC_RELEASE);
 if(chos_wake_word(&t->done,8)<0)chos_exit(127);
 int cleanup_error=0;
 if(t->joining==DETACHED&&!t->main_thread){
  /* Every user/key/compiler TLS destructor has completed. malloc/free must
   * still unlock their own pthread heap mutex after unmapping this TCB's
   * arena, so give that private final cleanup a valid, serialized TLS owner.
   * No user code observes this owner and no TLS access follows unlock. */
  finalizer_tcb=(chos_libc_tcb_t){.magic=CHOS_TCB_MAGIC,.tid=t->tid,.exiting=1,.exit_counted=1,.terminating=1};
  __asm__ volatile("msr tpidr_el0,%0\n\tisb"::"r"((uintptr_t)&finalizer_tcb):"memory");
  t->magic=0;cleanup_error=__chos_libc_free_thread_storage(t);
 }
 lifetime_unlock();
 if(cleanup_error)chos_exit(127);
 long rc=chos_syscall1(CHOS_SYS_THREAD_EXIT,(long)(uintptr_t)result);
 /* The synchronous single-task profile has no siblings to preserve. */
 chos_exit(main_thread&&rc==-CHOS_ERRNO_ENOSYS?0:127);
}
static void start_thread(void *p){
 chos_libc_tcb_t *t=p;
 while(!__atomic_load_n(&t->tid,__ATOMIC_ACQUIRE)){
  long rc=chos_wait_word(&t->tid,0,60000);
  if(rc&&rc!=-EAGAIN&&rc!=-ETIMEDOUT)chos_exit(127);
 }
 /* Raw kernel workers start with zero FP state; the C pthread contract inherits
  * only its creating thread's FP environment, never another process's Q data. */
 __chos_pthread_set_fp(t->initial_fpcr,t->initial_fpsr);
 pthread_exit(t->start(t->argument));
}
int pthread_create(pthread_t *out,const pthread_attr_t *a,void *(*start)(void *),void *argument){
 if(!out||!start||(a&&a->magic!=ATTR_MAGIC))return EINVAL;
 if(!__atomic_load_n(&active_c_threads,__ATOMIC_ACQUIRE)||__chos_libc_tls()->process_exiting)return EAGAIN;
 if(a&&a->detached!=PTHREAD_CREATE_JOINABLE&&a->detached!=PTHREAD_CREATE_DETACHED)return EINVAL;
 size_t bytes=a?a->stack_bytes:chos_native_worker_stack_bytes();if(bytes<PTHREAD_STACK_MIN||bytes>CHOS_THREAD_STACK_MAX||(bytes&4095u))return EINVAL;
 uint64_t fpcr,fpsr;__chos_pthread_get_fp(&fpcr,&fpsr);
 (void)__chos_libc_tls();chos_libc_tcb_t *t=calloc(1,sizeof(*t));if(!t)return ENOMEM;
 long base=chos_map_memory(bytes);if(base<0){free(t);return (int)-base;}
 t->magic=CHOS_TCB_MAGIC;t->start=start;t->argument=argument;t->stack_base=(uint64_t)base;t->stack_bytes=bytes;
 t->initial_fpcr=fpcr;t->initial_fpsr=fpsr;
 t->joining=a&&a->detached==PTHREAD_CREATE_DETACHED?DETACHED:JOINABLE;
 chos_thread_request_t r={.version=CHOS_THREAD_MANAGED_VERSION,.size_bytes=sizeof(r),.entry=(uint64_t)(uintptr_t)start_thread,.stack_base=(uint64_t)base,
  .stack_bytes=bytes,.argument=(uint64_t)(uintptr_t)t,.tls=(uint64_t)(uintptr_t)t,
  .reserved={CHOS_THREAD_MANAGED_STACK|(t->joining==DETACHED?CHOS_THREAD_CREATE_DETACHED:0),0}};
 __atomic_add_fetch(&active_c_threads,1,__ATOMIC_ACQ_REL);
 long id=chos_thread_create(&r);
 if(id<0){__atomic_sub_fetch(&active_c_threads,1,__ATOMIC_ACQ_REL);chos_unmap_memory((void *)(uintptr_t)base,bytes);t->magic=0;free(t);return (int)-id;}
 lifetime_lock();*out=(pthread_t)(uintptr_t)t;
 __atomic_store_n(&t->tid,(uint32_t)id,__ATOMIC_RELEASE);
 if(chos_wake_word(&t->tid,1)<0)chos_exit(127);
 lifetime_unlock();return 0;
}
int pthread_join(pthread_t thread,void **result){
 if(!thread)return ESRCH;chos_libc_tcb_t *t=(void *)(uintptr_t)thread;
 if(t==__chos_libc_tls())return EDEADLK;
 lifetime_lock();
 if(t->magic!=CHOS_TCB_MAGIC){lifetime_unlock();return ESRCH;}
 if(t->joining!=JOINABLE){lifetime_unlock();return EINVAL;}
 t->joining=JOINING;lifetime_unlock();
 int rc=0;
 while(!__atomic_load_n(&t->done,__ATOMIC_ACQUIRE)){
  long wait=chos_wait_word(&t->done,0,60000);if(wait&&wait!=-EAGAIN&&wait!=-ETIMEDOUT){rc=(int)-wait;goto failed;}
 }
 if(t->main_thread){if(result)*result=t->result;return 0;}
 for(;;){
  chos_thread_info_t info;long status=chos_thread_status(__atomic_load_n(&t->tid,__ATOMIC_ACQUIRE),&info);
  if(status<0){rc=(int)-status;goto failed;}
  if(info.state==CHOS_THREAD_EXITED||info.state==CHOS_THREAD_FAILED)break;
  long sleep=chos_sleep_ticks(1);if(sleep<0){rc=(int)-sleep;goto failed;}
 }
 long collected=chos_thread_collect(t->tid);if(collected<0){rc=(int)-collected;goto failed;}
 if(chos_unmap_memory((void *)(uintptr_t)t->stack_base,t->stack_bytes)<0){rc=EIO;goto failed;}
 if(result)*result=t->result;t->magic=0;free(t);return 0;
failed:
 lifetime_lock();t->joining=JOINABLE;lifetime_unlock();return rc;
}
int pthread_detach(pthread_t thread){
 if(!thread)return ESRCH;chos_libc_tcb_t *t=(void *)(uintptr_t)thread;
 lifetime_lock();
 if(t->magic!=CHOS_TCB_MAGIC){lifetime_unlock();return ESRCH;}
 if(t->joining!=JOINABLE){lifetime_unlock();return EINVAL;}
 long rc=t->main_thread?0:chos_thread_detach(__atomic_load_n(&t->tid,__ATOMIC_ACQUIRE));
 if(rc<0){lifetime_unlock();return (int)-rc;}
 t->joining=DETACHED;
 if(__atomic_load_n(&t->done,__ATOMIC_ACQUIRE)&&!t->main_thread){t->magic=0;if(__chos_libc_free_thread_storage(t))chos_exit(127);}
 lifetime_unlock();return 0;
}
