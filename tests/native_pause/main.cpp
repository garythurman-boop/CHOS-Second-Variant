#include "seed.h"
#include <chos/user.h>
#include <chos/libc.h>
#include <fenv.h>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cstdlib>
static_assert(CHOS_SYS_FUTEX_WAIT==61 && CHOS_SYSCALL_LIMIT==82);
static bool fail(unsigned line){printf("[FAIL] native pause line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static std::atomic<unsigned> errors;
static void sleep(){if(chos_sleep_ticks(1))chos_exit(99);}
static void prepare(PauseSeed& s,unsigned mode,void *area){
 memset(&s,0,sizeof(s));s.mode=mode;
 for(unsigned i=0;i<10;i++)s.roots[i]=(uintptr_t)area+16+i*32;
 for(unsigned i=0;i<32;i++){s.lanes[i][0]=UINT64_C(0x8712340000000000)+i;s.lanes[i][1]=UINT64_C(0xafe9870000000000)+i*313;}
}
static void worker(PauseSeed& s){
 if(fesetround(FE_UPWARD)||feclearexcept(FE_ALL_EXCEPT)||feraiseexcept(FE_INVALID))++errors;
 asm volatile("mrs %0,fpcr\n\tmrs %1,fpsr":"=r"(s.fpcr),"=r"(s.fpsr));
 s.tid=chos_thread_id();pause_seed_run(&s);
}
static bool started(PauseSeed& s){for(unsigned i=0;!__atomic_load_n(&s.ready,__ATOMIC_ACQUIRE);i++){CHECK(i<1000);sleep();}return true;}
static bool snapshot(PauseSeed& s,chos_thread_context_t& c){
 CHECK(!chos_thread_paused_context(s.tid,&c));
 CHECK(c.version==1&&c.size_bytes==sizeof(c)&&c.tid==s.tid&&c.flags==14);
 CHECK(c.sp>=c.stack_base&&c.sp<c.stack_base+c.stack_bytes&&!(c.sp&15));
 CHECK(c.pc>=(uintptr_t)pause_seed_active&&c.pc<(uintptr_t)pause_seed_end);
 CHECK(c.fpcr==s.fpcr&&c.fpsr==s.fpsr);
 for(unsigned i=0;i<10;i++)CHECK(c.x[i+19]==s.roots[i]);
 CHECK(!memcmp(c.q,s.lanes,sizeof(s.lanes)));return true;
}
static bool restored(PauseSeed& s){CHECK(!memcmp(s.roots,s.observedRoots,sizeof(s.roots))&&!memcmp(s.lanes,s.observedLanes,sizeof(s.lanes)));CHECK(s.fpcr==s.observedFpcr&&s.fpsr==s.observedFpsr);return true;}
static void stop(PauseSeed& s){__atomic_store_n(&s.stop,1,__ATOMIC_RELEASE);__atomic_store_n(&s.word,1,__ATOMIC_RELEASE);(void)chos_wake_word(&s.word,1);}
static bool phase(unsigned mode,void *area){
 PauseSeed s;prepare(s,mode,area);std::thread target([&]{worker(s);});CHECK(started(s));
 chos_thread_context_t c{},again{};
 for(unsigned attempt=0;;attempt++){
  CHECK(attempt<100&&!chos_thread_suspend(s.tid)&&snapshot(s,c));
  if(!mode || c.pc==(uintptr_t)pause_seed_wait_return)break;
  CHECK(!chos_thread_resume(s.tid));sleep();
 }
 if(mode)CHECK(c.x[8]==61&&c.x[0]==(uintptr_t)&s.word&&c.x[1]==0&&c.x[2]==60000);
 auto counter=s.counter;
 CHECK(chos_thread_suspend(chos_thread_id())==-EDEADLK);
 // Wake while frozen: the wait result becomes ready but must not dispatch.
 if(mode){__atomic_store_n(&s.word,1,__ATOMIC_RELEASE);CHECK(chos_wake_word(&s.word,1)==1);}
 for(unsigned i=1;i<32;i++)CHECK(!chos_thread_suspend(s.tid));
 CHECK(chos_thread_suspend(s.tid)==-EOVERFLOW);
 CHECK(chos_syscall3(CHOS_SYS_THREAD_PAUSED_CONTEXT,s.tid,1,sizeof(c))==-EFAULT);
 std::thread competing([&]{if(chos_thread_suspend(s.tid)!=-EBUSY||chos_thread_resume(s.tid)!=-EPERM||chos_thread_paused_context(s.tid,&again)!=-EPERM)++errors;});competing.join();
 for(unsigned i=0;i<31;i++){CHECK(!chos_thread_resume(s.tid));sleep();CHECK(s.counter==counter);}
 CHECK(snapshot(s,again)&&!memcmp(&c,&again,sizeof(c)));
 c.x[19]=0;c.pc=0;CHECK(snapshot(s,again)&&again.x[19]==s.roots[0]&&again.pc);
 stop(s);CHECK(!chos_thread_resume(s.tid));target.join();CHECK(restored(s)&&!errors);
 CHECK(chos_thread_suspend(s.tid)==-ESRCH && chos_thread_resume(s.tid)==-ESRCH);
 return true;
}
static bool main_pause(void *area){
 PauseSeed s;prepare(s,0,area);s.tid=chos_thread_id();
 std::thread controller([&]{if(!started(s)||chos_thread_suspend(s.tid)){++errors;stop(s);return;}chos_thread_context_t c{};if(!snapshot(s,c))++errors;auto n=s.counter;sleep();if(s.counter!=n)++errors;stop(s);if(chos_thread_resume(s.tid))++errors;});
 if(fesetround(FE_UPWARD)||feclearexcept(FE_ALL_EXCEPT)||feraiseexcept(FE_INVALID))return false;
 asm volatile("mrs %0,fpcr\n\tmrs %1,fpsr":"=r"(s.fpcr),"=r"(s.fpsr));pause_seed_run(&s);controller.join();CHECK(restored(s)&&!errors);return true;
}
static bool owner_exit(void *area){
 PauseSeed s;prepare(s,0,area);std::thread target([&]{worker(s);});CHECK(started(s));
 std::atomic<unsigned> frozen{},release{};
 std::thread controller([&]{if(chos_thread_suspend(s.tid))++errors;frozen=1;while(!release)sleep();});
 while(!frozen)sleep();auto n=s.counter;sleep();CHECK(s.counter==n&&chos_thread_suspend(s.tid)==-EBUSY);release=1;controller.join();
 for(unsigned i=0;s.counter==n;i++){CHECK(i<1000);sleep();}
 stop(s);target.join();CHECK(restored(s)&&!errors);return true;
}
static bool foreign_child(uint32_t id){
 chos_thread_context_t c{};CHECK(chos_thread_suspend(id)==-ESRCH&&chos_thread_resume(id)==-ESRCH&&chos_thread_paused_context(id,&c)==-ESRCH);return true;
}
static bool live_child(const char* mode){
 long area=chos_map_memory(4096);CHECK(area>0);PauseSeed a,b;prepare(a,0,(void*)area);prepare(b,0,(void*)area);
 std::thread first([&]{worker(a);}),second([&]{worker(b);});CHECK(started(a)&&started(b));
 if(!strcmp(mode,"deny")){
  CHECK(chos_thread_suspend(a.tid)==-EPERM&&chos_thread_resume(a.tid)==-EPERM);chos_thread_context_t c{};CHECK(chos_thread_paused_context(a.tid,&c)==-EPERM);chos_exit(0);
 }
 CHECK(!chos_thread_suspend(a.tid)&&!chos_thread_suspend(b.tid));
 if(!strcmp(mode,"fault")){*(volatile uint64_t*)(uintptr_t)0x20000000=0;return false;}
 chos_exit(23);return false;
}
static bool child_ownership(void* area){
 PauseSeed s;prepare(s,0,area);std::thread target([&]{worker(s);});CHECK(started(s)&&!chos_thread_suspend(s.tid));
 char args[48];snprintf(args,sizeof(args),"foreign %u",(unsigned)s.tid);long pid=chos_spawn_limited("/PAUSE.ELF",args,CHOS_PROCESS_CAP_ALL);CHECK(pid>0&&chos_wait(pid)==0);
 stop(s);CHECK(!chos_thread_resume(s.tid));target.join();CHECK(restored(s));
 for(auto mode:{"deny","exit","fault"}){
  auto caps=CHOS_PROCESS_CAP_ALL;if(!strcmp(mode,"deny"))caps&=~CHOS_PROCESS_CAP(CHOS_SYS_THREAD_REAP);
  pid=chos_spawn_limited("/PAUSE.ELF",mode,caps);CHECK(pid>0);auto result=chos_wait(pid);
  CHECK(result==(!strcmp(mode,"deny")?0:!strcmp(mode,"exit")?23:-164));
  if(!strcmp(mode,"fault"))CHECK(chos_getstatus(pid)==CHOS_STATUS_FAILED);
 }
 return true;
}
static bool run(){
 CHECK(chos_thread_suspend(0)==-EINVAL);long area=chos_map_memory(4096);CHECK(area>0);
 for(unsigned cycle=0;cycle<3;cycle++){CHECK(phase(0,(void*)area)&&phase(1,(void*)area)&&main_pause((void*)area)&&owner_exit((void*)area));}
 CHECK(child_ownership((void*)area));
 CHECK(!chos_unmap_memory((void*)area,4096));chos_heap_stats_t h{};CHECK(!chos_libc_heap_info(&h)&&!h.allocations&&!h.mapped_bytes);
 puts("[ OK ] native foreign pause real GPR/SIMD,blocked wake,ownership and resume passed");return true;
}
int main(int argc,char** argv){if(argc>1){if(!strcmp(argv[1],"foreign"))return argc==3&&foreign_child(strtoul(argv[2],nullptr,10))?0:1;return live_child(argv[1])?0:1;}return run()?0:1;}
