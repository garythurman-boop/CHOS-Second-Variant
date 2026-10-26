#include "chos/tls_runtime.h"
#include "chos/user.h"
static _Alignas(16) chos_libc_tcb_t main_tcb={.magic=CHOS_TCB_MAGIC,.main_thread=1};
chos_libc_tcb_t *__chos_libc_tls(void){
 chos_libc_tcb_t *t=chos_thread_tls();
 if(!t){
  t=&main_tcb;
  __asm__ volatile("msr tpidr_el0,%0\n\tisb"::"r"((uintptr_t)t):"memory");
 }
 /* A raw native thread may supply its own opaque TPIDR. The C runtime reserves
  * its own TCB layout; do not silently write errno into an unrelated object. */
 if(t->magic!=CHOS_TCB_MAGIC)chos_exit(126);
 return t;
}
int *__chos_errno_location(void){return &__chos_libc_tls()->error;}
