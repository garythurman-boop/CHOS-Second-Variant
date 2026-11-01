#include <pthread.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fenv.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <chos/tls_runtime.h>
static bool fail(unsigned n){printf("[FAIL] native detached lifecycle line %u errno %d\n",n,errno);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
struct State {
 std::mutex mutex;std::condition_variable condition;bool notified=false;
 std::atomic<uint32_t> tid{},body{},stage{},keys{};uint32_t gate=0;unsigned mode=0;
};
static State states[3];static pthread_key_t key;static std::atomic<unsigned> errors;
static uint64_t expected_resident,expected_mapped;
static bool check_worker(bool ok){if(!ok)++errors;return ok;}
struct Witness {State *s;~Witness(){check_worker(s->stage==0);s->stage=1;}};
struct LateWitness {State *s;~LateWitness(){check_worker(s->stage==2&&s->keys==2);s->stage=3;}};
static void witness(State *s){thread_local Witness w{s};(void)w;}
static void late_witness(State *s){thread_local LateWitness w{s};(void)w;}
static void key_destructor(void *p){
 auto *s=static_cast<State*>(p);unsigned call=++s->keys;
 check_worker(s->stage==1);late_witness(s);
 if(call==1)check_worker(!pthread_setspecific(key,p));else {check_worker(call==2);s->stage=2;}
}
static void reset(State& s,unsigned mode){s.notified=false;s.tid=0;s.gate=0;s.body=0;s.stage=0;s.keys=0;s.mode=mode;}
static void *body(void *p){
 auto *s=static_cast<State*>(p);check_worker(fegetround()==FE_DOWNWARD);errno=73;
 witness(s);check_worker(!pthread_setspecific(key,s));
 if(s->mode==2){check_worker(!pthread_detach(pthread_self()));check_worker(pthread_detach(pthread_self())==EINVAL);}
 s->tid=(uint32_t)chos_thread_id();
 while(s->mode!=3&&!__atomic_load_n(&s->gate,__ATOMIC_ACQUIRE)){long rc=chos_wait_word(&s->gate,0,60000);check_worker(!rc||rc==-EAGAIN||rc==-ETIMEDOUT);}
 check_worker(errno==73&&fegetround()==FE_DOWNWARD);s->body=1;
 std::unique_lock lock(s->mutex);s->notified=true;std::notify_all_at_thread_exit(s->condition,std::move(lock));
 return reinterpret_cast<void*>(uintptr_t(0x515));
}
static bool started(State& s){for(unsigned n=0;!s.tid;n++){CHECK(n<1000&&!chos_sleep_ticks(1));}return true;}
static bool gone(uint32_t tid){
 for(unsigned n=0;;n++){
  chos_thread_info_t i;long rc=chos_thread_status(tid,&i);if(rc==-ESRCH)return true;
  CHECK(!rc&&n<1000&&!chos_sleep_ticks(1));
 }
}
static bool completed(State& s){
 {std::unique_lock lock(s.mutex);CHECK(s.condition.wait_for(lock,std::chrono::seconds(5),[&]{return s.notified;}));CHECK(s.stage==3&&s.keys==2);}
 CHECK(started(s)&&gone(s.tid)&&s.body==1&&!errors);return true;
}
static bool empty_heap(){
 chos_heap_stats_t h;CHECK(!chos_libc_heap_info(&h)&&!h.allocations&&!h.mapped_bytes);
 if(expected_resident){chos_resource_info_t i;CHECK(!chos_resource_info(&i)&&i.resident_bytes==expected_resident&&i.mapped_bytes==expected_mapped);}
 return true;
}
static void release(State& s){__atomic_store_n(&s.gate,1,__ATOMIC_RELEASE);if(chos_wake_word(&s.gate,1)<0)std::abort();}
static bool waves(unsigned mode){
 pthread_attr_t attr;CHECK(!pthread_attr_init(&attr));if(mode==0||mode==3)CHECK(!pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED));
 for(unsigned wave=0;wave<(mode==3?20u:3u);wave++){
  pthread_t handles[3];for(unsigned i=0;i<3;i++){reset(states[i],mode);CHECK(!pthread_create(&handles[i],&attr,body,&states[i]));}
  if(mode!=3){
   for(unsigned i=0;i<3;i++){
    CHECK(started(states[i]));if(mode==1)CHECK(!pthread_detach(handles[i]));
    CHECK(pthread_join(handles[i],nullptr)==EINVAL&&pthread_detach(handles[i])==EINVAL);
   }
   for(auto& s:states)release(s);
  }
  for(auto& s:states)CHECK(completed(s));CHECK(empty_heap());
 }
 CHECK(!pthread_attr_destroy(&attr));return true;
}
static bool after_exit(){
 for(unsigned wave=0;wave<6;wave++){
  reset(states[0],3);pthread_t h;CHECK(!pthread_create(&h,nullptr,body,&states[0])&&started(states[0]));
  for(unsigned n=0;;n++){chos_thread_info_t i;CHECK(!chos_thread_status(states[0].tid,&i));if(i.state==CHOS_THREAD_EXITED)break;CHECK(n<1000&&!chos_sleep_ticks(1));}
  CHECK(!pthread_detach(h)&&completed(states[0])&&empty_heap());
 }
 return true;
}
static bool cpp_detach(){
 for(unsigned wave=0;wave<6;wave++){
  for(auto& s:states)reset(s,1);
  {std::thread a([]{body(&states[0]);}),b([]{body(&states[1]);}),c([]{body(&states[2]);});a.detach();b.detach();c.detach();CHECK(!a.joinable()&&!b.joinable()&&!c.joinable());}
  for(auto& s:states){CHECK(started(s));release(s);}for(auto& s:states)CHECK(completed(s));CHECK(empty_heap());
 }
 return true;
}
struct Raw {uint32_t gate,tid;};static Raw raw;alignas(16) static unsigned char raw_tls[32];
static void raw_worker(void *p){
 auto *r=static_cast<Raw*>(p);__atomic_store_n(&r->tid,(uint32_t)chos_thread_id(),__ATOMIC_RELEASE);
 while(!__atomic_load_n(&r->gate,__ATOMIC_ACQUIRE)){long rc=chos_wait_word(&r->gate,0,60000);if(rc&&rc!=-EAGAIN&&rc!=-ETIMEDOUT)chos_exit(91);}
 chos_thread_exit(0x515);
}
static chos_thread_request_t raw_request(uint64_t stack,unsigned version,uint64_t flags){
 chos_thread_request_t r{};r.version=version;r.size_bytes=sizeof r;r.entry=(uintptr_t)raw_worker;r.stack_base=stack;r.stack_bytes=32768;r.tls=(uintptr_t)raw_tls;r.argument=(uintptr_t)&raw;r.reserved[0]=flags;return r;
}
static bool raw_cases(){
 CHECK(chos_syscall3(CHOS_SYS_THREAD_DETACH,UINT64_C(0x100000001),0,0)==-EINVAL);
 CHECK(chos_syscall3(CHOS_SYS_THREAD_DETACH,1,1,0)==-EINVAL&&chos_thread_detach(0)==-EINVAL);
 for(unsigned version=1;version<=2;version++){
  long stack=chos_map_memory(32768);CHECK(stack>0);raw={};auto r=raw_request(stack,version,version==2?CHOS_THREAD_MANAGED_STACK:0);
  long tid=chos_thread_create(&r);CHECK(tid>0);
  CHECK(chos_thread_detach(tid)==(version==1?-ENOTSUP:0));
  if(version==2)CHECK(chos_thread_collect(tid)==-EINVAL&&chos_thread_detach(tid)==-EINVAL);
  CHECK(chos_unmap_memory((void*)(uintptr_t)stack,32768)==-EBUSY);__atomic_store_n(&raw.gate,1,__ATOMIC_RELEASE);CHECK(chos_wake_word(&raw.gate,1)>=0);
  if(version==2)CHECK(gone(tid));
  else {
   for(unsigned n=0;;n++){chos_thread_info_t i;CHECK(!chos_thread_status(tid,&i));if(i.state==CHOS_THREAD_EXITED)break;CHECK(n<1000&&!chos_sleep_ticks(1));}
   CHECK(!chos_unmap_memory((void*)(uintptr_t)stack,32768));long reused=chos_map_memory(32768);CHECK(reused==stack);
   CHECK(chos_thread_detach(tid)==-ENOTSUP&&!chos_thread_collect(tid));
   CHECK(!chos_unmap_memory((void*)(uintptr_t)reused,32768));
  }
 }
 /* Fast detached exit may precede creation acknowledgement. Every actual TID
  * must still be returned, and stale operations cannot free a reused mapping. */
 for(unsigned n=0;n<24;n++){
  long stack=chos_map_memory(32768);CHECK(stack>0);raw={.gate=1};auto r=raw_request(stack,CHOS_THREAD_MANAGED_VERSION,CHOS_THREAD_MANAGED_STACK|CHOS_THREAD_CREATE_DETACHED);
  long tid=chos_thread_create(&r);CHECK(tid>0&&gone(tid));long reused=chos_map_memory(32768);CHECK(reused==stack);
  auto *p=(unsigned char*)(uintptr_t)reused;p[0]=0xa5;p[32767]=0x5a;
  CHECK(chos_thread_collect(tid)==-ESRCH&&chos_thread_detach(tid)==-ESRCH&&!chos_sleep_ticks(1)&&p[0]==0xa5&&p[32767]==0x5a);
  CHECK(!chos_unmap_memory(p,32768));
 }
 return true;
}
static bool setup(){CHECK(!pthread_key_create(&key,key_destructor)&&!fesetround(FE_DOWNWARD));errno=19;return true;}
static bool denied_reap(){
 CHECK(setup());pthread_attr_t a;CHECK(!pthread_attr_init(&a)&&!pthread_attr_setdetachstate(&a,PTHREAD_CREATE_DETACHED));
 reset(states[0],0);pthread_t h=0x515;
 CHECK(chos_syscall3(CHOS_SYS_THREAD_DETACH,0,0,0)==-EPERM);
 CHECK(pthread_create(&h,&a,body,&states[0])==EPERM&&h==0x515&&empty_heap());
 CHECK(!pthread_attr_setdetachstate(&a,PTHREAD_CREATE_JOINABLE)&&!pthread_create(&h,&a,body,&states[0])&&started(states[0]));
 CHECK(pthread_detach(h)==EPERM);release(states[0]);CHECK(pthread_join(h,nullptr)==EPERM);
 {std::unique_lock lock(states[0].mutex);CHECK(states[0].stage==3&&states[0].keys==2);}
 puts("[ OK ] managed detach capability refusals preserved ownership");return true;
}
static pthread_t main_handle;
static void *main_exit_worker(void*){
 // The native main TCB is static for the entire process. Observe its real
 // completion publication, after all TLS phases and the active-count update,
 // rather than letting a fast worker finish before main has actually exited.
 auto *main_tcb=reinterpret_cast<chos_libc_tcb_t*>(main_handle);
 for(unsigned n=0;states[0].stage!=3||!__atomic_load_n(&main_tcb->done,__ATOMIC_ACQUIRE);n++){if(n>=1000||chos_sleep_ticks(1))std::abort();}
 if(pthread_join(main_handle,nullptr)!=EINVAL||errors)std::abort();
 puts("[ OK ] detached main exit preserved worker and TLS cleanup");return nullptr;
}
static bool detached_main(){
 CHECK(setup());reset(states[0],0);witness(&states[0]);CHECK(!pthread_setspecific(key,&states[0]));
 main_handle=pthread_self();CHECK(!pthread_detach(main_handle)&&pthread_detach(main_handle)==EINVAL);
 pthread_attr_t a;CHECK(!pthread_attr_init(&a)&&!pthread_attr_setdetachstate(&a,PTHREAD_CREATE_DETACHED));
 pthread_t h;CHECK(!pthread_create(&h,&a,main_exit_worker,nullptr));pthread_exit(nullptr);
}
static bool live_exit(bool fault){
 CHECK(setup());pthread_attr_t a;CHECK(!pthread_attr_init(&a)&&!pthread_attr_setdetachstate(&a,PTHREAD_CREATE_DETACHED));
 for(unsigned i=0;i<3;i++){reset(states[i],0);pthread_t h;CHECK(!pthread_create(&h,&a,body,&states[i]));}
 for(auto& s:states)CHECK(started(s));
 puts(fault?"[ OK ] entering fault with three live detached workers":"[ OK ] exiting with three live detached workers");
 if(fault){uintptr_t address=0;asm volatile("":"+r"(address)::"memory");*reinterpret_cast<volatile unsigned char*>(address)=1;CHECK(false);}
 return true;
}
static bool foreign(uint32_t tid){
 chos_thread_info_t i;CHECK(tid&&chos_thread_status(tid,&i)==-ESRCH&&chos_thread_detach(tid)==-ESRCH&&chos_thread_collect(tid)==-ESRCH);
 puts("[ OK ] foreign thread ownership refused");return true;
}
static bool children(){
 reset(states[0],0);pthread_t h;CHECK(!pthread_create(&h,nullptr,body,&states[0])&&started(states[0]));
 char args[64];snprintf(args,sizeof args,"foreign %u",(unsigned)states[0].tid.load());long pid=chos_spawn_args("/DETACH.ELF",args);
 CHECK(pid>0&&chos_wait(pid)==0&&chos_getstatus(pid)==CHOS_STATUS_EXITED&&!pthread_detach(h));release(states[0]);CHECK(completed(states[0])&&empty_heap());
 for(const char *mode:{"deny-reap","main-detach","exit-live","fault-live"}){
  long child=!strcmp(mode,"deny-reap")?chos_spawn_limited("/DETACH.ELF",mode,CHOS_PROCESS_CAP_ALL&~CHOS_PROCESS_CAP(CHOS_SYS_THREAD_REAP)):chos_spawn_args("/DETACH.ELF",mode);
  CHECK(child>0);bool fault=!strcmp(mode,"fault-live");CHECK(chos_wait(child)==(fault?-164:0)&&chos_getstatus(child)==(fault?CHOS_STATUS_FAILED:CHOS_STATUS_EXITED));
 }
 return true;
}
static bool run(){
 CHECK(setup());chos_resource_info_t i;CHECK(!chos_resource_info(&i));expected_resident=i.resident_bytes;expected_mapped=i.mapped_bytes;
 CHECK(waves(0)&&waves(1)&&waves(2)&&after_exit()&&cpp_detach()&&waves(3)&&raw_cases()&&children());
 CHECK(errno==19&&fegetround()==FE_DOWNWARD&&!pthread_key_delete(key)&&empty_heap());
 puts("[ OK ] native managed detach,TLS ordering,stack ownership and repeated cleanup passed");return true;
}
int main(int argc,char **argv){
 if(argc>1){
  if(!strcmp(argv[1],"deny-reap"))return denied_reap()?0:1;
  if(!strcmp(argv[1],"main-detach"))return detached_main()?0:1;
  if(!strcmp(argv[1],"exit-live")||!strcmp(argv[1],"fault-live"))return live_exit(!strcmp(argv[1],"fault-live"))?0:1;
  if(argc==3&&!strcmp(argv[1],"foreign")){char *end;unsigned long tid=strtoul(argv[2],&end,10);return !*end&&tid<=UINT32_MAX&&foreign((uint32_t)tid)?0:1;}
  return 2;
 }
 return run()?0:1;
}
