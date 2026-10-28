#include <bmalloc/bmalloc.h>
#include <bmalloc/CryptoRandom.h>
#include <bmalloc/Logging.h>
#include <bmalloc/Mutex.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <chos/user_memory_limits.h>
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <thread>
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char b[80];unsigned n=0;const char *s="[FAIL] upstream native services line ";while(*s)b[n++]=*s++;char d[16];unsigned i=0;do{d[i++]=(char)('0'+line%10);line/=10;}while(line);while(i)b[n++]=d[--i];b[n++]='\n';chos_write(b,n);return 1;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static bmalloc::Mutex mutex;
static unsigned guarded;
static std::atomic<unsigned> failures,arrived;
static const unsigned char read_only[32]={};
static bool allocation(){
 constexpr auto mode=bmalloc::CompactAllocationMode::NonCompact;
 for(size_t size:{size_t(1),size_t(17),size_t(4097),size_t(32768)}){
  auto *p=static_cast<unsigned char *>(bmalloc::api::mallocOutOfLine(size,mode));if(!p)return false;
  for(size_t i=0;i<size;i++)p[i]=static_cast<unsigned char>(i*29u+7u);
  auto *q=static_cast<unsigned char *>(bmalloc::api::tryRealloc(p,size*2,mode));if(!q){bmalloc::api::freeOutOfLine(p);return false;}
  for(size_t i=0;i<size;i++)if(q[i]!=static_cast<unsigned char>(i*29u+7u))return false;
  bmalloc::api::freeOutOfLine(q);
 }
 auto *p=static_cast<unsigned char *>(bmalloc::api::tryZeroedMemalign(64,8192,mode));if(!p||((uintptr_t)p&63u))return false;
 for(size_t i=0;i<8192;i++)if(p[i])return false;bmalloc::api::free(p);
 p=static_cast<unsigned char *>(bmalloc::api::tryLargeZeroedMemalignVirtual(65536,32768,mode));if(!p||((uintptr_t)p&65535u))return false;
 for(size_t i=0;i<32768;i++)if(p[i])return false;p[0]=11;p[32767]=17;bmalloc::api::freeLargeVirtual(p,32768);
 return !bmalloc::api::tryMalloc(SIZE_MAX,mode);
}
static bool randomness(){
 std::array<unsigned char,602> first{},second{};first.front()=first.back()=0x73;second.front()=second.back()=0x19;
 bmalloc::cryptoRandom(nullptr,0);bmalloc::cryptoRandom(first.data()+1,600);bmalloc::cryptoRandom(second.data()+1,600);
 return first.front()==0x73&&first.back()==0x73&&second.front()==0x19&&second.back()==0x19&&std::memcmp(first.data()+1,second.data()+1,600);
}
static void worker(){
 ++arrived;
 for(unsigned i=0;i<3;i++){mutex.lock();++guarded;mutex.unlock();if(!allocation()||!randomness())++failures;}
}
extern "C" int main(int argc,char **argv){
 if(argc>1&&!std::strcmp(argv[1],"deny-output")){
  bmalloc::reportAssertionFailureWithMessage("fixture.cpp",23,"closed_output","bounded logger %d\n",7);return 42;
 }
 if(argc>1&&(!std::strcmp(argv[1],"deny-random")||!std::strcmp(argv[1],"missing-entropy"))){
  say("[EXPECT] native entropy must fail closed\n");unsigned char bytes[32];bmalloc::cryptoRandom(bytes,sizeof(bytes));return fail(__LINE__);
 }
 if(argc>1&&!std::strcmp(argv[1],"readonly-entropy")){
  say("[EXPECT] native entropy rejects a read-only output buffer\n");bmalloc::cryptoRandom(const_cast<unsigned char *>(read_only),sizeof(read_only));return fail(__LINE__);
 }
 CHECK(allocation());
 if(argc>1&&!std::strcmp(argv[1],"control-only")){
  bmalloc::reportAssertionFailureWithMessage("fixture.cpp",23,"control_only","[native-bmalloc] recovery %d\n",7);
  say("[ OK ] upstream native allocation and logging recover after entropy failure\n");return 0;
 }
 bmalloc::reportAssertionFailureWithMessage("fixture.cpp",23,"native_services","[native-bmalloc] value=%d text=%s\n",7,"ready");
 char long_message[800];std::memset(long_message,'z',sizeof(long_message));long_message[799]=0;
 bmalloc::reportAssertionFailureWithMessage("fixture.cpp",24,"bounded_message","%s",long_message);
 bmalloc::reportAssertionFailureWithMessage("fixture.cpp",25,"rejected_format","%lc",static_cast<wint_t>(0xd800));
 CHECK(randomness());
 for(unsigned cycle=0;cycle<3;cycle++){
  arrived=0;mutex.lock();std::thread threads[3];for(auto &t:threads)t=std::thread(worker);
  while(arrived!=3)std::this_thread::yield();CHECK(!chos_sleep_ticks(2));mutex.unlock();
  for(auto &t:threads)t.join();CHECK(!failures&&guarded==(cycle+1)*9);
  // Upstream SystemHeap retains its small unordered-map bucket cache until
  // process exit. Bound that live cache; the guest runner checks complete reap.
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&stats.allocations==1&&stats.mapped_bytes==65536);
 }
 const char *path="/BIN/BMSVC.ELF";
 long pid=chos_spawn_limited(path,"deny-output",CHOS_PROCESS_CAP_ALL&~CHOS_PROCESS_CAP(CHOS_SYS_WRITE));CHECK(pid>0&&chos_wait((uint32_t)pid)==42);
 pid=chos_spawn_limited(path,"deny-random",CHOS_PROCESS_CAP_ALL&~CHOS_PROCESS_CAP(CHOS_SYS_RANDOM_BYTES));CHECK(pid>0&&chos_wait((uint32_t)pid)<0&&chos_getstatus((uint32_t)pid)==CHOS_STATUS_FAILED);
 say("[ OK ] upstream bmalloc allocation, native entropy, diagnostics and mutex services passed\n");return 0;
}
