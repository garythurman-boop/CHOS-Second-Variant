#include <assert.h>
#include <string.h>
#include "chos/thread_context.h"
#include "chos/thread_api.h"
#include "chos/address_space.h"
static chos_thread_context_t output,unchanged;
static void reject(chos_irq_context_t *f,uint32_t tid,uint64_t base,uint64_t bytes){
 memset(&output,0xa5,sizeof(output));unchanged=output;assert(!chos_thread_context_capture(f,tid,base,bytes,&output));assert(!memcmp(&output,&unchanged,sizeof(output)));
}
int main(void){
 uint64_t base=CHOS_AS_USER_VA_BASE+4096,bytes=32768;
 chos_irq_context_t f={.sp_el0=base+bytes,.elr=CHOS_AS_USER_VA_BASE+16,.spsr=0,.fpcr=0x400000,.fpsr=0x10,.esr=UINT64_MAX,.reserved=UINT64_MAX,.tpidr_el0=UINT64_MAX,.thread_reserved=UINT64_MAX};
 for(unsigned i=0;i<31;i++)f.x[i]=UINT64_C(0xab00000000)+i;
 for(unsigned i=0;i<32;i++){f.q[i][0]=UINT64_C(0x1100000000)+i;f.q[i][1]=UINT64_C(0x2200000000)+i;}
 assert(chos_thread_context_capture(&f,7,base,bytes,&output));
 assert(output.version==1&&output.size_bytes==832&&output.tid==7&&output.flags==7&&!output.reserved);
 assert(output.stack_base==base&&output.stack_bytes==bytes&&output.pc==f.elr&&output.sp==f.sp_el0&&output.fpcr==f.fpcr&&output.fpsr==f.fpsr);
 assert(!memcmp(output.x,f.x,sizeof(f.x))&&!memcmp(output.q,f.q,sizeof(f.q)));
 reject(0,7,base,bytes);reject(&f,0,base,bytes);reject(&f,7,base+1,bytes);reject(&f,7,base,bytes+1);
 reject(&f,7,base,0);reject(&f,7,0,bytes);reject(&f,7,UINT64_MAX-4095,bytes);reject(&f,7,base,CHOS_THREAD_STACK_MAX+4096);
 reject(&f,7,CHOS_AS_USER_VA_LIMIT-4096,8192);
 f.spsr=5;reject(&f,7,base,bytes);f.spsr=0;
 f.sp_el0=base-16;reject(&f,7,base,bytes);f.sp_el0=base+bytes+16;reject(&f,7,base,bytes);f.sp_el0=base+1;reject(&f,7,base,bytes);f.sp_el0=base;
 f.elr=0;reject(&f,7,base,bytes);f.elr=CHOS_AS_USER_VA_LIMIT;reject(&f,7,base,bytes);f.elr=CHOS_AS_USER_VA_BASE+1;reject(&f,7,base,bytes);
 f.elr=CHOS_AS_USER_VA_BASE;assert(chos_thread_context_capture(&f,1,base,bytes,&output));assert(!chos_thread_context_capture(&f,1,base,bytes,0));return 0;
}
