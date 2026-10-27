#include <pthread.h>
#include <errno.h>
#include "chos/tls_runtime.h"
#ifdef CHOS_PTHREAD_HOST_TEST
extern long chos_wait_word(volatile uint32_t *,uint32_t,uint64_t);
extern long chos_wake_word(volatile uint32_t *,unsigned);
#else
#include "chos/user.h"
#endif
#define MUTEX_ATTR_MAGIC UINT32_C(0x4d415454)
#define COND_ATTR_MAGIC UINT32_C(0x43415454)
static int mutex_valid(const pthread_mutex_t *m){return m&&m->magic==CHOS_MUTEX_MAGIC&&m->type>=0&&m->type<=PTHREAD_MUTEX_ERRORCHECK;}
static uintptr_t self_id(void){return (uintptr_t)__chos_libc_tls();}
int pthread_mutexattr_init(pthread_mutexattr_t *a){if(!a)return EINVAL;*a=(pthread_mutexattr_t){MUTEX_ATTR_MAGIC,PTHREAD_MUTEX_NORMAL};return 0;}
int pthread_mutexattr_destroy(pthread_mutexattr_t *a){if(!a||a->magic!=MUTEX_ATTR_MAGIC)return EINVAL;a->magic=0;return 0;}
int pthread_mutexattr_settype(pthread_mutexattr_t *a,int type){if(!a||a->magic!=MUTEX_ATTR_MAGIC||type<0||type>PTHREAD_MUTEX_ERRORCHECK)return EINVAL;a->type=type;return 0;}
int pthread_mutexattr_gettype(const pthread_mutexattr_t *a,int *out){if(!a||a->magic!=MUTEX_ATTR_MAGIC||!out)return EINVAL;*out=a->type;return 0;}
int pthread_mutexattr_setpshared(pthread_mutexattr_t *a,int type){if(!a||a->magic!=MUTEX_ATTR_MAGIC)return EINVAL;return type==PTHREAD_PROCESS_PRIVATE?0:type==PTHREAD_PROCESS_SHARED?ENOTSUP:EINVAL;}
int pthread_mutex_init(pthread_mutex_t *m,const pthread_mutexattr_t *a){
 if(!m||(a&&a->magic!=MUTEX_ATTR_MAGIC))return EINVAL;
 *m=(pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;if(a)m->type=a->type;return 0;
}
int pthread_mutex_destroy(pthread_mutex_t *m){if(!mutex_valid(m))return EINVAL;if(__atomic_load_n(&m->state,__ATOMIC_ACQUIRE))return EBUSY;m->magic=0;return 0;}
static int recursive(pthread_mutex_t *m,uintptr_t self,int try_only){
 if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)!=self)return -1;
 if(m->type==PTHREAD_MUTEX_RECURSIVE){if(m->depth==UINT32_MAX)return EAGAIN;++m->depth;return 0;}
 return try_only?EBUSY:m->type==PTHREAD_MUTEX_ERRORCHECK?EDEADLK:-1;
}
static void acquired(pthread_mutex_t *m,uintptr_t self){__atomic_store_n(&m->owner,self,__ATOMIC_RELAXED);m->depth=1;}
int pthread_mutex_trylock(pthread_mutex_t *m){
 if(!mutex_valid(m))return EINVAL;uintptr_t self=self_id();int r=recursive(m,self,1);if(r>=0)return r;
 uint32_t zero=0;if(!__atomic_compare_exchange_n(&m->state,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return EBUSY;
 acquired(m,self);return 0;
}
int pthread_mutex_lock(pthread_mutex_t *m){
 if(!mutex_valid(m))return EINVAL;uintptr_t self=self_id();int r=recursive(m,self,0);if(r>=0)return r;
 uint32_t zero=0;
 if(__atomic_compare_exchange_n(&m->state,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){acquired(m,self);return 0;}
 for(;;){
  if(!__atomic_exchange_n(&m->state,2,__ATOMIC_ACQUIRE)){acquired(m,self);return 0;}
  long rc=chos_wait_word(&m->state,2,60000);
  if(rc&&rc!=-EAGAIN&&rc!=-ETIMEDOUT)return (int)-rc;
 }
}
int pthread_mutex_unlock(pthread_mutex_t *m){
 if(!mutex_valid(m))return EINVAL;
 if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)!=self_id())return EPERM;
 if(m->depth>1){--m->depth;return 0;}
 m->depth=0;__atomic_store_n(&m->owner,0,__ATOMIC_RELAXED);
 if(__atomic_fetch_sub(&m->state,1,__ATOMIC_RELEASE)==1)return 0;
 __atomic_store_n(&m->state,0,__ATOMIC_RELEASE);long rc=chos_wake_word(&m->state,1);return rc<0?(int)-rc:0;
}
int pthread_condattr_init(pthread_condattr_t *a){if(!a)return EINVAL;*a=(pthread_condattr_t){COND_ATTR_MAGIC,CLOCK_REALTIME};return 0;}
int pthread_condattr_destroy(pthread_condattr_t *a){if(!a||a->magic!=COND_ATTR_MAGIC)return EINVAL;a->magic=0;return 0;}
int pthread_condattr_setclock(pthread_condattr_t *a,clockid_t id){if(!a||a->magic!=COND_ATTR_MAGIC||(id!=CLOCK_REALTIME&&id!=CLOCK_MONOTONIC))return EINVAL;a->clock=id;return 0;}
int pthread_condattr_getclock(const pthread_condattr_t *a,clockid_t *out){if(!a||a->magic!=COND_ATTR_MAGIC||!out)return EINVAL;*out=a->clock;return 0;}
int pthread_condattr_setpshared(pthread_condattr_t *a,int type){if(!a||a->magic!=COND_ATTR_MAGIC)return EINVAL;return type==PTHREAD_PROCESS_PRIVATE?0:type==PTHREAD_PROCESS_SHARED?ENOTSUP:EINVAL;}
int pthread_cond_init(pthread_cond_t *c,const pthread_condattr_t *a){if(!c||(a&&a->magic!=COND_ATTR_MAGIC))return EINVAL;*c=(pthread_cond_t)PTHREAD_COND_INITIALIZER;if(a)c->clock=a->clock;return 0;}
int pthread_cond_destroy(pthread_cond_t *c){if(!c||c->magic!=CHOS_COND_MAGIC)return EINVAL;if(__atomic_load_n(&c->waiters,__ATOMIC_ACQUIRE))return EBUSY;c->magic=0;return 0;}
static int notify(pthread_cond_t *c,unsigned n){
 if(!c||c->magic!=CHOS_COND_MAGIC)return EINVAL;
 __atomic_add_fetch(&c->sequence,1,__ATOMIC_RELEASE);long rc=chos_wake_word(&c->sequence,n);return rc<0?(int)-rc:0;
}
int pthread_cond_signal(pthread_cond_t *c){return notify(c,1);}
int pthread_cond_broadcast(pthread_cond_t *c){return notify(c,8);}
static int remaining(clockid_t clock,const struct timespec *end,uint64_t *ticks){
 struct timespec now;if(clock_gettime(clock,&now))return errno;
 if(end->tv_sec<now.tv_sec||(end->tv_sec==now.tv_sec&&end->tv_nsec<=now.tv_nsec)){*ticks=0;return 0;}
 uint64_t sec=(uint64_t)end->tv_sec-(uint64_t)now.tv_sec;long ns=end->tv_nsec-now.tv_nsec;
 if(ns<0){--sec;ns+=1000000000L;}
 *ticks=sec>=600u?60000u:sec*100u+((unsigned long)ns+9999999u)/10000000u;
 if(!*ticks)*ticks=1;return 0;
}
static int wait_cond(pthread_cond_t *c,pthread_mutex_t *m,const struct timespec *end,clockid_t clock){
 if(!c||c->magic!=CHOS_COND_MAGIC||!mutex_valid(m))return EINVAL;
 if(__atomic_load_n(&m->owner,__ATOMIC_RELAXED)!=self_id())return EPERM;
 if(m->depth!=1)return EINVAL;
 if(end&&(end->tv_nsec<0||end->tv_nsec>=1000000000L))return EINVAL;
 uint64_t ticks=60000;int rc=end?remaining(clock,end,&ticks):0;if(rc)return rc;if(!ticks)return ETIMEDOUT;
 uint32_t sequence=__atomic_load_n(&c->sequence,__ATOMIC_ACQUIRE);
 __atomic_add_fetch(&c->waiters,1,__ATOMIC_RELAXED);
 rc=pthread_mutex_unlock(m);if(rc){__atomic_sub_fetch(&c->waiters,1,__ATOMIC_RELAXED);int restore=pthread_mutex_lock(m);return restore?restore:rc;}
 for(;;){
  long result=chos_wait_word(&c->sequence,sequence,ticks);
  if(result==0||result==-EAGAIN){rc=0;break;}
  if(result!=-ETIMEDOUT){rc=(int)-result;break;}
  if(end){rc=remaining(clock,end,&ticks);if(rc||!ticks){if(!rc)rc=ETIMEDOUT;break;}}
 }
 __atomic_sub_fetch(&c->waiters,1,__ATOMIC_RELAXED);
 int relock=pthread_mutex_lock(m);return relock?relock:rc;
}
int pthread_cond_wait(pthread_cond_t *c,pthread_mutex_t *m){return wait_cond(c,m,0,CLOCK_REALTIME);}
int pthread_cond_timedwait(pthread_cond_t *c,pthread_mutex_t *m,const struct timespec *end){return end&&c?wait_cond(c,m,end,c->clock):EINVAL;}
int pthread_cond_clockwait(pthread_cond_t *c,pthread_mutex_t *m,clockid_t clock,const struct timespec *end){
 if(!end||(clock!=CLOCK_REALTIME&&clock!=CLOCK_MONOTONIC))return EINVAL;
 return wait_cond(c,m,end,clock);
}
int pthread_once(pthread_once_t *once,void (*initialize)(void)){
 if(!once||!initialize||((uintptr_t)once&3u))return EINVAL;
 chos_libc_tcb_t *t=__chos_libc_tls();
 for(;;){
  uint32_t state=__atomic_load_n(once,__ATOMIC_ACQUIRE);if(state==2)return 0;if(state>2)return EINVAL;
  if(!state){
   if(t->once_depth==8)return EAGAIN;
   uint32_t expected=0;
   if(__atomic_compare_exchange_n(once,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
    t->active_once[t->once_depth++]=once;initialize();--t->once_depth;
    __atomic_store_n(once,2,__ATOMIC_RELEASE);long rc=chos_wake_word(once,8);return rc<0?(int)-rc:0;
   }
  }else {long rc=chos_wait_word(once,1,60000);if(rc&&rc!=-EAGAIN&&rc!=-ETIMEDOUT)return (int)-rc;}
 }
}
void __chos_once_abandon(chos_libc_tcb_t *t){
 while(t->once_depth){pthread_once_t *once=t->active_once[--t->once_depth];__atomic_store_n(once,0,__ATOMIC_RELEASE);(void)chos_wake_word(once,8);}
 while(t->cxx_once_depth){uint64_t *once=t->active_cxx_once[--t->cxx_once_depth];__atomic_store_n(once,0,__ATOMIC_RELEASE);(void)chos_wake_word((uint32_t *)(void *)once,8);}
}
/* libc++'s AArch64 once_flag is 64 bits, with all bits set for completion.
 * Use full-width acquire/release publication; only the futex wait uses the low
 * word on this explicitly little-endian ABI. Do not alias it to pthread_once_t.
 * Tracking owners permits pthread_exit to abandon initialization and wake peers.
 */
_Static_assert(__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__,"C++ once futex ABI requires little endian");
int __chos_cxx_execute_once(volatile uint64_t *flag,void *arg,void (*initialize)(void *)){
 if(!flag||((uintptr_t)flag&7u)||!initialize)return EINVAL;
 chos_libc_tcb_t *t=__chos_libc_tls();
 for(;;){
  uint64_t state=__atomic_load_n(flag,__ATOMIC_ACQUIRE);
  if(state==UINT64_MAX)return 0;
  if(state>1)return EINVAL;
  if(!state){
   if(t->cxx_once_depth==CHOS_CXX_ONCE_DEPTH)return EAGAIN;
   uint64_t expected=0;
   if(__atomic_compare_exchange_n(flag,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
    t->active_cxx_once[t->cxx_once_depth++]=(uint64_t *)(uintptr_t)flag;
    initialize(arg);
    --t->cxx_once_depth;
    __atomic_store_n(flag,UINT64_MAX,__ATOMIC_RELEASE);
    long rc=chos_wake_word((volatile uint32_t *)(uintptr_t)flag,8);
    return rc<0?(int)-rc:0;
   }
  }else{
   for(unsigned i=0;i<t->cxx_once_depth;i++)if(t->active_cxx_once[i]==(uint64_t *)(uintptr_t)flag)return EDEADLK;
   long rc=chos_wait_word((volatile uint32_t *)(uintptr_t)flag,1,60000);
   if(rc&&rc!=-EAGAIN&&rc!=-ETIMEDOUT&&rc!=-EINTR)return (int)-rc;
  }
 }
}
