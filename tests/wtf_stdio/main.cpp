#include "config.h"
#include <wtf/FilePrintStream.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <stdio.h>
#include <errno.h>
#include <cstring>
#include <thread>
#include <atomic>
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char b[80];int n=snprintf(b,sizeof b,"[FAIL] native output line %u\n",line);if(n>0)chos_write(b,static_cast<size_t>(n));return 1;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static std::atomic<unsigned> failures;
static void worker(unsigned id){for(unsigned n=0;n<6;n++){
 if(fprintf(stdout,"[STREAM] worker=%u item=%u complete\n",id,n)!=34)++failures;
 std::this_thread::yield();
}}
extern "C" int main(int argc,char **argv){
 if(argc>1){
  if(!std::strcmp(argv[1],"deny-write")){
   CHECK(fprintf(stderr,"FORBIDDEN")<0&&errno==EPERM&&ferror(stderr));clearerr(stderr);
   CHECK(fputc('x',stdout)==EOF&&errno==EPERM&&ferror(stdout));
   return 0;
  }
  CHECK(!fclose(stdout)&&printf("FORBIDDEN")<0&&errno==EBADF);
  CHECK(fputs("[STREAM] stderr survives stdout closure\n",stderr)>=0);return 0;
 }
 CHECK(fileno(stdout)==1&&fileno(stderr)==2&&!setvbuf(stderr,nullptr,_IONBF,0));
 CHECK(setvbuf(stdout,nullptr,_IOLBF,256)<0&&errno==ENOTSUP);
 CHECK(printf("[STREAM] native %04x %.2f %lld\n",42,1.25,-123LL)==31);
 CHECK(fprintf(stderr,"[STREAM] native stderr\n")==23);
 CHECK(fprintf(stdout,"[STREAM] binary128 %.4Lf count=%zu utf8=%s\n",1.234375L,size_t(512),"caf\xc3\xa9")==47);
 {WTF::FilePrintStream real(stdout,WTF::FilePrintStream::Borrow);
  real.printf("[STREAM] actual upstream FilePrintStream %d %.2f\n",42,1.25);real.flush();}
 CHECK(!fputs("[STREAM] borrowed stream remains open\n",stdout));
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread threads[3];for(unsigned i=0;i<3;i++)threads[i]=std::thread(worker,i);
  for(auto &t:threads)t.join();CHECK(!failures);
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 FILE *f=fopen("/BIN/WTFOUT.ELF","rb");CHECK(f);unsigned char elf[4];
 CHECK(fread(elf,1,4,f)==4&&!std::memcmp(elf,"\177ELF",4));
 CHECK(fprintf(f,"FORBIDDEN")<0&&errno==EBADF&&ferror(f));
 clearerr(f);CHECK(!fseek(f,0,SEEK_SET)&&fread(elf,1,4,f)==4&&!fclose(f));
 for(const char *mode:{"deny-write","close-output"}){
  say("[EXPECT] native output child refusal\n");
  uint64_t caps=CHOS_PROCESS_CAP_ALL;
  if(!std::strcmp(mode,"deny-write"))caps&=~CHOS_PROCESS_CAP(CHOS_SYS_WRITE);
  long pid=chos_spawn_limited("/BIN/WTFOUT.ELF",mode,caps);CHECK(pid>0&&chos_wait(static_cast<uint32_t>(pid))==0);
 }
 CHECK(!fflush(stdout)&&!fflush(stderr)&&!fflush(nullptr));
 say("[ OK ] actual WTF output and native standard streams passed\n");return 0;
}
