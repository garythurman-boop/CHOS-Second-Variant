#include "config.h"
#include "seed.h"
#include <wtf/Threading.h>
#include <wtf/RefPtr.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <fenv.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
using namespace WTF;
static bool fail(unsigned line){printf("[FAIL] actual WTF foreign GC line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static unsigned errors;
static void sleep(){if(chos_sleep_ticks(1))chos_exit(99);}
static void prepare(PauseSeed& s,unsigned mode,void *area){
 memset(&s,0,sizeof(s));s.mode=mode;
 for(unsigned i=0;i<10;i++)s.roots[i]=(uintptr_t)area+64+i*48;
 for(unsigned i=0;i<32;i++){s.lanes[i][0]=UINT64_C(0x8712340000000000)+i;s.lanes[i][1]=UINT64_C(0xafe9870000000000)+i*313;}
}
static void run_seed(PauseSeed& s){
 if(fesetround(FE_DOWNWARD)||feclearexcept(FE_ALL_EXCEPT)||feraiseexcept(FE_DIVBYZERO))++errors;
 asm volatile("mrs %0,fpcr\n\tmrs %1,fpsr":"=r"(s.fpcr),"=r"(s.fpsr));
 s.tid=Thread::currentID();pause_seed_run(&s);
}
static bool started(PauseSeed& s){for(unsigned i=0;!__atomic_load_n(&s.ready,__ATOMIC_ACQUIRE);i++){CHECK(i<1000);sleep();}return true;}
static bool snapshot(PauseSeed& s,Thread& target,ThreadSuspendLocker& locker){
 PlatformRegisters c{};CHECK(target.getRegisters(locker,c)==sizeof(c));
 CHECK(c.flags==14&&c.version==1&&c.size_bytes==832&&c.tid==s.tid&&target.id()==s.tid);
 CHECK(c.stack_bytes==131072&&target.stack().size()==c.stack_bytes&&c.sp>=c.stack_base&&c.sp<c.stack_base+c.stack_bytes);
 CHECK(c.pc>=(uintptr_t)pause_seed_active&&c.pc<(uintptr_t)pause_seed_end&&c.fpcr==s.fpcr&&c.fpsr==s.fpsr);
 for(unsigned i=0;i<10;i++)CHECK(c.x[i+19]==s.roots[i]);
 CHECK(!memcmp(c.q,s.lanes,sizeof(s.lanes)));return true;
}
static void stop(PauseSeed& s){__atomic_store_n(&s.stop,1,__ATOMIC_RELEASE);__atomic_store_n(&s.word,1,__ATOMIC_RELEASE);(void)chos_wake_word(&s.word,1);}
static bool gone(uint32_t tid){for(unsigned i=0;;i++){chos_thread_info_t t{};auto rc=chos_thread_status(tid,&t);if(rc==-ESRCH)return true;CHECK(!rc&&i<1000);sleep();}}
static bool phase(unsigned mode,bool detached,void* area){
 PauseSeed s;prepare(s,mode,area);
 auto target=Thread::create("CHOS.GC.Foreign"_s,[&]{run_seed(s);},ThreadType::Unknown,Thread::defaultQOS,Thread::SchedulingPolicy::Other,StackAllocationSpecification::RequestSize(131072));
 CHECK(started(s)&&target->id()==s.tid);if(detached)target->detach();
 {
  ThreadSuspendLocker locker;
  for(unsigned attempt=0;;attempt++){
   CHECK(attempt<100&&target->suspend(locker));PlatformRegisters c{};CHECK(target->getRegisters(locker,c)==sizeof(c));
   if(!mode || c.pc==(uintptr_t)pause_seed_wait_return){
    if(mode)CHECK(c.x[8]==61&&c.x[0]==(uintptr_t)&s.word&&c.x[1]==0&&c.x[2]==60000);break;
   }
   target->resume(locker);sleep();
  }
  CHECK(snapshot(s,target.get(),locker));auto n=s.counter;
  CHECK(target->suspend(locker));target->resume(locker);
  if(mode){__atomic_store_n(&s.word,1,__ATOMIC_RELEASE);CHECK(chos_wake_word(&s.word,1)==1);}
  sleep();CHECK(s.counter==n&&snapshot(s,target.get(),locker));stop(s);target->resume(locker);
 }
 if(detached)CHECK(gone(s.tid));else CHECK(!target->waitForCompletion());
 CHECK(!memcmp(s.roots,s.observedRoots,sizeof(s.roots))&&!memcmp(s.lanes,s.observedLanes,sizeof(s.lanes))&&s.observedFpcr==s.fpcr&&s.observedFpsr==s.fpsr);
 // Retain the actual WTF object after native pthread/stack disposal. No freed
 // native TCB can be consulted to find its stable target ID.
 {ThreadSuspendLocker locker;auto result=target->suspend(locker);CHECK(!result&&result.error()==ESRCH);}
 CHECK(!errors);return true;
}
static bool child(const char* mode){
 auto& current=Thread::currentSingleton();ThreadSuspendLocker locker;
 if(!strcmp(mode,"self")){current.suspend(locker);return fail(__LINE__);}
 long area=chos_map_memory(4096);CHECK(area>0);PauseSeed s;prepare(s,0,(void*)area);
 auto target=Thread::create("CHOS.GC.Refusal"_s,[&]{run_seed(s);},ThreadType::Unknown,Thread::defaultQOS,Thread::SchedulingPolicy::Other,StackAllocationSpecification::RequestSize(131072));
 CHECK(started(s));
 if(!strcmp(mode,"unheld-read")){PlatformRegisters c{};target->getRegisters(locker,c);}
 else target->suspend(locker); // Reduced control permission: fail closed, not skip a live stack.
 return fail(__LINE__);
}
static bool run(){
 auto& current=Thread::currentSingleton();CHECK(current.id()==Thread::currentID()&&current.id()==(uint32_t)chos_thread_id());
 long area=chos_map_memory(4096);CHECK(area>0);
 for(unsigned i=0;i<3;i++)CHECK(phase(0,false,(void*)area)&&phase(1,false,(void*)area)&&phase(0,true,(void*)area)&&phase(1,true,(void*)area));
 CHECK(!chos_unmap_memory((void*)area,4096));
 for(auto mode:{"self","unheld-read","denied-live"}){
  auto caps=CHOS_PROCESS_CAP_ALL;if(!strcmp(mode,"denied-live"))caps&=~CHOS_PROCESS_CAP(CHOS_SYS_THREAD_REAP);
  auto pid=chos_spawn_limited("/WTFGC.ELF",mode,caps);CHECK(pid>0&&chos_wait(pid)==134&&chos_getstatus(pid)==CHOS_STATUS_EXITED);
 }
 puts("[ OK ] actual WTF foreign suspend,registers,resume and stable-ID lifecycle passed");return true;
}
int main(int argc,char** argv){return (argc>1?child(argv[1]):run())?0:1;}
