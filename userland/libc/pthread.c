#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include "chos/user.h"
#include "chos/tls_runtime.h"
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
pthread_t pthread_self(void){return (pthread_t)(uintptr_t)__chos_libc_tls();}
int pthread_equal(pthread_t a,pthread_t b){return a==b;}
int pthread_attr_init(pthread_attr_t *a){if(!a)return EINVAL;*a=(pthread_attr_t){ATTR_MAGIC,32768,0};return 0;}
int pthread_attr_destroy(pthread_attr_t *a){if(!a||a->magic!=ATTR_MAGIC)return EINVAL;a->magic=0;return 0;}
int pthread_attr_setstacksize(pthread_attr_t *a,size_t n){if(!a||a->magic!=ATTR_MAGIC||n<PTHREAD_STACK_MIN||n>CHOS_THREAD_STACK_MAX)return EINVAL;a->stack_bytes=(n+4095u)&~(size_t)4095u;return 0;}
int pthread_attr_getstacksize(const pthread_attr_t *a,size_t *out){if(!a||a->magic!=ATTR_MAGIC||!out)return EINVAL;*out=a->stack_bytes;return 0;}
int pthread_attr_setdetachstate(pthread_attr_t *a,int state){if(!a||a->magic!=ATTR_MAGIC)return EINVAL;if(state==PTHREAD_CREATE_DETACHED)return ENOTSUP;if(state!=PTHREAD_CREATE_JOINABLE)return EINVAL;a->detached=state;return 0;}
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
static void late_destructors(chos_libc_tcb_t *t){
 for(unsigned i=0;i<PTHREAD_KEYS_MAX;i++){
  void (*destroy)(void *)=0;void *value=0;
  if(pthread_mutex_lock(&keys_lock))abort();
  if(keys[i].live&&keys[i].late&&t->specific[i].generation==keys[i].generation&&t->specific[i].value){
   destroy=keys[i].destroy;value=t->specific[i].value;t->specific[i].value=0;
  }
  pthread_mutex_unlock(&keys_lock);if(destroy)destroy(value);
 }
}
void pthread_exit(void *result){
 chos_libc_tcb_t *t=__chos_libc_tls();
 if(t->process_exiting||t->process_finalizer_depth)abort();
 if(!t->exiting){t->exiting=1;__chos_once_abandon(t);__chos_cxx_guard_abandon(t);__chos_cxx_thread_finalize(t);destructors(t);}
 /* A key/TLS destructor may itself exit during a new guarded/once initializer. */
 __chos_once_abandon(t);__chos_cxx_guard_abandon(t);
 /* Key destructors can construct previously unused C++ TLS objects. Their
  * callbacks must run while compiler TLS storage is still retained. */
 __chos_cxx_thread_finalize(t);
 if(!t->exit_counted){
  t->exit_counted=1;
  if(__atomic_fetch_sub(&active_c_threads,1,__ATOMIC_ACQ_REL)==1)exit(0);
 }
 late_destructors(t);
 t->result=result;__atomic_store_n(&t->done,1,__ATOMIC_RELEASE);(void)chos_wake_word(&t->done,8);
 long rc=chos_syscall1(CHOS_SYS_THREAD_EXIT,(long)(uintptr_t)result);
 /* The synchronous single-task profile has no siblings to preserve. */
 chos_exit(t->main_thread&&rc==-CHOS_ERRNO_ENOSYS?0:127);
}
static void start_thread(void *p){
 chos_libc_tcb_t *t=p;
 /* Raw kernel workers start with zero FP state; the C pthread contract inherits
  * only its creating thread's FP environment, never another process's Q data. */
 __chos_pthread_set_fp(t->initial_fpcr,t->initial_fpsr);
 pthread_exit(t->start(t->argument));
}
int pthread_create(pthread_t *out,const pthread_attr_t *a,void *(*start)(void *),void *argument){
 if(!out||!start||(a&&a->magic!=ATTR_MAGIC))return EINVAL;
 if(!__atomic_load_n(&active_c_threads,__ATOMIC_ACQUIRE)||__chos_libc_tls()->process_exiting)return EAGAIN;
 if(a&&a->detached!=PTHREAD_CREATE_JOINABLE)return ENOTSUP;
 size_t bytes=a?a->stack_bytes:32768;if(bytes<PTHREAD_STACK_MIN||bytes>CHOS_THREAD_STACK_MAX||(bytes&4095u))return EINVAL;
 uint64_t fpcr,fpsr;__chos_pthread_get_fp(&fpcr,&fpsr);
 (void)__chos_libc_tls();chos_libc_tcb_t *t=calloc(1,sizeof(*t));if(!t)return ENOMEM;
 long base=chos_map_memory(bytes);if(base<0){free(t);return (int)-base;}
 t->magic=CHOS_TCB_MAGIC;t->start=start;t->argument=argument;t->stack_base=(uint64_t)base;t->stack_bytes=bytes;
 t->initial_fpcr=fpcr;t->initial_fpsr=fpsr;
 chos_thread_request_t r={.version=1,.size_bytes=sizeof(r),.entry=(uint64_t)(uintptr_t)start_thread,.stack_base=(uint64_t)base,
  .stack_bytes=bytes,.argument=(uint64_t)(uintptr_t)t,.tls=(uint64_t)(uintptr_t)t};
 __atomic_add_fetch(&active_c_threads,1,__ATOMIC_ACQ_REL);
 long id=chos_thread_create(&r);
 if(id<0){__atomic_sub_fetch(&active_c_threads,1,__ATOMIC_ACQ_REL);chos_unmap_memory((void *)(uintptr_t)base,bytes);t->magic=0;free(t);return (int)-id;}
 __atomic_store_n(&t->tid,(uint32_t)id,__ATOMIC_RELEASE);*out=(pthread_t)(uintptr_t)t;return 0;
}
int pthread_join(pthread_t thread,void **result){
 if(!thread)return ESRCH;chos_libc_tcb_t *t=(void *)(uintptr_t)thread;
 if(t==__chos_libc_tls())return EDEADLK;if(t->magic!=CHOS_TCB_MAGIC)return ESRCH;
 uint32_t zero=0;if(!__atomic_compare_exchange_n(&t->joining,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return EINVAL;
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
 __atomic_store_n(&t->joining,0,__ATOMIC_RELEASE);return rc;
}
