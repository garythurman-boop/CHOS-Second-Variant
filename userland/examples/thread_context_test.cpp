#include <chos/user.h>
#include <chos/libc.h>
#include <array>
#include <atomic>
#include <cstring>
#include <thread>
extern "C" long chos_capture_probe(chos_thread_context_t *,uint64_t);
extern "C" void chos_capture_probe_after(void);
extern "C" long chos_context_on_alternate_stack(chos_thread_context_t *,uint64_t);
static_assert(CHOS_SYS_THREAD_CONTEXT==66&&sizeof(chos_thread_context_t)==832,"assembly probe ABI");
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char b[80];unsigned n=0;const char *s="[FAIL] native thread context line ";while(*s)b[n++]=*s++;char d[16];unsigned i=0;do{d[i++]=(char)('0'+line%10);line/=10;}while(line);while(i)b[n++]=d[--i];b[n++]='\n';chos_write(b,n);return 1;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
alignas(16) static const unsigned char readonly_output[sizeof(chos_thread_context_t)]={1};
struct record {uint32_t tid;uint64_t base,bytes;};
static record workers[3];static std::atomic<unsigned> failures;
static int check_snapshot(uint64_t tag,record *save){
 struct alignas(16) {unsigned char before[16];chos_thread_context_t value;unsigned char after[16];} box;
 std::memset(&box,0xa5,sizeof(box));CHECK(!chos_capture_probe(&box.value,tag));auto &c=box.value;
 for(auto byte:box.before)CHECK(byte==0xa5);for(auto byte:box.after)CHECK(byte==0xa5);
 CHECK(c.version==1&&c.size_bytes==sizeof(c)&&c.flags==7&&!c.reserved&&c.tid==chos_thread_id());
 CHECK(c.stack_bytes>=4096&&c.stack_bytes<=CHOS_THREAD_STACK_MAX&&!(c.stack_base&4095)&&!(c.stack_bytes&4095));
 CHECK(c.stack_base<=reinterpret_cast<uintptr_t>(&box)&&reinterpret_cast<uintptr_t>(&box)+sizeof(box)<=c.stack_base+c.stack_bytes);
 CHECK(!(c.sp&15)&&c.sp>=c.stack_base&&c.sp<=c.stack_base+c.stack_bytes&&c.pc==reinterpret_cast<uintptr_t>(chos_capture_probe_after));
 CHECK(c.fpcr==((tag&3)<<22)&&c.fpsr==(UINT64_C(1)<<(tag&3)));
 CHECK(c.x[19]==tag&&c.x[20]==0x2020&&c.x[21]==0x2121&&c.x[22]==0x2222&&c.x[23]==0x2323&&c.x[24]==0x2424&&c.x[8]==CHOS_SYS_THREAD_CONTEXT);
 CHECK(c.x[0]==reinterpret_cast<uintptr_t>(&box.value)&&c.x[1]==sizeof(c)&&!c.x[2]);
 for(unsigned i=0;i<32;i++){
  uint64_t pattern=UINT64_C(0x0101010101010101)*(0x40+i);
  CHECK(c.q[i][0]==(i==0?tag:i==31?~tag:pattern)&&c.q[i][1]==(i==0?~tag:i==31?tag:pattern));
 }
 if(save)*save={c.tid,c.stack_base,c.stack_bytes};return 0;
}
static void worker(unsigned slot){
 for(unsigned n=0;n<100;n++){if(check_snapshot(UINT64_C(0xa000000000000000)+slot,&workers[slot])){++failures;return;}if(!(n%13))std::this_thread::yield();}
}
static int refusals(){
 chos_thread_context_t value,original;std::memset(&value,0xa5,sizeof(value));original=value;
 for(auto size:{size_t(0),sizeof(value)-1,sizeof(value)+1})CHECK(chos_syscall3(CHOS_SYS_THREAD_CONTEXT,reinterpret_cast<uintptr_t>(&value),size,0)==-CHOS_ERRNO_EINVAL&&!std::memcmp(&value,&original,sizeof(value)));
 CHECK(chos_syscall3(CHOS_SYS_THREAD_CONTEXT,reinterpret_cast<uintptr_t>(&value),sizeof(value),1)==-CHOS_ERRNO_EINVAL&&!std::memcmp(&value,&original,sizeof(value)));
 CHECK(chos_thread_context(nullptr)==-CHOS_ERRNO_EFAULT);
 CHECK(chos_thread_context(reinterpret_cast<chos_thread_context_t *>(UINTPTR_MAX-15))==-CHOS_ERRNO_EFAULT);
 CHECK(chos_thread_context(reinterpret_cast<chos_thread_context_t *>(const_cast<unsigned char *>(readonly_output)))==-CHOS_ERRNO_EFAULT);
 long region=chos_map_memory(4096);CHECK(region>0);auto *p=reinterpret_cast<unsigned char *>(region);std::memset(p+4096-16,0x73,16);
 CHECK(chos_thread_context(reinterpret_cast<chos_thread_context_t *>(p+4096-16))==-CHOS_ERRNO_EFAULT);
 for(unsigned i=0;i<16;i++)CHECK(p[4096-16+i]==0x73);
 CHECK(chos_context_on_alternate_stack(&value,region+4096)==-CHOS_ERRNO_EFAULT&&!std::memcmp(&value,&original,sizeof(value)));
 CHECK(!chos_unmap_memory(p,4096));return 0;
}
extern "C" int main(int argc,char **argv){
 if(argc>1&&!std::strcmp(argv[1],"deny-context")){
  chos_thread_context_t value,original;std::memset(&value,0xa5,sizeof(value));original=value;
  CHECK(chos_thread_context(&value)==-CHOS_ERRNO_EPERM&&!std::memcmp(&value,&original,sizeof(value)));return 42;
 }
 record main_record;CHECK(!check_snapshot(0xa111111111111111,&main_record)&&!refusals());
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread threads[3];for(unsigned i=0;i<3;i++)threads[i]=std::thread(worker,i);for(auto &t:threads)t.join();CHECK(!failures);
  for(unsigned i=0;i<3;i++){
   CHECK(workers[i].tid!=main_record.tid);
   CHECK(workers[i].base+workers[i].bytes<=main_record.base||main_record.base+main_record.bytes<=workers[i].base);
   for(unsigned j=0;j<i;j++)CHECK(workers[i].tid!=workers[j].tid&&(workers[i].base+workers[i].bytes<=workers[j].base||workers[j].base+workers[j].bytes<=workers[i].base));
  }
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);CHECK(!check_snapshot(0xa222222222222222,nullptr));
 }
 long pid=chos_spawn_limited("/BIN/THRCTX.ELF","deny-context",CHOS_PROCESS_CAP_ALL&~CHOS_PROCESS_CAP(CHOS_SYS_THREAD_INFO));CHECK(pid>0&&chos_wait(static_cast<uint32_t>(pid))==42);
 say("[ OK ] native current-thread stack, GPR and full SIMD context passed\n");return 0;
}
