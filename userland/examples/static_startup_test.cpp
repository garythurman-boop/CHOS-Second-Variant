#include <atomic>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <pthread.h>
#include "chos/libc.h"
#include "chos/user.h"
extern "C" int __cxa_atexit(void (*)(void *),void *,void *);
extern "C" void __cxa_finalize(void *);
extern "C" void __cxa_pure_virtual();
extern uintptr_t __init_array_start[];
static void say(const char *s){chos_write(s,std::strlen(s));}
[[noreturn]] static void fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native static lifecycle line ");chos_write(n,11);std::_Exit(1);}
#define CHECK(x) do{if(!(x))fail(__LINE__);}while(0)
#ifdef STARTUP_EARLY_EXIT
static constexpr bool early=true;
#else
static constexpr bool early=false;
#endif
static unsigned initialization[32],init_count,destruction[8],destroy_count,closing_done,fini_phase;
static std::atomic<unsigned> tls_destructs{0},post_key_destructs{0};
static unsigned expected_tls_destructs=4,expected_thread_value=0xabc;
static thread_local unsigned thread_value;
struct ThreadObject {
 unsigned value=0;
 ~ThreadObject(){CHECK(value==thread_value);++tls_destructs;}
};
static thread_local ThreadObject tls_object;
static void init_event(unsigned n){CHECK(init_count<32);initialization[init_count++]=n;}
struct Tracked {
 unsigned id;std::vector<unsigned> values;
 explicit Tracked(unsigned n):id(n),values{n,n+1,n+2}{init_event(n==1?3:n==2?5:n==3?7:8);}
 ~Tracked(){CHECK(values.size()==3&&values[0]==id&&values[2]==id+2&&destroy_count<8);destruction[destroy_count++]=id;}
};
static void closing(){
 if(early){CHECK(destroy_count==2&&destruction[0]==2&&destruction[1]==1);chos_heap_stats_t s;CHECK(!chos_libc_heap_info(&s)&&!s.mapped_bytes&&!s.allocations);}
 else{
  CHECK(destroy_count==4&&destruction[0]==4&&destruction[1]==3&&destruction[2]==2&&destruction[3]==1);
  CHECK(tls_destructs==expected_tls_destructs&&thread_value==expected_thread_value);
 }
 closing_done=1;
}
static void preinit(){init_event(1);CHECK(!std::atexit(closing));}
__attribute__((used,section(".preinit_array"))) static void (*const preinit_entry)()=preinit;
__attribute__((constructor(101))) static void first(){init_event(2);chos_yield();}
__attribute__((init_priority(200))) static Tracked first_object(1);
static unsigned constructor_worker;
static void *during_construction(void *){chos_sleep_ticks(1);constructor_worker=1;return reinterpret_cast<void *>(uintptr_t(0x515));}
__attribute__((constructor(202))) static void middle(){
 CHECK(first_object.values[1]==2);pthread_t t;void *v=nullptr;
 CHECK(!pthread_create(&t,nullptr,during_construction,nullptr)&&!pthread_join(t,&v)&&v==reinterpret_cast<void *>(uintptr_t(0x515))&&constructor_worker==1);init_event(4);
}
__attribute__((init_priority(300))) static Tracked second_object(2);
__attribute__((constructor(303))) static void last(){init_event(6);if(early){say("[ OK ] early constructor exit begins\n");std::exit(9);}}
__attribute__((destructor(202))) static void fini_first(){CHECK(closing_done&&fini_phase==0);fini_phase=1;}
static void post_fini(){
 CHECK(fini_phase==2);
 say(early?"[ OK ] native early constructor cleanup passed\n":"[ OK ] native static lifecycle globals,TLS,atexit and fini arrays passed\n");
}
__attribute__((destructor(101))) static void fini_last(){
 CHECK(closing_done&&fini_phase==1);fini_phase=2;CHECK(!std::atexit(post_fini));
}
static Tracked& main_static(){static Tracked t(3);return t;}
static Tracked& shared_static(){static Tracked t(4);return t;}
extern "C" void chos_startup_set_postkey(unsigned);
extern "C" void chos_startup_postkey_destroyed(unsigned value){CHECK(value==thread_value);++post_key_destructs;}
static pthread_key_t post_key;
static void post_key_callback(void *p){chos_startup_set_postkey(unsigned(reinterpret_cast<uintptr_t>(p)));}
static void *post_key_worker(void *p){thread_value=unsigned(reinterpret_cast<uintptr_t>(p));CHECK(!pthread_setspecific(post_key,p));return nullptr;}
static int dso_a,dso_b,shared_dso;
static unsigned dso_order[8],dso_count;
static std::atomic<unsigned> callback_hits[15]{};
static void dso_record(void *p){CHECK(dso_count<8);dso_order[dso_count++]=unsigned(reinterpret_cast<uintptr_t>(p));}
static void concurrent_record(void *p){unsigned i=unsigned(reinterpret_cast<uintptr_t>(p));CHECK(i<15);++callback_hits[i];chos_sleep_ticks(1);}
static void *register_worker(void *p){
 unsigned i=unsigned(reinterpret_cast<uintptr_t>(p));thread_value=0x100+i;tls_object.value=thread_value;
 CHECK(shared_static().values[0]==4);
 for(unsigned j=0;j<5;j++)CHECK(!__cxa_atexit(concurrent_record,reinterpret_cast<void *>(uintptr_t(i*5+j)),&shared_dso));
 return reinterpret_cast<void *>(uintptr_t(0x900+i));
}
static void *finalize_worker(void *){__cxa_finalize(&shared_dso);return nullptr;}
static void c_tail(){CHECK(destroy_count==2&&destruction[0]==4&&destruction[1]==3);}
static void noop(){}
static void cxx_noop(void *){}
static void rearm(void *){CHECK(!__cxa_atexit(rearm,nullptr,&dso_a));}
static void recursive_finalize(void *){CHECK(!__cxa_atexit(recursive_finalize,nullptr,&dso_a));__cxa_finalize(&dso_a);}
static void bad_thread_exit(){pthread_exit(nullptr);}
static void bad_exit(){std::exit(42);}
static void bad_manual_thread_exit(void *){pthread_exit(nullptr);}
static void *last_worker(void *p){
 chos_sleep_ticks(6);thread_value=0xdef;tls_object.value=thread_value;
 if(p)std::exit(37);return nullptr;
}
extern "C" int main(int argc,char **argv){
 CHECK(!early);CHECK(argc==3&&!std::strcmp(argv[2],"alpha"));
 CHECK(init_count==6&&constructor_worker==1);
 for(unsigned i=0;i<6;i++)CHECK(initialization[i]==i+1);
 CHECK(first_object.values[0]==1&&second_object.values[0]==2);
 CHECK(!std::atexit(c_tail));CHECK(main_static().values[0]==3);
 thread_value=0xabc;tls_object.value=thread_value;
 CHECK(!__cxa_atexit(dso_record,reinterpret_cast<void *>(1),&dso_a));
 CHECK(!__cxa_atexit(dso_record,reinterpret_cast<void *>(2),&dso_b));
 CHECK(!__cxa_atexit(dso_record,reinterpret_cast<void *>(3),&dso_a));
 __cxa_finalize(&dso_a);__cxa_finalize(&dso_a);
 CHECK(!__cxa_atexit(dso_record,reinterpret_cast<void *>(4),&dso_a));
 __cxa_finalize(&dso_a);__cxa_finalize(&dso_b);
 CHECK(dso_count==4&&dso_order[0]==3&&dso_order[1]==1&&dso_order[2]==4&&dso_order[3]==2);
 pthread_t threads[3];
 for(unsigned i=0;i<3;i++)CHECK(!pthread_create(&threads[i],nullptr,register_worker,reinterpret_cast<void *>(uintptr_t(i))));
 for(unsigned i=0;i<3;i++){void *value=nullptr;CHECK(!pthread_join(threads[i],&value)&&value==reinterpret_cast<void *>(uintptr_t(0x900+i)));}
 for(unsigned i=0;i<3;i++)CHECK(!pthread_create(&threads[i],nullptr,finalize_worker,nullptr));
 for(unsigned i=0;i<3;i++)CHECK(!pthread_join(threads[i],nullptr));
 for(auto& n:callback_hits)CHECK(n==1);
 chos_heap_stats_t before_key,after_key;CHECK(!chos_libc_heap_info(&before_key));
 CHECK(!pthread_key_create(&post_key,post_key_callback));
 for(unsigned i=0;i<3;i++)CHECK(!pthread_create(&threads[i],nullptr,post_key_worker,reinterpret_cast<void *>(uintptr_t(0x700+i))));
 for(unsigned i=0;i<3;i++)CHECK(!pthread_join(threads[i],nullptr));
 CHECK(post_key_destructs==3&&!pthread_key_delete(post_key));
 CHECK(!chos_libc_heap_info(&after_key)&&after_key.allocations==before_key.allocations&&after_key.requested_bytes==before_key.requested_bytes);
 CHECK(tls_destructs==3&&init_count==8);
 say("[ OK ] native static lifecycle main checks passed\n");
 const char *mode=argv[1];
 if(!std::strcmp(mode,"return"))return 17;
 if(!std::strcmp(mode,"exit"))std::exit(23);
 if(!std::strcmp(mode,"immediate")){say("[ OK ] native immediate exit skips cleanup\n");std::_Exit(77);}
 if(!std::strcmp(mode,"last-thread")){
  expected_tls_destructs=5;expected_thread_value=0xdef;pthread_t t;
  CHECK(!pthread_create(&t,nullptr,last_worker,nullptr));pthread_exit(nullptr);
 }
 if(!std::strcmp(mode,"worker-exit")){
  expected_thread_value=0xdef;pthread_t t;CHECK(!pthread_create(&t,nullptr,last_worker,reinterpret_cast<void *>(1)));
  for(;;)chos_sleep_ticks(1);
 }
 if(!std::strcmp(mode,"c-quota")){
  unsigned n;for(n=0;n<512;n++)if(std::atexit(noop))break;CHECK(n>=32&&n<512);return 18;
 }
 if(!std::strcmp(mode,"cxx-quota")){say("C++ process callback quota refusal begins\n");for(unsigned n=0;n<512;n++)__cxa_atexit(cxx_noop,nullptr,&dso_a);fail(__LINE__);}
 if(!std::strcmp(mode,"loop")){say("process callback rearm refusal begins\n");CHECK(!__cxa_atexit(rearm,nullptr,&dso_a));__cxa_finalize(&dso_a);fail(__LINE__);}
 if(!std::strcmp(mode,"recursion")){say("process callback recursion refusal begins\n");CHECK(!__cxa_atexit(recursive_finalize,nullptr,&dso_a));__cxa_finalize(&dso_a);fail(__LINE__);}
 if(!std::strcmp(mode,"thread-in-exit")){say("pthread_exit during process cleanup refusal begins\n");CHECK(!std::atexit(bad_thread_exit));std::exit(23);}
 if(!std::strcmp(mode,"thread-in-finalize")){say("pthread_exit during manual finalization refusal begins\n");CHECK(!__cxa_atexit(bad_manual_thread_exit,nullptr,&dso_a));__cxa_finalize(&dso_a);fail(__LINE__);}
 if(!std::strcmp(mode,"recursive-exit")){say("recursive process exit refusal begins\n");CHECK(!std::atexit(bad_exit));std::exit(23);}
 if(!std::strcmp(mode,"pure-virtual")){say("pure virtual refusal begins\n");__cxa_pure_virtual();fail(__LINE__);}
 if(!std::strcmp(mode,"array-ro")){say("read-only constructor array store begins\n");*reinterpret_cast<volatile uintptr_t *>(__init_array_start)=0;fail(__LINE__);}
 fail(__LINE__);
}
