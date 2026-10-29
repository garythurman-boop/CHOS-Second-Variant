#include "config.h"
#include <wtf/RandomDevice.h>
#include <wtf/ProcessID.h>
#include <wtf/PageBlock.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <array>
#include <atomic>
#include <cstring>
#include <thread>
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char b[80];unsigned n=0;const char *s="[FAIL] native WTF entropy line ";while(*s)b[n++]=*s++;char d[16];unsigned i=0;do{d[i++]=(char)('0'+line%10);line/=10;}while(line);while(i)b[n++]=d[--i];b[n++]='\n';chos_write(b,n);return 1;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
alignas(16) static const unsigned char read_only[32]={1};
static std::atomic<unsigned> failures;
static WTF::RandomDevice shared;
static int positive(){
 std::array<uint8_t,602> first{},second{};first.front()=first.back()=0x73;second.front()=second.back()=0x19;
 shared.cryptographicallyRandomValues({});
 shared.cryptographicallyRandomValues(std::span(first).subspan(1,600));shared.cryptographicallyRandomValues(std::span(second).subspan(1,600));
 CHECK(first.front()==0x73&&first.back()==0x73&&second.front()==0x19&&second.back()==0x19);
 CHECK(std::memcmp(first.data()+1,second.data()+1,600));
 CHECK(WTF::getCurrentProcessID()==chos_getpid()&&WTF::pageSize()==4096&&WTF::pageSize()==static_cast<size_t>(getpagesize()));return 0;
}
static void worker(){for(unsigned n=0;n<3;n++){if(positive())++failures;std::this_thread::yield();}}
extern "C" int main(int argc,char **argv){
 if(argc>1){
  if(!std::strcmp(argv[1],"empty")){shared.cryptographicallyRandomValues({});say("[ OK ] native WTF empty request works without entropy\n");return 0;}
  if(!std::strcmp(argv[1],"deny-pid")){say("[EXPECT] native WTF PID error fails closed\n");(void)WTF::getCurrentProcessID();return fail(__LINE__);}
  say("[EXPECT] native WTF entropy error fails closed\n");
  if(!std::strcmp(argv[1],"readonly")){shared.cryptographicallyRandomValues(std::span<uint8_t>(const_cast<uint8_t *>(read_only),sizeof(read_only)));return fail(__LINE__);}
  uint8_t bytes[32];shared.cryptographicallyRandomValues(bytes);return fail(__LINE__);
 }
 CHECK(!positive());
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread threads[3];for(auto &t:threads)t=std::thread(worker);for(auto &t:threads)t.join();CHECK(!failures);
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 for(const char *mode:{"deny-random","readonly","deny-pid"}){
  uint64_t caps=CHOS_PROCESS_CAP_ALL;
  if(!std::strcmp(mode,"deny-random"))caps&=~CHOS_PROCESS_CAP(CHOS_SYS_RANDOM_BYTES);
  if(!std::strcmp(mode,"deny-pid"))caps&=~CHOS_PROCESS_CAP(CHOS_SYS_GETPID);
  long pid=chos_spawn_limited("/BIN/WTFENT.ELF",mode,caps);CHECK(pid>0&&chos_wait(static_cast<uint32_t>(pid))==134);
 }
 say("[ OK ] native WTF OS entropy, process and page services passed\n");return 0;
}
