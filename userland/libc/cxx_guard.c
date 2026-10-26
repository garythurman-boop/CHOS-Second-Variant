/* AArch64 Itanium ABI function-local initialization guards. No exceptions or
 * unwinding are provided. The compiler acquires the low guard byte; publication
 * is a release store of COMPLETE. Remaining bytes belong to this runtime.
 * Pending owners are tracked in the native TCB, not a truncated pthread_t. */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include "chos/tls_runtime.h"
#ifdef CHOS_HOST_TEST
long chos_guard_wait(volatile uint32_t *,uint32_t,uint64_t);
long chos_guard_wake(volatile uint32_t *,unsigned);
#else
#include "chos/user.h"
#define chos_guard_wait chos_wait_word
#define chos_guard_wake chos_wake_word
#endif
_Static_assert(__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__,"guard publication requires little-endian ABI");
#define COMPLETE UINT32_C(1)
#define PENDING UINT32_C(256)
#define WAITING UINT32_C(512)
static uint32_t *word(uint64_t *guard){
 if(!guard||((uintptr_t)guard&7u))abort();
 return (uint32_t *)(void *)guard;
}
static int owns(chos_libc_tcb_t *t,uint64_t *guard){
 for(unsigned i=0;i<t->guard_depth;i++)if(t->active_guards[i]==guard)return 1;
 return 0;
}
int __cxa_guard_acquire(uint64_t *guard){
 uint32_t *p=word(guard);chos_libc_tcb_t *t=__chos_libc_tls();
 for(;;){
  uint32_t state=__atomic_load_n(p,__ATOMIC_ACQUIRE);
  if(state==COMPLETE)return 0;
  if(state!=0&&state!=PENDING&&state!=(PENDING|WAITING))abort();
  if(state&PENDING){
   if(owns(t,guard))abort(); /* recursive initialization cannot make progress */
   uint32_t waiting=state|WAITING;
   if(!__atomic_compare_exchange_n(p,&state,waiting,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))continue;
   long rc=chos_guard_wait(p,waiting,60000);
   if(rc!=0&&rc!=-EAGAIN&&rc!=-ETIMEDOUT&&rc!=-EINTR)abort();
  }else{
   if(t->guard_depth>=CHOS_CXX_GUARD_DEPTH)abort();
   if(__atomic_compare_exchange_n(p,&state,PENDING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
    t->active_guards[t->guard_depth++]=guard;return 1;
   }
  }
 }
}
static void finish(chos_libc_tcb_t *t,uint64_t *guard,uint32_t next){
 uint32_t *p=word(guard);
 if(!t->guard_depth||t->active_guards[t->guard_depth-1]!=guard)abort();
 uint32_t old=__atomic_exchange_n(p,next,__ATOMIC_RELEASE);
 if(old!=PENDING&&old!=(PENDING|WAITING))abort();
 t->active_guards[--t->guard_depth]=0;
 if((old&WAITING)&&chos_guard_wake(p,8)<0)abort();
}
void __cxa_guard_release(uint64_t *guard){finish(__chos_libc_tls(),guard,COMPLETE);}
void __cxa_guard_abort(uint64_t *guard){finish(__chos_libc_tls(),guard,0);}
void __chos_cxx_guard_abandon(chos_libc_tcb_t *t){
 while(t->guard_depth)finish(t,t->active_guards[t->guard_depth-1],0);
}
