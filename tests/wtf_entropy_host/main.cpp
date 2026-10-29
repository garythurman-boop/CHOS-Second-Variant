#include <wtf/RandomDevice.h>
#include <wtf/ProcessID.h>
#include <wtf/PageBlock.h>
#undef NDEBUG
#include <cassert>
#include <cstring>
#include <csignal>
#include <cstdlib>
#include "chos/syscall.h"
static volatile sig_atomic_t calls,delays,pending,fatal_mode;
extern "C" long chos_random_bytes(void *p,size_t n){
 assert(p&&n&&n<=256);calls=calls+1;
 if(fatal_mode==1)return -CHOS_ERRNO_EPERM;
 if(fatal_mode==2)return 0;
 if(fatal_mode==3)return static_cast<long>(n+1);
 if(fatal_mode==4||fatal_mode==5)return -CHOS_ERRNO_EAGAIN;
 if(pending){pending=pending-1;return pending%2?-CHOS_ERRNO_EINTR:-CHOS_ERRNO_EAGAIN;}
 size_t count=n>17?17:n;std::memset(p,0x5a,count);return static_cast<long>(count);
}
extern "C" long chos_sleep_ticks(uint64_t n){assert(n==1);delays=delays+1;return fatal_mode==5?-CHOS_ERRNO_EPERM:0;}
extern "C" pid_t chos_test_getpid(void) noexcept{return fatal_mode==6?-1:123;}
extern "C" long chos_test_sysconf(int name) noexcept{assert(name==_SC_PAGESIZE);return fatal_mode==7?3000:fatal_mode==8?-1:4096;}
static void aborted(int){
 bool correct=fatal_mode==4?(calls==33&&delays==32):fatal_mode==5?(calls==1&&delays==1):fatal_mode>=6?(!calls&&!delays):(calls==1&&!delays);
 std::_Exit(correct?42:43);
}
int main(int argc,char **argv){
 WTF::RandomDevice device;
 if(argc>1){fatal_mode=std::atoi(argv[1]);assert(fatal_mode>=1&&fatal_mode<=8);std::signal(SIGABRT,aborted);
  if(fatal_mode==6)(void)WTF::getCurrentProcessID();
  else if(fatal_mode>=7)(void)WTF::pageSize();
  else{uint8_t buffer[32];device.cryptographicallyRandomValues(buffer);}
  return 44;
 }
 for(size_t count:{size_t(1),size_t(255),size_t(256),size_t(257),size_t(512),size_t(1024)}){
  uint8_t buffer[1026];std::memset(buffer,0xa5,sizeof(buffer));calls=0;delays=0;pending=32;
  device.cryptographicallyRandomValues(std::span(buffer+1,count));assert(delays==32&&calls>32);
  assert(buffer[0]==0xa5&&buffer[count+1]==0xa5);for(size_t i=0;i<count;i++)assert(buffer[i+1]==0x5a);
 }
 auto before=calls;device.cryptographicallyRandomValues({});assert(calls==before);
 assert(WTF::getCurrentProcessID()==123&&WTF::pageSize()==4096&&WTF::pageSize()==4096);return 0;
}
