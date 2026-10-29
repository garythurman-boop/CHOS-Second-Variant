#include "config.h"
#include <wtf/ThreadingPrimitives.h>
#include <wtf/ApproximateTime.h>
#include <wtf/ContinuousTime.h>
#include <wtf/ContinuousApproximateTime.h>
#include <wtf/MonotonicTime.h>
#include <wtf/UnbarrieredMonotonicTime.h>
#include <array>
#include <atomic>
#include <cstring>
#include <numeric>
#include <thread>
#include <time.h>
#include <chos/user.h>
#include <chos/libc.h>
#if !OS(CHOS) || OS(UNIX) || OS(LINUX) || !USE(PTHREADS)
#error "Expected the real native CHOS platform profile, not a Unix identity"
#endif
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char b[80];unsigned n=0;const char *s="[FAIL] native WTF primitives line ";while(*s)b[n++]=*s++;char d[16];unsigned i=0;do{d[i++]=(char)('0'+line%10);line/=10;}while(line);while(i)b[n++]=d[--i];b[n++]='\n';chos_write(b,n);return 1;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static int numeric(){
 std::array<int,8> input,output;std::iota(input.begin(),input.end(),1);
 CHECK(std::accumulate(input.begin(),input.end(),0)==36);
 CHECK(std::reduce(input.begin(),input.end(),0)==36);
 CHECK(std::inner_product(input.begin(),input.end(),input.begin(),0)==204);
 CHECK(std::transform_reduce(input.begin(),input.end(),input.begin(),0)==204);
 std::partial_sum(input.begin(),input.end(),output.begin());CHECK((output==std::array<int,8>{1,3,6,10,15,21,28,36}));
 std::inclusive_scan(input.begin(),input.end(),output.begin());CHECK((output==std::array<int,8>{1,3,6,10,15,21,28,36}));
 std::exclusive_scan(input.begin(),input.end(),output.begin(),0);CHECK((output==std::array<int,8>{0,1,3,6,10,15,21,28}));
 std::adjacent_difference(input.begin(),input.end(),output.begin());for(int x:output)CHECK(x==1);
 std::transform_exclusive_scan(input.begin(),input.end(),output.begin(),0,std::plus<>{},[](int x){return x*2;});CHECK(output.front()==0&&output.back()==56);
 std::transform_inclusive_scan(input.begin(),input.end(),output.begin(),std::plus<>{},[](int x){return x*2;});CHECK(output.front()==2&&output.back()==72);
 volatile int lo=INT32_MIN,hi=INT32_MAX;CHECK(std::midpoint(lo,hi)==-1&&std::midpoint(hi,lo)==0);
 volatile double large=1e308;CHECK(std::midpoint(large,large)==large&&std::midpoint(-large,large)==0);
 CHECK(std::gcd(-18,24)==6&&std::lcm(18,24)==72&&std::midpoint(input.data(),input.data()+8)==input.data()+4);return 0;
}
static WTF::Mutex mutex;
static WTF::ThreadCondition ready_condition,start_condition;
static unsigned ready,counter;static bool release_workers;
static std::atomic<unsigned> failures,destructed;
static WTF::ThreadSpecificKey key;
static void destroy_value(void *p){if(p)++destructed;}
static int clocks();
static void worker(unsigned id){
 uintptr_t value=id+1;WTF::threadSpecificSet(key,reinterpret_cast<void *>(value));
 mutex.lock();++ready;ready_condition.signal();while(!release_workers)start_condition.wait(mutex);mutex.unlock();
 for(unsigned n=0;n<1000;n++){mutex.lock();++counter;mutex.unlock();if(!(n%37))std::this_thread::yield();}
 if(WTF::threadSpecificGet(key)!=reinterpret_cast<void *>(value)||numeric()||clocks())++failures;
}
static double seconds(clockid_t clock){timespec t;if(clock_gettime(clock,&t))return -1;return static_cast<double>(t.tv_sec)+t.tv_nsec/1e9;}
static int clocks(){
 double before=seconds(CLOCK_MONOTONIC),wall=seconds(CLOCK_REALTIME);CHECK(before>=0&&wall>1577836800);
 auto m=WTF::MonotonicTime::now();auto w=WTF::WallTime::now();
 CHECK(m.secondsSinceEpoch().value()>=before&&w.secondsSinceEpoch().value()>=wall);
 auto ns=WTF::currentTimeInNanoseconds();CHECK(ns>static_cast<WTF::Int128>(1577836800)*1000000000);
 CHECK(WTF::ApproximateTime::now().secondsSinceEpoch().value()>=before);
 CHECK(WTF::ContinuousTime::now().secondsSinceEpoch().value()>=before);
 CHECK(WTF::ContinuousApproximateTime::now().secondsSinceEpoch().value()>=before);
 CHECK(WTF::UnbarrieredMonotonicTime::now().secondsSinceEpoch().value()>=before);
 CHECK(!chos_sleep_ticks(2));CHECK(WTF::MonotonicTime::now()>m);return 0;
}
static bool signaled;
static void signal_later(){chos_sleep_ticks(2);mutex.lock();signaled=true;start_condition.signal();mutex.unlock();}
extern "C" int main(int argc,char **argv){
 if(argc>1){
  if(!std::strcmp(argv[1],"invalid-unlock")){say("[EXPECT] native WTF rejects unowned unlock\n");mutex.unlock();}
  else if(!std::strcmp(argv[1],"invalid-tls")){say("[EXPECT] native WTF rejects invalid TLS key\n");WTF::threadSpecificSet(WTF::InvalidThreadSpecificKey,nullptr);}
  else if(!std::strcmp(argv[1],"deny-wait")){say("[EXPECT] native WTF rejects denied wait\n");mutex.lock();start_condition.wait(mutex);}
  else if(!std::strcmp(argv[1],"deny-clock")){say("[EXPECT] native WTF rejects denied deadline clock\n");mutex.lock();start_condition.timedWait(mutex,WTF::WallTime::fromRawSeconds(2000000000));}
  else if(!std::strcmp(argv[1],"deny-wall-clock")){say("[EXPECT] native WTF rejects denied wall clock\n");(void)WTF::WallTime::now();}
  else if(!std::strcmp(argv[1],"deny-monotonic-clock")){say("[EXPECT] native WTF rejects denied monotonic clock\n");(void)WTF::MonotonicTime::now();}
  else if(!std::strcmp(argv[1],"deny-ns-clock")){say("[EXPECT] native WTF rejects denied nanosecond clock\n");(void)WTF::currentTimeInNanoseconds();}
  return fail(__LINE__);
 }
 CHECK(!numeric()&&!clocks());
 for(unsigned cycle=0;cycle<3;cycle++){
  WTF::threadSpecificKeyCreate(&key,destroy_value);CHECK(!WTF::threadSpecificGet(key));
  ready=counter=0;release_workers=false;mutex.lock();CHECK(!mutex.tryLock());
  std::thread threads[3];for(unsigned i=0;i<3;i++)threads[i]=std::thread(worker,i);
  while(ready!=3)ready_condition.wait(mutex);CHECK(!counter);release_workers=true;start_condition.broadcast();mutex.unlock();
  for(auto &t:threads)t.join();CHECK(counter==3000&&!failures&&destructed==(cycle+1)*3&&!WTF::threadSpecificGet(key));WTF::threadSpecificKeyDelete(key);
  mutex.lock();CHECK(!start_condition.timedWait(mutex,WTF::WallTime::nan()));
  CHECK(!start_condition.timedWait(mutex,-WTF::WallTime::infinity()));
  CHECK(!start_condition.timedWait(mutex,WTF::WallTime::fromRawSeconds(0)));
  double before=seconds(CLOCK_MONOTONIC),now=seconds(CLOCK_REALTIME);CHECK(before>=0&&now>0);
  CHECK(!start_condition.timedWait(mutex,WTF::WallTime::fromRawSeconds(now+.03)));
  double after=seconds(CLOCK_MONOTONIC);CHECK(after>=before+.02&&after-before<5);mutex.unlock();
  for(double deadline:{std::numeric_limits<double>::infinity(),0x1p63,seconds(CLOCK_REALTIME)+2}){
   mutex.lock();signaled=false;std::thread signaler(signal_later);
   while(!signaled)CHECK(start_condition.timedWait(mutex,WTF::WallTime::fromRawSeconds(deadline)));
   mutex.unlock();signaler.join();
  }
  CHECK(mutex.tryLock());mutex.unlock();
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 const char *path="/BIN/WTFPRIM.ELF";
 for(const char *mode:{"invalid-unlock","invalid-tls","deny-wait","deny-clock","deny-wall-clock","deny-monotonic-clock","deny-ns-clock"}){
  uint64_t caps=CHOS_PROCESS_CAP_ALL;
  if(!std::strcmp(mode,"deny-wait"))caps&=~CHOS_PROCESS_CAP(CHOS_SYS_FUTEX_WAIT);
  if(std::strstr(mode,"clock"))caps&=~CHOS_PROCESS_CAP(CHOS_SYS_CLOCK_INFO);
  long pid=chos_spawn_limited(path,mode,caps);CHECK(pid>0&&chos_wait(static_cast<uint32_t>(pid))==134&&chos_getstatus(static_cast<uint32_t>(pid))==CHOS_STATUS_EXITED);
 }
 say("[ OK ] native WTF mutex, condition, TLS and original numeric integration passed\n");return 0;
}
