#include "chos/thread_context.h"
#include "chos/thread_api.h"
#include "chos/address_space.h"
int chos_thread_context_capture(const chos_irq_context_t *frame,uint32_t tid,uint64_t base,uint64_t bytes,chos_thread_context_t *out){
 if(!frame||!out||!tid||(frame->spsr&15u)||!bytes||(base&4095u)||(bytes&4095u)||bytes>CHOS_THREAD_STACK_MAX||
    base<CHOS_AS_USER_VA_BASE||base>=CHOS_AS_USER_VA_LIMIT||bytes>CHOS_AS_USER_VA_LIMIT-base||
    (frame->sp_el0&15u)||frame->sp_el0<base||frame->sp_el0>base+bytes||
    (frame->elr&3u)||frame->elr<CHOS_AS_USER_VA_BASE||frame->elr>CHOS_AS_USER_VA_LIMIT-4u)return 0;
 chos_thread_context_t value={.version=CHOS_THREAD_CONTEXT_VERSION,.size_bytes=sizeof(value),.tid=tid,
  .flags=CHOS_THREAD_CONTEXT_CURRENT|CHOS_THREAD_CONTEXT_REGISTERS_VALID|CHOS_THREAD_CONTEXT_STACK_VALID,
  .stack_base=base,.stack_bytes=bytes,.pc=frame->elr,.sp=frame->sp_el0,.fpcr=frame->fpcr,.fpsr=frame->fpsr};
 for(unsigned i=0;i<31;i++)value.x[i]=frame->x[i];
 for(unsigned i=0;i<32;i++){value.q[i][0]=frame->q[i][0];value.q[i][1]=frame->q[i][1];}
 *out=value;return 1;
}
