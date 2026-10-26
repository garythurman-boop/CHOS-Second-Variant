#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include "chos/libc.h"
#include "chos/user.h"
extern "C" int __cxa_guard_acquire(uint64_t *);
extern "C" void __cxa_guard_release(uint64_t *);
extern "C" void __cxa_guard_abort(uint64_t *);
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native C++ guard line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static std::atomic<unsigned> initialized[8]{},arrived{0},attempts{0},destructor_attempts{0};
static pthread_key_t exit_key;
static uint64_t manual_guard;
struct Record {unsigned index;uint64_t checksum;};
template<unsigned I> static Record make_record(){
 ++initialized[I];chos_sleep_ticks(2);return {I,UINT64_C(0xabc00000)+I};
}
template<unsigned I> static const Record& record(){static const Record r=make_record<I>();return r;}
static Record make_retry_record(){
 unsigned n=attempts.fetch_add(1);chos_sleep_ticks(2);
 if(!n)pthread_exit(reinterpret_cast<void *>(uintptr_t(0x201)));
 return {91,UINT64_C(0x918273645)};
}
static const Record& retry_record(){static const Record r=make_retry_record();return r;}
static Record make_destructor_record(){
 unsigned n=destructor_attempts.fetch_add(1);chos_sleep_ticks(2);
 if(!n)pthread_exit(reinterpret_cast<void *>(uintptr_t(0x301)));
 return {92,UINT64_C(0x123456789)};
}
static const Record& destructor_record(){static const Record r=make_destructor_record();return r;}
static void key_destructor(void *){
 const Record& r=destructor_record();if(r.index!=92||r.checksum!=UINT64_C(0x123456789))std::abort();
}
static int check_record(const Record& r,unsigned i){CHECK(r.index==i&&r.checksum==UINT64_C(0xabc00000)+i);return 0;}
static void *worker(void *){
 ++arrived;while(arrived.load()!=3)chos_sleep_ticks(1);
 if(check_record(record<0>(),0)||check_record(record<1>(),1)||check_record(record<2>(),2)||check_record(record<3>(),3)||
    check_record(record<4>(),4)||check_record(record<5>(),5)||check_record(record<6>(),6)||check_record(record<7>(),7))return reinterpret_cast<void *>(1);
 const Record& r=retry_record();if(r.index!=91||r.checksum!=UINT64_C(0x918273645))return reinterpret_cast<void *>(2);
 if(pthread_setspecific(exit_key,reinterpret_cast<void *>(1)))return reinterpret_cast<void *>(3);
 return reinterpret_cast<void *>(uintptr_t(0x100));
}
static unsigned recursive();
__attribute__((noinline)) static unsigned recursive_init(){chos_yield();return recursive()+1;}
static unsigned recursive(){static unsigned x=recursive_init();return x;}
template<unsigned N> static unsigned nested(){static unsigned value=nested<N-1>()+1;return value;}
template<> unsigned nested<0>(){return 0;}
extern "C" int main(int argc,char **argv){
 if(argc>1){
  if(!std::strcmp(argv[1],"recursive")){say("recursive guard refusal begins\n");return int(recursive());}
  if(!std::strcmp(argv[1],"quota")){say("guard quota refusal begins\n");return int(nested<17>());}
  return fail(__LINE__);
 }
 CHECK(__cxa_guard_acquire(&manual_guard)==1);__cxa_guard_abort(&manual_guard);CHECK(manual_guard==0);
 CHECK(__cxa_guard_acquire(&manual_guard)==1);__cxa_guard_release(&manual_guard);CHECK(__cxa_guard_acquire(&manual_guard)==0);
 CHECK(nested<16>()==16);
 CHECK(!pthread_key_create(&exit_key,key_destructor));
 unsigned abandoned=0,destructor_exits=0;
 for(unsigned lifecycle=0;lifecycle<3;lifecycle++){
  arrived=0;pthread_t threads[3];for(unsigned i=0;i<3;i++)CHECK(!pthread_create(&threads[i],nullptr,worker,nullptr));
  for(unsigned i=0;i<3;i++){
   void *value=nullptr;CHECK(!pthread_join(threads[i],&value));
   if(value==reinterpret_cast<void *>(uintptr_t(0x201)))++abandoned;
   else if(value==reinterpret_cast<void *>(uintptr_t(0x301)))++destructor_exits;
   else CHECK(value==reinterpret_cast<void *>(uintptr_t(0x100)));
  }
  for(auto& count:initialized)CHECK(count==1);
  CHECK(attempts==2&&abandoned==1&&destructor_attempts==2&&destructor_exits==1);
 }
 CHECK(!pthread_key_delete(exit_key));
 chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&stats.mapped_bytes==0&&!stats.allocations);
 say("[ OK ] native C++ static guards publish once,wait,retry abandoned owners and clean up\n");return 0;
}
