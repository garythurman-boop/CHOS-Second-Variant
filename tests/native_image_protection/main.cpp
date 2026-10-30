#include <chos/user.h>
#include <chos/libc.h>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <cerrno>
#include <thread>
#include <atomic>
alignas(4096) static unsigned char data_pages[3*4096];
static bool fail(unsigned n){printf("[FAIL] native image protection line %u errno %d\n",n,errno);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static long protect(void *p,uint64_t bytes,uint64_t op){return chos_syscall3(CHOS_SYS_IMAGE_PROTECT,(long)(uintptr_t)p,(long)bytes,(long)op);}
static int child(const char *mode){
 data_pages[0]=0x67;
 if(!strcmp(mode,"deny")){
  if(protect(data_pages,4096,CHOS_MEMORY_PROTECT_READ)!=-CHOS_ERRNO_EPERM||mprotect(data_pages,4096,PROT_READ)!=-1||errno!=EPERM)return 1;
  data_pages[0]=0x71;puts("[ OK ] native image protection capability refusal");return 0;
 }
 if(mprotect(data_pages,4096,!strcmp(mode,"ro-write")?PROT_READ:PROT_NONE))return 1;
 if(!strcmp(mode,"ro-write")){auto *p=(volatile unsigned char *)data_pages;*p=0x42;}
 else{volatile unsigned char value=*(volatile unsigned char *)data_pages;(void)value;}
 puts("[FAIL] forbidden image access continued");return 1;
}
static std::atomic<unsigned> errors;
static bool worker(){
 for(unsigned n=0;n<200;n++){
  if(mprotect(data_pages,4096,PROT_READ)||mprotect(data_pages,4096,PROT_READ|PROT_WRITE)!=-1||errno!=EINVAL)return false;
  unsigned char value=data_pages[0];if(value!=0xa5)return false;
 }
 return true;
}
static bool run(){
 memset(data_pages,0xa5,sizeof data_pages);
 for(uint64_t mode:{UINT64_C(2),UINT64_C(3),UINT64_C(4),UINT64_C(256),UINT64_C(0x100000001),UINT64_MAX})CHECK(protect(data_pages,4096,mode)==-CHOS_ERRNO_EINVAL);
 CHECK(protect(data_pages,0,1)==-CHOS_ERRNO_EINVAL&&protect(data_pages,CHOS_IMAGE_PROTECT_MAX_BYTES+1u,1)==-CHOS_ERRNO_EINVAL);
 CHECK(protect(data_pages+1,4096,1)==-CHOS_ERRNO_EINVAL&&protect((void *)(UINT64_MAX-4095),4096,1)==-CHOS_ERRNO_EINVAL);
 // Invalid suffix must not seal an otherwise writable prefix.
 CHECK(protect(data_pages,CHOS_IMAGE_PROTECT_MAX_BYTES,1)==-CHOS_ERRNO_EINVAL);data_pages[0]=0x5a;CHECK(data_pages[0]==0x5a);data_pages[0]=0xa5;
 int local=0;CHECK(protect((void *)((uintptr_t)&local&~uintptr_t(4095)),4096,1)==-CHOS_ERRNO_EINVAL);
 CHECK(protect((void *)UINT64_C(0x40000000),4096,1)==-CHOS_ERRNO_EINVAL);
 CHECK(protect((void *)UINT64_C(0x20000000),4096,1)==-CHOS_ERRNO_EINVAL); // Actual RX code is refused.
 void *anon=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(anon!=MAP_FAILED);
 CHECK(protect(anon,4096,1)==-CHOS_ERRNO_EINVAL&&!mprotect(anon,4096,PROT_READ)&&!mprotect(anon,4096,PROT_READ|PROT_WRITE)&&!munmap(anon,4096));
 CHECK(!mprotect(data_pages,1,PROT_READ)&&data_pages[0]==0xa5);
 CHECK(mprotect(data_pages,4096,PROT_READ|PROT_WRITE)==-1&&errno==EINVAL);
 int fd=(int)chos_open("/BIN/HELLO.CHX");CHECK(fd>=3);
 CHECK(chos_read(fd,data_pages,1)==-CHOS_ERRNO_EFAULT&&chos_seek(fd,0,CHOS_SEEK_CUR)==0&&data_pages[0]==0xa5&&!chos_close(fd));
 CHECK(!mprotect(data_pages+4096,4096,PROT_NONE));
 CHECK(protect(data_pages+4096,4096,CHOS_MEMORY_PROTECT_READ)==-CHOS_ERRNO_EPERM);
 CHECK(mprotect(data_pages+4096,4096,PROT_READ)==-1&&errno==EPERM);
 CHECK(chos_write(data_pages+4096,1)==-CHOS_ERRNO_EFAULT);
 data_pages[8192]=0x39;CHECK(data_pages[8192]==0x39);
 {std::thread a([]{if(!worker())++errors;}),b([]{if(!worker())++errors;}),c([]{if(!worker())++errors;});a.join();b.join();c.join();}CHECK(!errors);
 for(const char *mode:{"ro-write","none-read","deny"}){
  uint64_t mask=CHOS_PROCESS_CAP_ALL;if(!strcmp(mode,"deny"))mask&=~CHOS_PROCESS_CAP(CHOS_SYS_MAP_MEMORY);
  long pid=chos_spawn_limited("/IMAGEPROT.ELF",mode,mask);CHECK(pid>0);long result=chos_wait((uint32_t)pid);
  if(!strcmp(mode,"deny"))CHECK(!result&&chos_getstatus(pid)==CHOS_STATUS_EXITED);
  else CHECK(result==(-128-0x24)&&chos_getstatus(pid)==CHOS_STATUS_FAILED);
 }
 CHECK(data_pages[0]==0xa5&&data_pages[8192]==0x39);
 chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&!heap.allocations&&!heap.mapped_bytes);
 puts("[ OK ] native one-way image permissions,owned ranges and isolated faults passed");return true;
}
int main(int argc,char **argv){return argc>1?child(argv[1]):(run()?0:1);}
