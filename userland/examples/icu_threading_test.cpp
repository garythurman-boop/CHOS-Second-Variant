/* Component integration fixture: records the actual ICU mutex cleanup callback.
 * Test code only, not a substitute for ICU's full cleanup registry. */
#include "umutex.h"
#include "ucln_cmn.h"
#include "uassert.h"
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include "chos/libc.h"
#include "chos/user.h"
#define NDEBUG
#include <assert.h>
static int assertion_no_evaluation(){int n=0;assert(++n);return n;}
#undef NDEBUG
#include <assert.h>
using namespace icu;
static cleanupFunc *recorded_cleanup;
static std::atomic<unsigned> registrations{};
U_CFUNC void U_EXPORT2 ucln_common_registerCleanup(ECleanupCommonType type,cleanupFunc *fn){
 if(type!=UCLN_COMMON_MUTEX||!fn||recorded_cleanup)std::abort();
 recorded_cleanup=fn;++registrations;
}
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] ICU threading line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static UMutex mutexes[8];
static UInitOnce once,failed_once;
static std::atomic<unsigned> arrived{};
static unsigned counters[8],global_count,initialized,initialized_value,failed_count;
static void initialize(){++initialized;chos_sleep_ticks(3);initialized_value=0x87654321;}
static void initialize_failure(UErrorCode& error){++failed_count;chos_sleep_ticks(3);error=U_ILLEGAL_ARGUMENT_ERROR;}
static void *worker(void *){
 ++arrived;while(arrived.load()!=3)chos_sleep_ticks(1);
 umtx_initOnce(once,initialize);if(initialized_value!=0x87654321)return (void *)1;
 UErrorCode error=U_ZERO_ERROR;umtx_initOnce(failed_once,initialize_failure,error);if(error!=U_ILLEGAL_ARGUMENT_ERROR)return (void *)2;
 for(unsigned n=0;n<80;n++){
  for(unsigned i=0;i<8;i++){std::lock_guard<UMutex> hold(mutexes[i]);++counters[i];if(!(n&31))chos_yield();}
  umtx_lock(nullptr);++global_count;umtx_unlock(nullptr);
 }
 return nullptr;
}
extern "C" int main(int argc,char **argv){
 if(argc>1&&!std::strcmp(argv[1],"assert")){say("ICU assertion refusal begins\n");U_ASSERT(false);return 1;}
 CHECK(!assertion_no_evaluation());
 CHECK(once.isReset()&&failed_once.isReset());
 for(unsigned cycle=0;cycle<4;cycle++){
  arrived=global_count=initialized=initialized_value=failed_count=0;for(auto& n:counters)n=0;
  pthread_t t[3];for(auto& thread:t)CHECK(!pthread_create(&thread,nullptr,worker,nullptr));
  for(auto thread:t){void *result=nullptr;CHECK(!pthread_join(thread,&result)&&!result);}
  for(auto n:counters)CHECK(n==240);
  CHECK(global_count==240&&initialized==1&&failed_count==1&&!once.isReset()&&!failed_once.isReset());
  CHECK(recorded_cleanup&&registrations==cycle+1);
  auto cleanup=recorded_cleanup;recorded_cleanup=nullptr;CHECK(cleanup());
  once.reset();failed_once.reset();CHECK(once.isReset()&&failed_once.isReset());
 }
 chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 say("[ OK ] original ICU mutex/init-once contention,error publication and cleanup/reinitialize passed\n");return 0;
}
