#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include "chos/tls_runtime.h"
int __cxa_guard_acquire(uint64_t *);
void __cxa_guard_release(uint64_t *);
void __cxa_guard_abort(uint64_t *);
static chos_libc_tcb_t tcb;
chos_libc_tcb_t *__chos_libc_tls(void){return &tcb;}
static unsigned waits,wakes;
static long wait_rc,wake_rc;
static int publish;
static jmp_buf recovery;
void chos_test_abort(void){longjmp(recovery,1);}
long chos_guard_wait(volatile uint32_t *p,uint32_t expected,uint64_t timeout){
 assert(expected==768&&*p==expected&&timeout==60000);++waits;
 if(publish)__atomic_store_n(p,1,__ATOMIC_RELEASE);
 return wait_rc;
}
long chos_guard_wake(volatile uint32_t *p,unsigned count){assert((*p==0||*p==1)&&count==8);++wakes;return wake_rc;}
#define ABORTS(code) do{if(!setjmp(recovery)){code;assert(!"missing guard refusal");}}while(0)
int main(void){
 uint64_t guards[17]={0};
 assert(__cxa_guard_acquire(&guards[0])==1&&tcb.guard_depth==1&&guards[0]==256);
 __cxa_guard_abort(&guards[0]);assert(!guards[0]&&!tcb.guard_depth);
 assert(__cxa_guard_acquire(&guards[0])==1);__cxa_guard_release(&guards[0]);
 assert(guards[0]==1&&__cxa_guard_acquire(&guards[0])==0&&!tcb.guard_depth);
 guards[1]=256;publish=1;assert(__cxa_guard_acquire(&guards[1])==0&&waits==1&&!wakes);
 guards[1]=0;assert(__cxa_guard_acquire(&guards[1])==1);guards[1]|=512;__cxa_guard_release(&guards[1]);assert(wakes==1);
 guards[1]=0;assert(__cxa_guard_acquire(&guards[1])==1);guards[1]|=512;__cxa_guard_abort(&guards[1]);assert(wakes==2&&!guards[1]);
 assert(__cxa_guard_acquire(&guards[1])==1);ABORTS(__cxa_guard_acquire(&guards[1]));__cxa_guard_abort(&guards[1]);
 memset(guards,0,sizeof(guards));
 for(unsigned i=0;i<16;i++)assert(__cxa_guard_acquire(&guards[i])==1);
 assert(tcb.guard_depth==16);ABORTS(__cxa_guard_acquire(&guards[16]));
 guards[0]|=512;__chos_cxx_guard_abandon(&tcb);assert(!tcb.guard_depth&&wakes==3);
 for(unsigned i=0;i<17;i++)assert(!guards[i]);
 ABORTS(__cxa_guard_acquire(0));
 ABORTS(__cxa_guard_acquire((uint64_t *)((uintptr_t)guards+1)));
 guards[0]=2;ABORTS(__cxa_guard_acquire(&guards[0]));guards[0]=0;
 ABORTS(__cxa_guard_release(&guards[0]));
 int retry_errors[]={EAGAIN,ETIMEDOUT,EINTR};
 for(unsigned i=0;i<3;i++){guards[1]=256;publish=1;wait_rc=-retry_errors[i];assert(__cxa_guard_acquire(&guards[1])==0&&guards[1]==1);}
 guards[0]=256;publish=0;wait_rc=-EINVAL;ABORTS(__cxa_guard_acquire(&guards[0]));guards[0]=0;
 assert(__cxa_guard_acquire(&guards[0])==1);guards[0]|=512;wake_rc=-EINVAL;ABORTS(__cxa_guard_release(&guards[0]));assert(!tcb.guard_depth);
 return 0;
}
