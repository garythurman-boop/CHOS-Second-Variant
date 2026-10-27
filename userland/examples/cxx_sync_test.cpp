#include <mutex>
#include <condition_variable>
#include <atomic>
#include <__chrono/steady_clock.h>
#include <__chrono/system_clock.h>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include "chos/libc.h"
#include "chos/user.h"
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native C++ sync line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static std::mutex gate,counter_mutex;
static std::recursive_mutex recursive_mutex;
static std::timed_mutex timed_mutex;
static std::recursive_timed_mutex recursive_timed_mutex;
static std::condition_variable ready,go_condition;
static std::once_flag initialized,retry,destructor_retry,nested_flags[17],recursive_flag;
static unsigned arrived,go,counter,init_count,init_value;
static std::atomic<unsigned> attempts{},destructor_attempts{};
static pthread_key_t exit_key;
static void initialize(unsigned value){++init_count;chos_sleep_ticks(3);init_value=value;}
static void initialize_retry(){unsigned n=attempts.fetch_add(1);chos_sleep_ticks(3);if(!n)pthread_exit((void *)uintptr_t(0x201));}
static void initialize_destructor(){unsigned n=destructor_attempts.fetch_add(1);chos_sleep_ticks(3);if(!n)pthread_exit((void *)uintptr_t(0x301));}
static void key_destructor(void *){std::call_once(destructor_retry,initialize_destructor);}
static void *worker(void *p){
 unsigned index=(unsigned)(uintptr_t)p;
 std::call_once(initialized,initialize,73u);if(init_value!=73)return (void *)1;
 {
  std::unique_lock<std::mutex> lock(gate);++arrived;ready.notify_one();
  go_condition.wait(lock,[]{return go!=0;});
  if(!lock.owns_lock())return (void *)2;
 }
 for(unsigned i=0;i<100;i++){
  std::lock_guard<std::mutex> lock(counter_mutex);++counter;if(!(i&7))chos_yield();
 }
 {std::lock_guard<std::recursive_mutex> lock(recursive_mutex);if(!recursive_mutex.try_lock())return (void *)3;recursive_mutex.unlock();}
 {std::lock_guard<std::timed_mutex> lock(timed_mutex);}
 {std::lock_guard<std::recursive_timed_mutex> lock(recursive_timed_mutex);if(!recursive_timed_mutex.try_lock())return (void *)4;recursive_timed_mutex.unlock();}
 std::call_once(retry,initialize_retry);
 if(pthread_setspecific(exit_key,(void *)uintptr_t(index+1)))return (void *)5;
 return (void *)uintptr_t(0x100);
}
static void nested(unsigned n){std::call_once(nested_flags[n-1],[n]{if(n>1)nested(n-1);});}
static void recursive(){std::call_once(recursive_flag,recursive);}
static std::atomic<unsigned> timed_ready{};
static void *timed_worker(void *){
 using namespace std::chrono;timed_ready=1;
 if(timed_mutex.try_lock_for(milliseconds(40)))return (void *)1;
 if(recursive_timed_mutex.try_lock_until(steady_clock::now()+milliseconds(40)))return (void *)2;
 timed_ready=2;
 if(!timed_mutex.try_lock_for(seconds(2)))return (void *)3;
 timed_mutex.unlock();
 if(!recursive_timed_mutex.try_lock_until(steady_clock::now()+seconds(2)))return (void *)4;
 recursive_timed_mutex.unlock();return nullptr;
}
static std::atomic<unsigned> shared_destroyed{};
struct SharedRecord {unsigned value=91;~SharedRecord(){++shared_destroyed;}};
struct SharedContext {
 std::recursive_mutex mutex;std::condition_variable_any condition;
 std::shared_ptr<SharedRecord> owner;std::weak_ptr<SharedRecord> weak;
 unsigned arrived=0,go=0,count=0;
};
static void *shared_worker(void *p){
 auto& c=*static_cast<SharedContext *>(p);
 {std::unique_lock<std::recursive_mutex> lock(c.mutex);++c.arrived;c.condition.notify_one();
  c.condition.wait(lock,[&c]{return c.go!=0;});}
 for(unsigned i=0;i<100;i++){
  auto a=c.owner;auto b=c.weak.lock();if(!a||!b||a.get()!=b.get()||b->value!=91)return (void *)1;
  {std::lock_guard<std::recursive_mutex> lock(c.mutex);++c.count;}
  if(!(i&7))chos_yield();
 }
 return nullptr;
}
static int shared_test(){
 using namespace std::chrono;SharedContext c;c.owner=std::make_shared<SharedRecord>();c.weak=c.owner;
 std::unique_lock<std::recursive_mutex> lock(c.mutex);
 CHECK(c.condition.wait_for(lock,milliseconds(30))==std::cv_status::timeout);
 pthread_t t[3];for(auto& thread:t)CHECK(!pthread_create(&thread,nullptr,shared_worker,&c));
 CHECK(c.condition.wait_until(lock,steady_clock::now()+seconds(5),[&c]{return c.arrived==3;}));
 c.go=1;c.condition.notify_all();lock.unlock();
 for(auto thread:t){void *result=nullptr;CHECK(!pthread_join(thread,&result)&&!result);}
 CHECK(c.count==300&&c.owner.use_count()==1&&!c.weak.expired()&&shared_destroyed==0);
 c.owner.reset();CHECK(c.weak.expired()&&!c.weak.lock()&&shared_destroyed==1);c.weak.reset();return 0;
}
extern "C" int main(int argc,char **argv){
 using namespace std::chrono;
 if(argc>1){
  if(!std::strcmp(argv[1],"recursive")){say("recursive once refusal begins\n");recursive();return 1;}
  if(!std::strcmp(argv[1],"quota")){say("once quota refusal begins\n");nested(17);return 1;}
  if(!std::strcmp(argv[1],"unlocked-wait")){say("unlocked condition wait refusal begins\n");std::unique_lock<std::mutex> l(gate,std::defer_lock);ready.wait(l);return 1;}
  if(!std::strcmp(argv[1],"double-lock")){say("unique_lock double acquisition refusal begins\n");std::unique_lock<std::mutex> l(gate);l.lock();return 1;}
  return fail(__LINE__);
 }
 CHECK(!shared_test());
 nested(16);
 auto start=steady_clock::now();auto wall=system_clock::now();auto epoch=system_clock::to_time_t(wall);
 CHECK(epoch>1700000000);CHECK(system_clock::to_time_t(system_clock::from_time_t(epoch))==epoch);
 {std::unique_lock<std::mutex> l(gate);CHECK(ready.wait_for(l,milliseconds(-1))==std::cv_status::timeout);
  CHECK(ready.wait_for(l,milliseconds(40))==std::cv_status::timeout);CHECK(l.owns_lock());
  CHECK(!ready.wait_until(l,steady_clock::now()+milliseconds(30),[]{return false;}));
  CHECK(ready.wait_until(l,system_clock::now()-milliseconds(1))==std::cv_status::timeout);}
 CHECK(steady_clock::now()-start>=milliseconds(70));
 CHECK(!pthread_key_create(&exit_key,key_destructor));unsigned abandoned=0,destructor_exits=0;
 for(unsigned cycle=0;cycle<3;cycle++){
  arrived=go=counter=0;pthread_t threads[3];
  std::unique_lock<std::mutex> lock(gate);
  for(unsigned i=0;i<3;i++)CHECK(!pthread_create(&threads[i],nullptr,worker,(void *)uintptr_t(i)));
  CHECK(ready.wait_for(lock,seconds(5),[]{return arrived==3;}));
  go=1;go_condition.notify_all();lock.unlock();
  for(auto t:threads){void *result=nullptr;CHECK(!pthread_join(t,&result));
   if(result==(void *)uintptr_t(0x201))++abandoned;
   else if(result==(void *)uintptr_t(0x301))++destructor_exits;
   else CHECK(result==(void *)uintptr_t(0x100));}
  CHECK(counter==300&&init_count==1&&attempts==2&&destructor_attempts==2&&abandoned==1&&destructor_exits==1);
 }
 CHECK(!pthread_key_delete(exit_key));
 timed_mutex.lock();recursive_timed_mutex.lock();pthread_t t;CHECK(!pthread_create(&t,nullptr,timed_worker,nullptr));
 while(timed_ready.load()!=2)chos_sleep_ticks(1);
 timed_mutex.unlock();recursive_timed_mutex.unlock();void *result=nullptr;CHECK(!pthread_join(t,&result)&&!result);
 {std::mutex a,b;std::scoped_lock both(a,b);CHECK(!a.try_lock()&&!b.try_lock());}
 chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 say("[ OK ] native C++ mutex/condition/clock/once contention,timeout and owner-exit passed\n");return 0;
}
