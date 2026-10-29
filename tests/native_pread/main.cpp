#include <chos/user.h>
#include <chos/libc.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <thread>
#include <sys/mman.h>
static const char *path="/DOCUMENTS/PREAD.BIN";
static unsigned char bytes[65536];
static bool fail(unsigned line){printf("[FAIL] native positional-read line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static unsigned char expected(uint64_t i){return (unsigned char)((i*73u)^(i>>3u)^0xa7u);}
static bool info(int fd,uint64_t position,uint64_t size,bool persistent){
 chos_file_info_t value;memset(&value,0xa5,sizeof value);CHECK(!chos_file_info(fd,&value));
 CHECK(value.version==CHOS_FILE_INFO_VERSION&&value.size_bytes==sizeof(value)&&!value.reserved0);
 CHECK(value.flags==(CHOS_FILE_INFO_SNAPSHOT|CHOS_FILE_INFO_READ_ONLY|(persistent?CHOS_FILE_INFO_CHFS_SOURCE:0))&&value.position==position&&value.length==size);
 for(auto r:value.reserved)CHECK(!r);return true;
}
static bool refusal(int fd){
 alignas(16) unsigned char b[128],old[128];memset(b,0xa5,sizeof b);memcpy(old,b,sizeof b);
 for(long size:{0L,63L,65L})CHECK(chos_syscall3(CHOS_SYS_FILE_INFO,fd,(long)(uintptr_t)(b+16),size)==-CHOS_ERRNO_EINVAL&&!memcmp(b,old,sizeof b));
 CHECK(chos_syscall3(CHOS_SYS_FILE_INFO,INT64_MAX,(long)(uintptr_t)b,64)==-CHOS_ERRNO_EBADF&&!memcmp(b,old,sizeof b));
 for(uintptr_t p:{uintptr_t(0),uintptr_t(-32),uintptr_t(0x40000000)})CHECK(chos_syscall3(CHOS_SYS_FILE_INFO,fd,(long)p,64)==-CHOS_ERRNO_EFAULT);
 chos_file_read_at_t r={.offset=0,.buffer=(uintptr_t)b,.length=1,.flags=1,.reserved=0};
 CHECK(chos_syscall3(CHOS_SYS_READ_AT,fd,(long)(uintptr_t)&r,32)==-CHOS_ERRNO_EINVAL&&!memcmp(b,old,sizeof b));
 r.flags=0;r.reserved=1;CHECK(chos_syscall3(CHOS_SYS_READ_AT,fd,(long)(uintptr_t)&r,32)==-CHOS_ERRNO_EINVAL);
 r.reserved=0;r.offset=UINT64_MAX;CHECK(chos_syscall3(CHOS_SYS_READ_AT,fd,(long)(uintptr_t)&r,32)==-CHOS_ERRNO_EINVAL);
 r.offset=0;r.length=257;CHECK(chos_syscall3(CHOS_SYS_READ_AT,fd,(long)(uintptr_t)&r,32)==-CHOS_ERRNO_E2BIG);
 CHECK(chos_syscall3(CHOS_SYS_READ_AT,fd,0,32)==-CHOS_ERRNO_EFAULT);
 CHECK(chos_syscall3(CHOS_SYS_READ_AT,fd,(long)(uintptr_t)&r,31)==-CHOS_ERRNO_EINVAL);
 CHECK(chos_read_at(fd,b,SIZE_MAX,0)==-CHOS_ERRNO_E2BIG&&!memcmp(b,old,sizeof b));
 errno=0;CHECK(pread(fd,b,1,-1)==-1&&errno==EINVAL&&!memcmp(b,old,sizeof b));
 auto *map=static_cast<unsigned char *>(mmap(nullptr,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));CHECK(map!=MAP_FAILED);
 memset(map,0xa5,8192);CHECK(!mprotect(map+4096,4096,PROT_READ));
 CHECK(chos_syscall3(CHOS_SYS_FILE_INFO,fd,(long)(uintptr_t)(map+4096-32),64)==-CHOS_ERRNO_EFAULT);
 CHECK(chos_read_at(fd,map+4096-128,256,0)==-CHOS_ERRNO_EFAULT);
 CHECK(chos_read(fd,map+4096-128,256)==-CHOS_ERRNO_EFAULT&&info(fd,0,sizeof bytes,true));
 for(unsigned i=0;i<8192;i++)CHECK(map[i]==0xa5);
 CHECK(chos_read_at(fd,map+4096-16,256,sizeof bytes-16)==16);
 for(unsigned i=0;i<8192;i++)CHECK(map[i]==(i>=4080&&i<4096?expected(sizeof bytes-16+i-4080):0xa5));
 CHECK(!munmap(map,8192));return true;
}
static std::atomic<unsigned> failures;static int shared_fd;
static void reader(unsigned id){
 uint64_t seed=0x5147236u+id;unsigned char result[258];
 for(unsigned n=0;n<200;n++){
  seed=seed*6364136223846793005ull+1;uint64_t offset=(seed>>24)%66000;
  size_t length=(seed>>43)%500;memset(result,0xa5,sizeof result);
  errno=EBUSY;ssize_t got=pread(shared_fd,result+1,length,(off_t)offset);
  size_t want=offset>=sizeof bytes?0:sizeof bytes-(size_t)offset;if(want>length)want=length;if(want>256)want=256;
  bool ok=got==(ssize_t)want&&errno==EBUSY&&result[0]==0xa5&&result[want+1]==0xa5;
  for(size_t i=0;i<want;i++)if(result[i+1]!=expected(offset+i))ok=false;
  if(!ok){++failures;return;}if((n&15u)==0u)std::this_thread::yield();
 }
}
static bool snapshots(int fd){
 CHECK(!chos_seek(fd,0,CHOS_SEEK_SET));
 for(size_t i=0;i<sizeof bytes;i++)bytes[i]=(unsigned char)~expected(i);
 CHECK(chos_write_file(path,bytes,sizeof bytes)==(long)sizeof bytes);
 unsigned char out[256];CHECK(chos_read_at(fd,out,sizeof out,123)==256);
 for(unsigned i=0;i<256;i++)CHECK(out[i]==expected(i+123));
 int other=(int)chos_open(path);CHECK(other>=3&&info(other,0,sizeof bytes,true));
 CHECK(chos_read_at(other,out,sizeof out,123)==256);
 for(unsigned i=0;i<256;i++)CHECK(out[i]==(unsigned char)~expected(i+123));
 CHECK(!chos_unlink(path)&&info(fd,0,sizeof bytes,true)&&info(other,0,sizeof bytes,true));
 CHECK(chos_read_at(fd,out,sizeof out,65500)==36);
 for(unsigned i=0;i<36;i++)CHECK(out[i]==expected(65500+i));
 CHECK(!chos_close(other));return true;
}
static bool run(){
 CHECK(chos_open(path)==-CHOS_ERRNO_ENOENT);
 for(size_t i=0;i<sizeof bytes;i++)bytes[i]=expected(i);
 CHECK(chos_write_file(path,bytes,sizeof bytes)==(long)sizeof bytes);shared_fd=(int)chos_open(path);CHECK(shared_fd>=3);
 CHECK(info(shared_fd,0,sizeof bytes,true)&&refusal(shared_fd)&&info(shared_fd,0,sizeof bytes,true));
 CHECK(chos_seek(shared_fd,137,CHOS_SEEK_SET)==137);
 for(unsigned cycle=0;cycle<3;cycle++){
  failures=0;std::thread workers[3];for(unsigned i=0;i<3;i++)workers[i]=std::thread(reader,i);for(auto &t:workers)t.join();
  CHECK(!failures&&info(shared_fd,137,sizeof bytes,true));
  chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&!heap.allocations&&!heap.mapped_bytes);
 }
 unsigned char byte;CHECK(chos_read(shared_fd,&byte,1)==1&&byte==expected(137)&&info(shared_fd,138,sizeof bytes,true));
 CHECK(!pread(shared_fd,nullptr,0,INT64_MAX)&&!pread(shared_fd,nullptr,1,INT64_MAX)&&info(shared_fd,138,sizeof bytes,true));
 for(const char *mode:{"foreign","denied"}){
  uint64_t mask=CHOS_PROCESS_CAP_ALL;if(!strcmp(mode,"denied"))mask&=~CHOS_PROCESS_CAP(CHOS_SYS_READ);
  long child=chos_spawn_limited("/BIN/PREAD.ELF",mode,mask);CHECK(child>0&&!chos_wait((uint32_t)child));
 }
 CHECK(snapshots(shared_fd)&&!chos_close(shared_fd));
 chos_file_info_t missing;CHECK(chos_file_info(shared_fd,&missing)==-CHOS_ERRNO_EBADF);
 int rom=(int)chos_open("/BIN/HELLO.CHX");CHECK(rom>=3);chos_file_info_t ri;CHECK(!chos_file_info(rom,&ri)&&ri.length);
 CHECK(info(rom,0,ri.length,false)&&!chos_close(rom));
 puts("[ OK ] native snapshot metadata and cursor-independent positional reads passed");return true;
}

extern "C" int main(int argc,char **argv){
 if(argc>1){
  bool denied=!strcmp(argv[1],"denied");int fd=denied?(int)chos_open(path):3;
  if(denied&&fd<3)return 1;
  chos_file_info_t value,old;memset(&value,0xa5,sizeof value);old=value;unsigned char b[2]={0xa5,0xa5};
  long error=denied?-CHOS_ERRNO_EPERM:-CHOS_ERRNO_EBADF;
  if(chos_file_info(fd,&value)!=error||memcmp(&value,&old,sizeof value)||chos_read_at(fd,b,1,0)!=error||b[0]!=0xa5)return 1;
  if(denied&&chos_close(fd))return 1;
  puts(denied?"[ OK ] READ permission denies snapshot metadata and positional read":"[ OK ] child cannot use parent snapshot descriptor");return 0;
 }
 return run()?0:1;
}
