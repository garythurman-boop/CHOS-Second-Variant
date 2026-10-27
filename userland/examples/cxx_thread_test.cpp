#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <pthread.h>
#include "chos/libc.h"
#include "chos/user.h"
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native C++ thread line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
struct ExitState {std::mutex mutex;std::condition_variable condition;bool ready=false;std::atomic<unsigned> destructors{};};
static ExitState states[3];
static std::mutex gate;static std::condition_variable arrived_condition,go_condition;
static unsigned arrived,go;
static pthread_key_t key;
static std::atomic<unsigned> functors_destroyed{},key_calls{};
extern "C" void cxx_thread_postkey_touch(std::atomic<unsigned> *,std::mutex *);
struct Witness {ExitState *state;~Witness(){++state->destructors;}};
static void witness(ExitState *s){thread_local Witness value{s};(void)value;}
static void key_destructor(void *p){auto *s=static_cast<ExitState *>(p);++key_calls;cxx_thread_postkey_touch(&s->destructors,&s->mutex);}
struct Payload {unsigned value;explicit Payload(unsigned n):value(n){}~Payload(){++functors_destroyed;}};
static void wait_gate(){std::unique_lock<std::mutex> l(gate);++arrived;arrived_condition.notify_one();go_condition.wait(l,[]{return go!=0;});}
static void release_gate(unsigned n){std::unique_lock<std::mutex> l(gate);arrived_condition.wait(l,[n]{return arrived==n;});go=1;go_condition.notify_all();}
static void worker(unsigned i,std::unique_ptr<Payload> payload){
 if(payload->value!=i||std::this_thread::get_id()==std::thread::id())std::abort();
 witness(&states[i]);if(pthread_setspecific(key,&states[i]))std::abort();
 wait_gate();std::this_thread::yield();
 std::unique_lock<std::mutex> lock(states[i].mutex);states[i].ready=true;
 std::notify_all_at_thread_exit(states[i].condition,std::move(lock));
}
static void handler_return(){say("custom terminate handler returned\n");}
static void handler_exit(){say("custom terminate handler exits process\n");std::exit(77);}
static void handler_recursive(){std::terminate();}
static void handler_pthread_exit(){pthread_exit(nullptr);}
__attribute__((noinline)) static void sleep_ms(int n){std::this_thread::sleep_for(std::chrono::milliseconds(n));}
__attribute__((noinline)) static void sleep_seconds(double n){std::this_thread::sleep_for(std::chrono::duration<double>(n));}
extern "C" int main(int argc,char **argv){
 using namespace std::chrono;
 if(argc>1){
  if(!std::strcmp(argv[1],"join-empty")){say("empty join refusal begins\n");std::thread t;t.join();return 1;}
  if(!std::strcmp(argv[1],"joinable-destructor")){say("joinable destructor refusal begins\n");{std::thread t(wait_gate);}return 1;}
  if(!std::strcmp(argv[1],"detach")){say("detach refusal begins\n");std::thread t(wait_gate);t.detach();return 1;}
  if(!std::strcmp(argv[1],"capacity")){say("fourth C++ worker refusal begins\n");std::thread a(wait_gate),b(wait_gate),c(wait_gate),d(wait_gate);return 1;}
  if(!std::strcmp(argv[1],"pthread-exit")){say("C++ callable pthread_exit refusal begins\n");std::thread t([]{pthread_exit(nullptr);});t.join();return 1;}
  if(!std::strcmp(argv[1],"move-over-live")){say("move over joinable refusal begins\n");std::thread a(wait_gate),b(wait_gate);a=std::move(b);return 1;}
  if(!std::strcmp(argv[1],"self-join")){say("self join refusal begins\n");std::thread t([&t]{wait_gate();t.join();});release_gate(1);for(;;)chos_sleep_ticks(1);}
  if(!std::strcmp(argv[1],"handler-return")){say("returning handler refusal begins\n");std::set_terminate(handler_return);std::terminate();}
  if(!std::strcmp(argv[1],"handler-exit")){say("exiting handler begins\n");std::set_terminate(handler_exit);std::terminate();}
  if(!std::strcmp(argv[1],"handler-recursive")){say("recursive handler refusal begins\n");std::set_terminate(handler_recursive);std::terminate();}
  if(!std::strcmp(argv[1],"handler-thread-exit")){say("handler pthread_exit refusal begins\n");std::set_terminate(handler_pthread_exit);std::terminate();}
  return fail(__LINE__);
 }
 auto original=std::get_terminate();CHECK(original);CHECK(std::set_terminate(handler_return)==original&&std::get_terminate()==handler_return);
 CHECK(std::set_terminate(original)==handler_return&&std::get_terminate()==original);
 CHECK(std::set_terminate(nullptr)==original&&std::get_terminate());
 CHECK(std::thread::hardware_concurrency()==0);CHECK(std::this_thread::get_id()!=std::thread::id());
 std::thread empty;CHECK(!empty.joinable()&&empty.get_id()==std::thread::id());
 CHECK(!pthread_key_create(&key,key_destructor));
 for(unsigned cycle=0;cycle<3;cycle++){
  arrived=go=0;for(auto& s:states){s.ready=false;s.destructors=0;}
  std::thread threads[3];
  for(unsigned i=0;i<3;i++)threads[i]=std::thread(worker,i,std::make_unique<Payload>(i));
  auto ids0=threads[0].get_id(),ids1=threads[1].get_id(),ids2=threads[2].get_id();
  CHECK(ids0!=ids1&&ids0!=ids2&&ids1!=ids2);CHECK((ids0<ids1)!=(ids1<ids0));
  std::thread moved(std::move(threads[0]));CHECK(!threads[0].joinable()&&moved.get_id()==ids0);threads[0].swap(moved);
  CHECK(!moved.joinable()&&threads[0].get_id()==ids0);
  release_gate(3);
  for(unsigned i=0;i<3;i++){
   std::unique_lock<std::mutex> l(states[i].mutex);
   CHECK(states[i].condition.wait_for(l,seconds(5),[i]{return states[i].ready;}));
   CHECK(states[i].destructors==2);l.unlock();threads[i].join();CHECK(!threads[i].joinable());
  }
  CHECK(key_calls==3*(cycle+1));CHECK(functors_destroyed==3*(cycle+1));
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 CHECK(!pthread_key_delete(key));
 auto begin=steady_clock::now();std::this_thread::sleep_for(nanoseconds(0));std::this_thread::sleep_for(nanoseconds(-10));
 std::this_thread::sleep_for(nanoseconds(30000000));CHECK(steady_clock::now()-begin>=milliseconds(30));
 volatile int ms=20;begin=steady_clock::now();sleep_ms(ms);CHECK(steady_clock::now()-begin>=milliseconds(20));
 volatile double seconds_value=0.02;begin=steady_clock::now();sleep_seconds(seconds_value);CHECK(steady_clock::now()-begin>=milliseconds(20));
 sleep_ms(-20);sleep_seconds(-0.5);
 begin=steady_clock::now();std::this_thread::sleep_until(begin+milliseconds(20));CHECK(steady_clock::now()-begin>=milliseconds(20));
 say("[ OK ] native std::thread move/join,TLS ordering,exit notification and sleep passed\n");return 0;
}
