#include <chos/user.h>
#include <chos/libc.h>
#include <sys/mman.h>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstring>
#include <cstdlib>
static constexpr const char *path="/DOCUMENTS/WRITABLE.BIN";
static constexpr const char *quota_path="/DOCUMENTS/QUOTA.BIN";
static constexpr const char *abandon_path="/DOCUMENTS/ABANDON.BIN";
static bool failed(unsigned line){printf("[FAIL] native writable handles line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return failed(__LINE__);}while(0)
static unsigned char saved[8192],observed[8192];
static unsigned char *volatile abandoned_memory;
static bool file_info(int fd,uint64_t size,uint64_t position,bool dirty){
 chos_file_info_t value;memset(&value,0xa5,sizeof value);CHECK(!chos_file_info(fd,&value));
 CHECK(value.version==1&&value.size_bytes==64&&!value.reserved0&&value.length==size&&value.position==position);
 CHECK(value.flags==(CHOS_FILE_INFO_WORKING_COPY|CHOS_FILE_INFO_CHFS_SOURCE|(dirty?CHOS_FILE_INFO_DIRTY:0)));
 for(auto r:value.reserved)CHECK(!r);return true;
}
static bool read_all(int fd,unsigned char *out,size_t size){
 for(size_t at=0;at<size;){size_t n=size-at;if(n>256)n=256;CHECK(chos_read_at(fd,out+at,n,at)==(long)n);at+=n;}
 unsigned char guard=0xa5;CHECK(!chos_read_at(fd,&guard,1,size)&&guard==0xa5);return true;
}
static bool refusals(){
 chos_file_open_write_t r={.version=1,.size_bytes=32,.flags=CHOS_FILE_WRITE_CREATE,.capacity=65536,.reserved={0,0}};
 for(long size:{0L,31L,33L})CHECK(chos_syscall3(CHOS_SYS_FILE_OPEN_WRITE,(long)(uintptr_t)path,(long)(uintptr_t)&r,size)==-CHOS_ERRNO_EINVAL);
 CHECK(chos_syscall3(CHOS_SYS_FILE_OPEN_WRITE,(long)(uintptr_t)path,0,32)==-CHOS_ERRNO_EFAULT);
 r.version=0;CHECK(chos_syscall3(CHOS_SYS_FILE_OPEN_WRITE,(long)(uintptr_t)path,(long)(uintptr_t)&r,32)==-CHOS_ERRNO_EINVAL);r.version=1;
 r.size_bytes=31;CHECK(chos_syscall3(CHOS_SYS_FILE_OPEN_WRITE,(long)(uintptr_t)path,(long)(uintptr_t)&r,32)==-CHOS_ERRNO_EINVAL);r.size_bytes=32;
 r.reserved[1]=1;CHECK(chos_syscall3(CHOS_SYS_FILE_OPEN_WRITE,(long)(uintptr_t)path,(long)(uintptr_t)&r,32)==-CHOS_ERRNO_EINVAL);
 for(unsigned flags:{8u,CHOS_FILE_WRITE_EXCLUSIVE})CHECK(chos_open_write(path,flags,65536)==-CHOS_ERRNO_EINVAL);
 for(size_t capacity:{size_t(0),size_t(CHOS_FILE_WRITE_MAX_BYTES)+1,SIZE_MAX})CHECK(chos_open_write(path,1,capacity)==-CHOS_ERRNO_EINVAL);
 CHECK(chos_open_write(nullptr,1,65536)==-CHOS_ERRNO_EFAULT);
 CHECK(chos_open_write("/CFG",1,65536)==-CHOS_ERRNO_EPERM&&chos_open_write("/cfg/network.cfg",1,65536)==-CHOS_ERRNO_EPERM);
 CHECK(chos_open_write("/BIN/HELLO.CHX",1,65536)==-CHOS_ERRNO_EPERM);
 CHECK(chos_open_write("/DOCUMENTS/THIS_NAME_IS_MUCH_TOO_LONG_FOR_CHFS_123456",1,65536)==-CHOS_ERRNO_EINVAL);
 CHECK(chos_open_write("/",0,65536)==-CHOS_ERRNO_EISDIR);
 CHECK(chos_open(path)==-CHOS_ERRNO_ENOENT);return true;
}
static int shared_fd;static std::atomic<unsigned> errors,ready;static std::atomic<bool> release_workers;
static void append(unsigned id){
 for(unsigned n=0;n<128;n++){
  unsigned char record[16];record[0]=(unsigned char)id;record[1]=(unsigned char)n;
  for(unsigned i=2;i<16;i++)record[i]=(unsigned char)(id*31+n*17+i);
  if(chos_file_write(shared_fd,record,16)!=16){++errors;return;}
  if(!(n&15))std::this_thread::yield();
 }
}
static bool records(int fd){
 CHECK(read_all(fd,observed,6144));bool seen[3][128]={};
 for(unsigned offset=0;offset<6144;offset+=16){
  unsigned id=observed[offset],n=observed[offset+1];CHECK(id<3&&n<128&&!seen[id][n]);seen[id][n]=true;
  for(unsigned i=2;i<16;i++)CHECK(observed[offset+i]==(unsigned char)(id*31+n*17+i));
 }
 for(auto &row:seen)for(bool v:row)CHECK(v);return true;
}
static bool bad_buffers(int fd){
 CHECK(chos_seek(fd,100,0)==100);CHECK(read_all(fd,saved,8192));
 auto *map=(unsigned char *)mmap(nullptr,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(map!=MAP_FAILED);memset(map,0xa5,8192);
 CHECK(!mprotect(map+4096,4096,PROT_NONE));
 CHECK(chos_file_write(fd,map+4096-128,256)==-CHOS_ERRNO_EFAULT&&file_info(fd,8192,100,true));
 CHECK(chos_file_write(fd,nullptr,1)==-CHOS_ERRNO_EFAULT&&chos_file_write(fd,map,257)==-CHOS_ERRNO_E2BIG);
 CHECK(read_all(fd,observed,8192)&&!memcmp(saved,observed,8192));
 CHECK(!mprotect(map+4096,4096,PROT_READ));
 CHECK(chos_read_at(fd,map+4096-128,256,0)==-CHOS_ERRNO_EFAULT);
 for(unsigned i=0;i<8192;i++)CHECK(map[i]==0xa5);
 CHECK(!chos_file_write(fd,nullptr,0)&&file_info(fd,8192,100,true));
 uint64_t alias=(UINT64_C(1)<<32)|(unsigned)fd;
 for(unsigned operation:{CHOS_SYS_READ,CHOS_SYS_CLOSE,CHOS_SYS_FILE_WRITE,CHOS_SYS_FILE_TRUNCATE,CHOS_SYS_FILE_FLUSH,CHOS_SYS_FILE_ABORT})
  CHECK(chos_syscall3(operation,(long)alias,(long)(uintptr_t)map,1)==-CHOS_ERRNO_EBADF);
 CHECK(file_info(fd,8192,100,true));CHECK(!munmap(map,8192));return true;
}
static long quota_answers[3];
static void quota_worker(unsigned id){
 quota_answers[id]=chos_open_write(path,0,CHOS_FILE_WRITE_MAX_BYTES);++ready;
 while(!release_workers.load())std::this_thread::yield();
 if(quota_answers[id]>=3&&chos_file_abort((int)quota_answers[id]))++errors;
}
static bool quota(){
 ready=0;release_workers=false;errors=0;
 std::thread workers[3];for(unsigned i=0;i<3;i++)workers[i]=std::thread(quota_worker,i);
 while(ready.load()!=3)std::this_thread::yield();
 unsigned accepted=0,refused=0;
 for(long result:quota_answers){if(result>=3)++accepted;else if(result==-CHOS_ERRNO_ENOMEM)++refused;}
 /* Release/join before reporting a failure, so the fixture itself never leaves
  * live joinable threads or hides cleanup behind an assertion abort. */
 bool correct=accepted==2&&refused==1&&chos_open_write(quota_path,1,1)==-CHOS_ERRNO_ENOMEM&&chos_open(quota_path)==-CHOS_ERRNO_ENOENT&&chos_open(path)==-CHOS_ERRNO_ENOMEM;
 int rom=(int)chos_open("/BIN/HELLO.CHX");correct=correct&&rom>=3;if(rom>=3)correct=correct&&!chos_close(rom);
 release_workers=true;for(auto &t:workers)t.join();CHECK(correct&&!errors);
 int fd=(int)chos_open_write(quota_path,CHOS_FILE_WRITE_CREATE|CHOS_FILE_WRITE_EXCLUSIVE,1);CHECK(fd>=3);
 CHECK(chos_file_write(fd,"no",2)==-CHOS_ERRNO_ENOSPC&&chos_file_write(fd,"q",1)==1&&!chos_close(fd));
 CHECK(!chos_unlink(quota_path));return true;
}
static int flush_fds[2];static long flush_answers[2];
static void flush_worker(unsigned id){++ready;while(!release_workers.load())std::this_thread::yield();flush_answers[id]=chos_file_flush(flush_fds[id]);}
static bool conflict(){
 for(unsigned i=0;i<2;i++){flush_fds[i]=(int)chos_open_write(path,0,65536);CHECK(flush_fds[i]>=3&&chos_file_write(flush_fds[i],i?"B":"A",1)==1);}
 ready=0;release_workers=false;std::thread a(flush_worker,0),b(flush_worker,1);while(ready.load()!=2)std::this_thread::yield();release_workers=true;a.join();b.join();
 CHECK((!flush_answers[0]&&flush_answers[1]==-CHOS_ERRNO_ESTALE)||(!flush_answers[1]&&flush_answers[0]==-CHOS_ERRNO_ESTALE));
 unsigned winner=flush_answers[0]?1:0,loser=1-winner;
 CHECK(chos_close(flush_fds[loser])==-CHOS_ERRNO_ESTALE&&file_info(flush_fds[loser],8,1,true));
 unsigned char byte;CHECK(chos_read_at(flush_fds[loser],&byte,1,0)==1&&byte==(loser?'B':'A'));
 CHECK(!chos_file_abort(flush_fds[loser])&&!chos_close(flush_fds[winner]));
 int disk=(int)chos_open(path);CHECK(disk>=3&&chos_read(disk,&byte,1)==1&&byte==(winner?'B':'A')&&!chos_close(disk));return true;
}
static bool run(){
 CHECK(refusals());
 int fd=(int)chos_open_write(path,CHOS_FILE_WRITE_CREATE|CHOS_FILE_WRITE_EXCLUSIVE,65536);CHECK(fd>=3&&file_info(fd,0,0,false));shared_fd=fd;
 CHECK(chos_open_write(path,3,65536)==-CHOS_ERRNO_EEXIST);
 int original=(int)chos_open(path);CHECK(original>=3);
 CHECK(chos_file_write(original,"x",1)==-CHOS_ERRNO_EBADF&&chos_file_truncate(original,0)==-CHOS_ERRNO_EBADF);
 errors=0;std::thread workers[3];for(unsigned i=0;i<3;i++)workers[i]=std::thread(append,i);for(auto &t:workers)t.join();
 CHECK(!errors&&file_info(fd,6144,6144,true)&&records(fd)&&!chos_file_flush(fd)&&file_info(fd,6144,6144,false));
 unsigned char guard=0xa5;CHECK(!chos_read(original,&guard,1)&&guard==0xa5&&!chos_close(original));
 int immutable=(int)chos_open(path);CHECK(immutable>=3&&records(immutable));
 unsigned char prefix[3];memcpy(prefix,observed,sizeof prefix);
 CHECK(!chos_file_truncate(fd,3)&&file_info(fd,3,6144,true));
 CHECK(chos_seek(fd,4093,0)==4093&&chos_file_write(fd,"0123456789abcdef",16)==16&&!chos_file_truncate(fd,8192));
 CHECK(file_info(fd,8192,4109,true)&&read_all(fd,observed,8192)&&!memcmp(prefix,observed,sizeof prefix));
 for(unsigned i=3;i<8192;i++)CHECK(observed[i]==(i>=4093&&i<4109?(unsigned char)"0123456789abcdef"[i-4093]:0));
 CHECK(bad_buffers(fd));
 CHECK(chos_file_truncate(fd,65537)==-CHOS_ERRNO_ENOSPC&&chos_file_truncate(fd,UINT64_MAX)==-CHOS_ERRNO_EINVAL&&file_info(fd,8192,100,true));
 CHECK(read_all(fd,observed,8192)&&!memcmp(saved,observed,8192));
 CHECK(chos_seek(fd,INT64_MAX,0)==INT64_MAX&&chos_file_write(fd,"x",1)==-CHOS_ERRNO_ENOSPC&&file_info(fd,8192,INT64_MAX,true));
 CHECK(!chos_seek(fd,0,0)&&!chos_file_flush(fd)&&records(immutable)&&!chos_close(immutable));
 CHECK(chos_write_file(path,"external",8)==8&&chos_file_write(fd,"Z",1)==1&&chos_close(fd)==-CHOS_ERRNO_ESTALE&&file_info(fd,8192,1,true));
 CHECK(!chos_file_abort(fd)&&chos_file_flush(fd)==-CHOS_ERRNO_EBADF);
 CHECK(conflict()&&quota());
 int parent=(int)chos_open_write(path,0,65536);CHECK(parent>=3);
 for(const char *mode:{"no-write","no-open","no-read","foreign"}){
  uint64_t mask=CHOS_PROCESS_CAP_ALL;
  if(!strcmp(mode,"no-write"))mask&=~CHOS_PROCESS_CAP(CHOS_SYS_WRITE_FILE);
  if(!strcmp(mode,"no-open"))mask&=~CHOS_PROCESS_CAP(CHOS_SYS_OPEN);
  if(!strcmp(mode,"no-read"))mask&=~CHOS_PROCESS_CAP(CHOS_SYS_READ);
  long pid=chos_spawn_limited("/BIN/WRITE.ELF",mode,mask);CHECK(pid>0&&!chos_wait((uint32_t)pid));
 }
 CHECK(!chos_close(parent));
 fd=(int)chos_open_write(path,0,65536);CHECK(fd>=3&&!chos_rename(path,"/DOCUMENTS/MOVED.BIN")&&chos_file_write(fd,"x",1)==1&&chos_close(fd)==-CHOS_ERRNO_ESTALE&&!chos_file_abort(fd));
 CHECK(!chos_unlink("/DOCUMENTS/MOVED.BIN")&&chos_open(path)==-CHOS_ERRNO_ENOENT);
 CHECK(chos_write_file(abandon_path,"before",6)==6);
 long pid=chos_spawn_limited("/BIN/WRITE.ELF","abandon",CHOS_PROCESS_CAP_ALL);CHECK(pid>0&&chos_wait((uint32_t)pid)==73);
 fd=(int)chos_open(abandon_path);CHECK(fd>=3&&read_all(fd,observed,6)&&!memcmp(observed,"before",6)&&!chos_close(fd)&&!chos_unlink(abandon_path));
 chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&!heap.allocations&&!heap.mapped_bytes);
 puts("[ OK ] native writable working copies,conditional flush,quotas and cleanup passed");return true;
}
static int child(const char *mode){
 if(!strcmp(mode,"abandon")){
  int fd=(int)chos_open_write(abandon_path,0,65536);if(fd<3||chos_file_write(fd,"unsaved",7)!=7)return 1;
  abandoned_memory=new unsigned char[16000];abandoned_memory[0]=0x7a;
  chos_heap_stats_t allocated;if(chos_libc_heap_info(&allocated)||allocated.requested_bytes<16000)return 1;
  ready=0;
  std::thread worker([]{int other=(int)chos_open_write(abandon_path,0,65536);ready=other>=3&&chos_file_write(other,"worker",6)==6?1u:2u;for(;;)std::this_thread::yield();});
  while(!ready.load())std::this_thread::yield();_Exit(ready==1?73:1);
 }
 if(!strcmp(mode,"foreign")){
  if(chos_file_write(3,"x",1)!=-CHOS_ERRNO_EBADF||chos_file_truncate(3,0)!=-CHOS_ERRNO_EBADF||chos_file_flush(3)!=-CHOS_ERRNO_EBADF||chos_file_abort(3)!=-CHOS_ERRNO_EBADF)return 1;
 }else{
  if(chos_open_write(path,CHOS_FILE_WRITE_TRUNCATE,65536)!=-CHOS_ERRNO_EPERM)return 1;
  if(!strcmp(mode,"no-write")&&(chos_file_write(3,nullptr,1)!=-CHOS_ERRNO_EPERM||chos_file_truncate(3,0)!=-CHOS_ERRNO_EPERM||chos_file_flush(3)!=-CHOS_ERRNO_EPERM))return 1;
 }
 printf("[ OK ] writable descriptor permission/owner refusal %s\n",mode);return 0;
}
extern "C" int main(int argc,char **argv){return argc>1?child(argv[1]):(run()?0:1);}
