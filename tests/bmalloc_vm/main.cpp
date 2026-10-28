/* Executes original upstream inline VM algorithms against the real CHOS SDK.
 * The only VM algorithm adaptation is the checked native discard branch. */
#include <bmalloc/VMAllocate.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <cstdint>
#include <cstring>
#include <thread>
#include <atomic>
#ifndef __CHOS__
#error Native target identity is required
#endif
static void say(const char *s){chos_write(s,std::strlen(s));}
static std::atomic<unsigned> failures;
static bool exercise(){
 if(bmalloc::vmPageSize()!=4096||bmalloc::vmPageSizePhysical()!=4096)return false;
 auto *p=static_cast<unsigned char *>(bmalloc::tryVMAllocate(64u*1024u,32u*1024u));
 if(!p||((uintptr_t)p&65535u))return false;
 for(unsigned i=0;i<32768;i++){if(p[i])return false;p[i]=static_cast<unsigned char>(i*29u+7u);}
 bmalloc::vmZeroAndPurge(p,32768);
 for(unsigned i=0;i<32768;i++){if(p[i])return false;p[i]=0xa5;}
 bmalloc::vmDeallocatePhysicalPages(p,32768);
 bmalloc::vmAllocatePhysicalPages(p,32768);
 for(unsigned i=0;i<32768;i++)if(p[i])return false;
 bmalloc::vmRevokePermissions(p,32768);
 if(chos_write(p,1)!=-CHOS_ERRNO_EFAULT)return false;
 if(mprotect(p,32768,PROT_READ|PROT_WRITE))return false;
 p[0]=71;p[32767]=19;if(p[0]!=71||p[32767]!=19)return false;
 bmalloc::vmDeallocate(p,32768);return true;
}
static void worker(){for(unsigned i=0;i<3;i++)if(!exercise())++failures;}
extern "C" int main(){
 for(unsigned cycle=0;cycle<3;cycle++){
  if(!exercise()){say("[FAIL] upstream bmalloc VM operations\n");return 1;}
  std::thread t[3];for(auto &v:t)v=std::thread(worker);for(auto &v:t)v.join();
  if(failures){say("[FAIL] upstream bmalloc VM workers\n");return 1;}
  chos_heap_stats_t stats;if(chos_libc_heap_info(&stats)||stats.allocations||stats.mapped_bytes){say("[FAIL] upstream VM worker heap cleanup\n");return 1;}
 }
 say("[ OK ] upstream bmalloc aligned VM, purge, revoke and cleanup passed\n");return 0;
}
