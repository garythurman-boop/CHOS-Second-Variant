#include <bmalloc/CryptoRandom.h>
#include <bmalloc/Logging.h>
#include <bmalloc/Mutex.h>
#undef NDEBUG
#include <cassert>
#include <cstring>
#include <cwchar>
#include <thread>
#include <atomic>
#include "chos/syscall.h"
static unsigned char output[4096];static size_t used;static unsigned output_calls;static long output_error;static bool output_zero;
static unsigned random_calls,delays,unready;
extern "C" long chos_write(const void *p,size_t n){
 assert(n&&n<=256);++output_calls;if(output_error)return output_error;if(output_zero)return 0;
 size_t count=n>13?13:n;assert(used+count<sizeof(output));std::memcpy(output+used,p,count);used+=count;output[used]=0;return static_cast<long>(count);
}
extern "C" long chos_random_bytes(void *p,size_t n){
 assert(p&&n&&n<=256);++random_calls;
 if(unready){--unready;return unready%2?-CHOS_ERRNO_EINTR:-CHOS_ERRNO_EAGAIN;}
 size_t count=n>17?17:n;std::memset(p,0x5a,count);return static_cast<long>(count);
}
extern "C" long chos_sleep_ticks(uint64_t n){assert(n==1);++delays;return 0;}
static std::atomic<unsigned> yields;
extern "C" long chos_yield(void){++yields;std::this_thread::yield();return 0;}
static void reset_output(){used=output_calls=0;output[0]=0;output_error=0;output_zero=false;}
int main(){
 for(size_t count:{size_t(1),size_t(255),size_t(256),size_t(257),size_t(512),size_t(1024)}){
  unsigned char buffer[1026];std::memset(buffer,0xa5,sizeof(buffer));random_calls=delays=0;unready=32;
  bmalloc::cryptoRandom(buffer+1,count);assert(delays==32&&random_calls>32);
  assert(buffer[0]==0xa5&&buffer[count+1]==0xa5);for(size_t i=0;i<count;i++)assert(buffer[i+1]==0x5a);
 }
 unsigned before=random_calls;bmalloc::cryptoRandom(nullptr,0);assert(random_calls==before);
 reset_output();bmalloc::reportAssertionFailureWithMessage("fixture",23,"host","value=%d %s\n",7,"ready");
 assert(!std::strcmp(reinterpret_cast<char*>(output),"value=7 ready\nfixture(23) : host\n"));assert(output_calls>2);
 char long_text[800];std::memset(long_text,'z',sizeof(long_text));long_text[799]=0;
 reset_output();bmalloc::reportAssertionFailureWithMessage("fixture",23,"host","%s",long_text);
 assert(used==511+12+sizeof("fixture(23) : host\n")-1&&std::strstr(reinterpret_cast<char*>(output),"[truncated]\n"));
 reset_output();bmalloc::reportAssertionFailureWithMessage(long_text,23,long_text,"long source\n");
 assert(used<=2*(511+12)&&std::strstr(reinterpret_cast<char*>(output),"[truncated]\n"));
 reset_output();bmalloc::reportAssertionFailureWithMessage("fixture",23,"host","%lc",static_cast<wint_t>(0xd800));
 assert(std::strstr(reinterpret_cast<char*>(output),"[diagnostic format failed]\n"));
 reset_output();output_error=-CHOS_ERRNO_EPERM;bmalloc::reportAssertionFailureWithMessage("fixture",23,"host","value=%d\n",7);assert(!used&&output_calls==2);
 reset_output();output_zero=true;bmalloc::reportAssertionFailureWithMessage("fixture",23,"host","zero sink\n");assert(!used&&output_calls==2);
 reset_output();
 bmalloc::Mutex mutex;unsigned guarded=0;std::atomic<unsigned> arrived=0;
 mutex.lock();std::thread workers[3];for(auto& t:workers)t=std::thread([&]{++arrived;for(unsigned i=0;i<1000;i++){mutex.lock();++guarded;mutex.unlock();}});
 while(arrived!=3)std::this_thread::yield();while(!yields)std::this_thread::yield();mutex.unlock();
 for(auto& t:workers)t.join();assert(guarded==3000&&yields);return 0;
}
