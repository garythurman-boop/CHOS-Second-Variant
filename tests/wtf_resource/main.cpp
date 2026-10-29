#include "config.h"
#include <wtf/CPUTime.h>
#include <wtf/MemoryFootprint.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <atomic>
#include <thread>
#include <cstring>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
static bool fail(unsigned line){printf("[FAIL] native resource line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static double total(const chos_resource_info_t &v){return double(v.process_user_ticks)/double(v.counter_frequency)+double(v.process_system_ticks)/double(v.counter_frequency);}
static double own(const chos_resource_info_t &v){return double(v.thread_user_ticks)/double(v.counter_frequency)+double(v.thread_system_ticks)/double(v.counter_frequency);}
static bool snapshot(chos_resource_info_t &v){
 CHECK(!chos_resource_info(&v)&&v.version==CHOS_RESOURCE_VERSION&&v.size_bytes==sizeof(v));
 CHECK(v.flags==(CHOS_RESOURCE_MEMORY_VALID|CHOS_RESOURCE_CPU_VALID)&&v.counter_frequency&&!v.reserved0);
 for(auto r:v.reserved)CHECK(!r);
 CHECK(v.resident_bytes&&!(v.resident_bytes&4095)&&v.mapped_bytes<=v.resident_bytes);
 CHECK(v.process_user_ticks>=v.thread_user_ticks&&v.process_system_ticks>=v.thread_system_ticks);return true;
}
static bool burn(double seconds){
 auto start=WTF::MonotonicTime::now();
 volatile uint64_t value=7;
 do {for(unsigned i=0;i<4000;i++)value=value*6364136223846793005ull+1;}while((WTF::MonotonicTime::now()-start).value()<seconds);
 return value!=0;
}
static bool refused_outputs(){
 alignas(16) unsigned char buffer[128],old[128];memset(buffer,0xa5,sizeof buffer);memcpy(old,buffer,sizeof old);
 for(long size:{0L,95L,97L,4096L})CHECK(chos_syscall3(CHOS_SYS_RESOURCE_INFO,(long)(uintptr_t)(buffer+16),size,0)==-CHOS_ERRNO_EINVAL&&!memcmp(buffer,old,sizeof old));
 CHECK(chos_syscall3(CHOS_SYS_RESOURCE_INFO,(long)(uintptr_t)(buffer+16),96,1)==-CHOS_ERRNO_EINVAL&&!memcmp(buffer,old,sizeof old));
 for(uintptr_t p:{uintptr_t(0),uintptr_t(-48),uintptr_t(0x40000000)})CHECK(chos_syscall3(CHOS_SYS_RESOURCE_INFO,(long)p,96,0)==-CHOS_ERRNO_EFAULT);
 auto *map=static_cast<unsigned char *>(mmap(nullptr,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));CHECK(map!=MAP_FAILED);
 memset(map,0xa5,8192);CHECK(!mprotect(map+4096,4096,PROT_READ));
 CHECK(chos_syscall3(CHOS_SYS_RESOURCE_INFO,(long)(uintptr_t)(map+4096-48),96,0)==-CHOS_ERRNO_EFAULT);
 for(unsigned i=0;i<8192;i++)CHECK(map[i]==0xa5);
 CHECK(chos_syscall3(CHOS_SYS_RESOURCE_INFO,(long)(uintptr_t)(map+4096),96,0)==-CHOS_ERRNO_EFAULT);
 CHECK(!munmap(map,8192));
 CHECK(!chos_resource_info(reinterpret_cast<chos_resource_info_t *>(buffer+16)));
 CHECK(buffer[15]==0xa5&&buffer[112]==0xa5);return true;
}
static bool memory(){
 chos_resource_info_t before,during,protected_pages,after;CHECK(snapshot(before));
 CHECK(WTF::memoryFootprint()==before.resident_bytes);
 auto *p=static_cast<unsigned char *>(mmap(nullptr,12288,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));CHECK(p!=MAP_FAILED);
 p[0]=17;p[12287]=33;CHECK(snapshot(during));
 CHECK(during.resident_bytes==before.resident_bytes+12288&&during.mapped_bytes==before.mapped_bytes+12288);
 CHECK(WTF::memoryFootprint()==during.resident_bytes);
 CHECK(!mprotect(p+4096,4096,PROT_NONE)&&snapshot(protected_pages));
 CHECK(protected_pages.resident_bytes==during.resident_bytes&&protected_pages.mapped_bytes==during.mapped_bytes);
 CHECK(!madvise(p,4096,MADV_DONTNEED)&&!p[0]);
 CHECK(!munmap(p,12288)&&snapshot(after));
 CHECK(after.resident_bytes==before.resident_bytes&&after.mapped_bytes==before.mapped_bytes);return true;
}
static bool main_cpu(){
 chos_resource_info_t a,b,c;CHECK(snapshot(a));auto engine_a=WTF::CPUTime::get();CHECK(engine_a);
 auto thread_a=WTF::CPUTime::forCurrentThread();auto start=WTF::MonotonicTime::now();CHECK(burn(.15));
 auto engine_b=WTF::CPUTime::get();CHECK(engine_b);CHECK(snapshot(b));
 double elapsed=(WTF::MonotonicTime::now()-start).value(),cpu=own(b)-own(a);
 CHECK(cpu>.005&&cpu<=elapsed+.03&&b.thread_user_ticks>a.thread_user_ticks&&b.thread_system_ticks>a.thread_system_ticks);
 CHECK(WTF::CPUTime::forCurrentThread()>thread_a);
 CHECK(engine_b->percentageCPUUsageSince(*engine_a)>0&&engine_b->percentageCPUUsageSince(*engine_a)<110);
 start=WTF::MonotonicTime::now();CHECK(!sleep(1)&&snapshot(c));elapsed=(WTF::MonotonicTime::now()-start).value();
 CHECK(elapsed>=1&&elapsed<5&&own(c)-own(b)<elapsed*.25&&total(c)-total(b)<elapsed*.25);
 printf("[MEASURE] main active %.6f CPU/%.6f wall; sleep CPU %.6f\n",cpu,(engine_b->cpuTime-engine_a->cpuTime).value(),own(c)-own(b));return true;
}
static std::atomic<unsigned> ready,done,failures;static std::atomic<bool> go;
static double worker_cpu[3];static uint64_t worker_user[3],worker_system[3];
static void worker(unsigned id){
 chos_resource_info_t a,b;bool good=snapshot(a);++ready;while(!go)std::this_thread::yield();
 if(id==0)good=good&&!sleep(1);else good=good&&burn(.18);
 good=good&&snapshot(b);
 if(good){worker_cpu[id]=own(b)-own(a);worker_user[id]=b.thread_user_ticks;worker_system[id]=b.thread_system_ticks;
  good=worker_cpu[id]>=0&&(id==0?worker_cpu[id]<.2:worker_cpu[id]>.005);
 }
 if(!good)++failures;++done;
}
static bool threads(){
 for(unsigned cycle=0;cycle<3;cycle++){
  ready=done=failures=0;go=false;chos_resource_info_t before,after,retained;CHECK(snapshot(before));
  std::thread workers[3];for(unsigned i=0;i<3;i++)workers[i]=std::thread(worker,i);
  while(ready!=3)std::this_thread::yield();go=true;
  for(auto &t:workers)t.join();CHECK(!failures&&done==3&&snapshot(after));
  uint64_t user=0,system=0;for(unsigned i=0;i<3;i++){user+=worker_user[i];system+=worker_system[i];}
  CHECK(after.process_user_ticks>=user+after.thread_user_ticks&&after.process_system_ticks>=system+after.thread_system_ticks);
  CHECK(total(after)-total(before)>.01&&own(after)-own(before)<.3);
  CHECK(!chos_sleep_ticks(2)&&snapshot(retained));CHECK(retained.process_user_ticks>=after.process_user_ticks&&retained.process_system_ticks>=after.process_system_ticks);
  printf("[MEASURE] workers sleep %.6f burn %.6f %.6f; process retains exited threads\n",worker_cpu[0],worker_cpu[1],worker_cpu[2]);
  chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&!heap.allocations&&!heap.mapped_bytes);
 }return true;
}
extern "C" int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"denied")){
  chos_resource_info_t value,old;memset(&value,0xa5,sizeof value);old=value;
  if(chos_resource_info(&value)!=-CHOS_ERRNO_EPERM||memcmp(&value,&old,sizeof value)||WTF::CPUTime::get())return 1;
  puts("[ OK ] denied native resources preserve output and optional CPU refusal");return 0;
 }
 if(argc>1&&!strcmp(argv[1],"isolated")){
  chos_resource_info_t value;if(!snapshot(value)||total(value)>.1||own(value)>.1)return 1;
  puts("[ OK ] new native process has independent generation-owned CPU totals");return 0;
 }
 chos_resource_info_t startup;
 if(!snapshot(startup)||total(startup)>.1||own(startup)>.1||
    startup.process_user_ticks!=startup.thread_user_ticks||startup.process_system_ticks!=startup.thread_system_ticks)return 1;
 if(!refused_outputs()||!memory()||!main_cpu()||!threads())return 1;
 for(const char *mode:{"isolated","denied"}){
  uint64_t mask=CHOS_PROCESS_CAP_ALL;if(!strcmp(mode,"denied"))mask&=~CHOS_PROCESS_CAP(CHOS_SYS_THREAD_INFO);
  long pid=chos_spawn_limited("/BIN/WTFRES.ELF",mode,mask);if(pid<=0||chos_wait(static_cast<uint32_t>(pid)))return 1;
 }
 puts("[ OK ] native WTF CPU and memory measurements passed");return 0;
}
