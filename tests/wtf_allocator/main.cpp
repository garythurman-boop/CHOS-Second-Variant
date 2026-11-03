#include "config.h"
#include <wtf/OSAllocator.h>
#include <wtf/Threading.h>
#include <chos/user.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cstdint>
using namespace WTF;
static constexpr size_t page=4096,small=64*1024,large=32*1024*1024;
static bool failed(unsigned line){std::printf("[FAIL] actual WTF allocator line%u errno%d\n",line,errno);return false;}
#define CHECK(v) do{if(!(v))return failed(__LINE__);}while(0)
static bool delta(const chos_resource_info_t& base,uint64_t bytes){
 chos_resource_info_t now{};CHECK(!chos_resource_info(&now));
 CHECK(now.resident_bytes==base.resident_bytes+bytes&&now.mapped_bytes==base.mapped_bytes+bytes);return true;
}
static bool zero(void* address,size_t bytes){
 auto p=static_cast<volatile unsigned char*>(address);for(size_t i=0;i<bytes;i++)CHECK(!p[i]);return true;
}
static bool normal(){
 errno=0;(void)Thread::currentSingleton();std::puts("[INFO] actual WTF allocator fixture begins");
 chos_resource_info_t base{};CHECK(!chos_resource_info(&base));
 void* p=OSAllocator::tryReserveUncommittedAligned(large,large);CHECK(p&&!(reinterpret_cast<uintptr_t>(p)%large)&&delta(base,0));
 OSAllocator::commit(p,small,true,false);CHECK(zero(p,small)&&delta(base,small));std::memset(p,0x5a,small);
 CHECK(OSAllocator::tryProtect(p,small,true,false)&&*static_cast<unsigned char*>(p)==0x5a);
 CHECK(OSAllocator::tryProtect(p,small,false,false)&&delta(base,small));
 OSAllocator::commit(p,small,true,false);CHECK(*static_cast<unsigned char*>(p)==0x5a);
 OSAllocator::decommit(p,small);CHECK(delta(base,0));
 OSAllocator::commit(p,small,true,false);CHECK(zero(p,small));OSAllocator::decommit(p,small);
 OSAllocator::releaseDecommitted(p,large,0);CHECK(delta(base,0));
 // Data alignment must survive both requested guards and trimming of slack.
 p=OSAllocator::tryReserveUncommittedAligned(2*small,small,OSAllocator::UnknownUsage,nullptr,true,false,false,1);
 CHECK(p&&!(reinterpret_cast<uintptr_t>(p)%small)&&delta(base,0));
 OSAllocator::commit(p,2*small,true,false);CHECK(zero(p,2*small));std::memset(p,0x44,2*small);
 OSAllocator::zeroFill(p,2*small);CHECK(zero(p,2*small)&&delta(base,2*small));
 OSAllocator::hintMemoryNotNeededSoon(p,2*small);CHECK(delta(base,2*small));
 OSAllocator::decommit(p,2*small);OSAllocator::releaseDecommitted(p,2*small,1);CHECK(delta(base,0));
 // Actual public API commits/releases more than one kernel transaction.
 p=OSAllocator::tryReserveAndCommit(2*1024*1024,OSAllocator::UnknownUsage,nullptr,true,false,false,2);
 CHECK(p&&zero(p,2*1024*1024)&&delta(base,2*1024*1024));
 OSAllocator::decommit(p,2*1024*1024);OSAllocator::releaseDecommitted(p,2*1024*1024,2);CHECK(delta(base,0));
 CHECK(!OSAllocator::tryReserveAndCommit(page,OSAllocator::UnknownUsage,nullptr,true,true)&&errno==EACCES);
 CHECK(!OSAllocator::tryReserveUncommittedAligned(page,3*page));
 CHECK(!OSAllocator::tryReserveUncommitted(page,OSAllocator::UnknownUsage,reinterpret_cast<void*>(uintptr_t(0x28000000))));
 CHECK(!OSAllocator::tryReserveUncommitted(large+page)&&delta(base,0));
 std::puts("[ OK ] actual WTF aligned reservation,guarded commit,decommit,zero fill and release passed");return true;
}
int main(int argc,char**argv){
 if(argc==1)return normal()?0:1;
 (void)Thread::currentSingleton();
 auto p=static_cast<unsigned char*>(OSAllocator::tryReserveUncommittedAligned(small,small,OSAllocator::UnknownUsage,nullptr,true,false,false,1));
 if(!p)return 2;
 if(!std::strcmp(argv[1],"readonly")){OSAllocator::commit(p,small,false,false);*static_cast<volatile unsigned char*>(p)=1;}
 else {
  OSAllocator::commit(p,small,true,false);
  if(!std::strcmp(argv[1],"leftguard"))*(static_cast<volatile unsigned char*>(p)-1)=1;
  else if(!std::strcmp(argv[1],"rightguard"))*(static_cast<volatile unsigned char*>(p)+small)=1;
  else if(!std::strcmp(argv[1],"decommitted")){OSAllocator::decommit(p,small);*static_cast<volatile unsigned char*>(p)=1;}
  else if(!std::strcmp(argv[1],"execute")){*reinterpret_cast<volatile uint32_t*>(p)=0xd65f03c0u;reinterpret_cast<void(*)()>(p)();}
  else return 3;
 }
 std::puts("[FAIL] WTF allocator fault unexpectedly returned");return 4;
}
