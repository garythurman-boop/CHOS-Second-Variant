#include "config.h"
#include <JavaScriptCore/RegisterState.h>
#include <JavaScriptCore/MachineContext.h>
#include <JavaScriptCore/ProfilerSupport.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#if !OS(CHOS) || !CPU(ARM64)
#error "This fixture requires the actual native ARM64 register backend"
#endif
static_assert(sizeof(JSC::RegisterState)==10*sizeof(uintptr_t));
struct Snapshot {uint64_t regs[10],stack[512],count,sp,ceiling,kernel_sp;uint32_t tid;};
extern "C" int capture_seeded_registers(const uintptr_t*,Snapshot*);
extern "C" __attribute__((noinline)) int capture_actual_registers(Snapshot *out,uintptr_t ceiling){
 // This is the original ARM64 register-capture macro, not a replacement spill.
 JSC::ALLOCATE_AND_GET_REGISTER_STATE(registers);
 uintptr_t sp;asm volatile("mov %0,sp":"=r"(sp));
 if(ceiling<sp||(ceiling-sp)%8||(ceiling-sp)/8>512)return 0;
 memcpy(out->regs,&registers,sizeof(registers));
 out->count=(ceiling-sp)/8;out->sp=sp;out->ceiling=ceiling;
 uintptr_t first=(uintptr_t)&registers,last=first+sizeof(registers);
 // Capture before asking the kernel for its independent context: that context
 // must not populate this scan with a second source of the expected roots.
 // Exclude the RegisterState object itself so this scan observes other spills.
 for(uint64_t i=0;i<out->count;i++){
  uintptr_t at=sp+i*8;
  out->stack[i]=at>=first&&at<last?0:*(const volatile uint64_t*)at;
 }
 chos_thread_context_t context{};if(chos_thread_context(&context))return 0;
 unsigned flags=CHOS_THREAD_CONTEXT_CURRENT|CHOS_THREAD_CONTEXT_REGISTERS_VALID|CHOS_THREAD_CONTEXT_STACK_VALID;
 if(context.version!=CHOS_THREAD_CONTEXT_VERSION||context.size_bytes!=sizeof(context)||(context.flags&flags)!=flags||!context.tid)return 0;
 if(sp<context.stack_base||ceiling>context.stack_base+context.stack_bytes)return 0;
 if(context.sp<context.stack_base||context.sp>context.stack_base+context.stack_bytes||JSC::MachineContext::stackPointer<uintptr_t>(context)!=context.sp)return 0;
 out->kernel_sp=context.sp;out->tid=context.tid;
 return 1;
}
static bool fail(unsigned line){printf("[FAIL] actual JSC register platform line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static std::atomic<unsigned> errors;
static std::atomic<uint32_t> ids[3];
static bool run_one(unsigned salt,uint32_t *id){
 long area=chos_map_memory(65536);CHECK(area>0);auto *bytes=(unsigned char*)(uintptr_t)area;
 uintptr_t roots[10];for(unsigned i=0;i<10;i++){roots[i]=(uintptr_t)bytes+(((i*307u+salt*167u)%59000u)&~uintptr_t(15));*(unsigned char*)roots[i]=(unsigned char)(0x90u+i);}
 Snapshot snapshot{};CHECK(capture_seeded_registers(roots,&snapshot)&&snapshot.count&&snapshot.ceiling>=snapshot.sp);
 unsigned in_registers=0,in_stack=0;
 for(unsigned i=0;i<10;i++){
  bool reg=false,stack=false;for(auto v:snapshot.regs)if(v==roots[i])reg=true;
  for(uint64_t j=0;j<snapshot.count;j++)if(snapshot.stack[j]==roots[i])stack=true;
  CHECK(reg||stack);in_registers+=reg;in_stack+=stack;CHECK(*(unsigned char*)roots[i]==(unsigned char)(0x90u+i));
 }
 CHECK(in_registers&&in_registers+in_stack>=10);
 auto tid=JSC::ProfilerSupport::getCurrentThreadID();CHECK(tid&&tid==snapshot.tid&&tid==(uint32_t)chos_thread_id());
 auto before=JSC::ProfilerSupport::generateTimestamp();CHECK(before);CHECK(!chos_sleep_ticks(1)&&JSC::ProfilerSupport::generateTimestamp()>=before);
 *id=tid;CHECK(!chos_unmap_memory(bytes,65536));return true;
}
static bool run(){
 uint32_t parent;CHECK(run_one(1,&parent));
 for(unsigned wave=0;wave<3;wave++){
  {std::thread a([wave]{uint32_t id;if(!run_one(11+wave,&id))++errors;else ids[0]=id;}),b([wave]{uint32_t id;if(!run_one(101+wave,&id))++errors;else ids[1]=id;}),c([wave]{uint32_t id;if(!run_one(201+wave,&id))++errors;else ids[2]=id;});a.join();b.join();c.join();}
  CHECK(!errors);for(unsigned i=0;i<3;i++){CHECK(ids[i]&&ids[i]!=parent);for(unsigned j=0;j<i;j++)CHECK(ids[i]!=ids[j]);}
  chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&!heap.allocations&&!heap.mapped_bytes);
 }
 puts("[ OK ] actual JSC ARM64 register roots,native SP and profiler thread IDs passed");return true;
}
int main(){return run()?0:1;}
