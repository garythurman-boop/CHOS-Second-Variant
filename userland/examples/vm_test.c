#include "chos/user.h"
#include "chos/libc.h"
#include "chos/user_memory_limits.h"
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char s[80];unsigned at=0;const char *prefix="[FAIL] native virtual memory line ";while(*prefix)s[at++]=*prefix++;char digits[12];unsigned n=0;do{digits[n++]=(char)('0'+line%10);line/=10;}while(line);while(n)s[at++]=digits[--n];s[at++]='\n';chos_write(s,at);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static void expect_fault(void *p){
 const char hex[]="0123456789abcdef";char s[64]="[VM FAULT EXPECT] 0x";unsigned n=sizeof("[VM FAULT EXPECT] 0x")-1;
 for(int shift=60;shift>=0;shift-=4)s[n++]=hex[((uintptr_t)p>>(unsigned)shift)&15u];s[n++]='\n';chos_write(s,n);
}
static int exercise(void){
 const int flags=MAP_PRIVATE|MAP_ANON;
 unsigned char *p=mmap(NULL,5*4096,PROT_NONE,flags,-1,0);CHECK(p!=MAP_FAILED&&!( (uintptr_t)p&4095u));
 CHECK(chos_write(p,1)==-CHOS_ERRNO_EFAULT);
 CHECK(!mprotect(p,5*4096,3));for(unsigned i=0;i<5*4096;i++)CHECK(!p[i]);
 for(unsigned i=0;i<5*4096;i++)p[i]=(unsigned char)(i/4096+1);
 CHECK(!mprotect(p+4096,4096,1));CHECK(p[4096]==2);
 CHECK(chos_syscall2(CHOS_SYS_CLOCK_INFO,(long)(uintptr_t)(p+4096),48)==-CHOS_ERRNO_EFAULT);
 CHECK(!mprotect(p+4096,4096,0));CHECK(!madvise(p+4096,4096,MADV_DONTNEED));
 CHECK(chos_write(p+4096,1)==-CHOS_ERRNO_EFAULT);CHECK(!mprotect(p+4096,4096,3));
 for(unsigned i=0;i<4096;i++)CHECK(!p[4096+i]);CHECK(p[0]==1&&p[8192]==3);
 CHECK(!madvise(p,5*4096,MADV_NORMAL));CHECK(!madvise(p,4096,MADV_SEQUENTIAL));
 CHECK(!munmap(p+2*4096,1));CHECK(p[0]==1&&p[3*4096]==4&&p[4*4096]==5);
 CHECK(mprotect(p,5*4096,3)==-1&&errno==EINVAL);
 CHECK(munmap(p,5*4096)==-1&&errno==EINVAL);
 CHECK(!munmap(p,4096));CHECK(!munmap(p+4096,4096));
 CHECK(!munmap(p+4*4096,4096));CHECK(!munmap(p+3*4096,4096));
 CHECK(munmap(p,4096)==-1&&errno==EINVAL);return 0;
}
static void *worker(void *value){for(unsigned i=0;i<3;i++)if(exercise())return (void *)1;return value;}
static uint64_t nanoseconds(void){chos_clock_info_t t;if(chos_clock_status(&t)<0||!(t.flags&CHOS_CLOCK_MONOTONIC_VALID))return 0;return t.monotonic_seconds*UINT64_C(1000000000)+t.monotonic_nanoseconds;}
static void metric(const char *label,uint64_t value){
 char out[96]="[VM TIMING NS] ";unsigned at=sizeof("[VM TIMING NS] ")-1;const char hex[]="0123456789abcdef";
 while(*label)out[at++]=*label++;out[at++]=' ';out[at++]='0';out[at++]='x';
 for(int shift=60;shift>=0;shift-=4)out[at++]=hex[(value>>(unsigned)shift)&15u];out[at++]='\n';chos_write(out,at);
}
static int largest(void){
 uint64_t start=nanoseconds();CHECK(start);
 unsigned char *p=mmap(NULL,CHOS_USER_MAP_MAX_BYTES,0,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(p!=MAP_FAILED);metric("map-none",nanoseconds()-start);
 start=nanoseconds();CHECK(!mprotect(p,CHOS_USER_MAP_MAX_BYTES,3));metric("protect-rw",nanoseconds()-start);
 for(unsigned i=0;i<CHOS_USER_MAP_MAX_BYTES;i++){CHECK(!p[i]);p[i]=(unsigned char)(i*29u+7u);}
 CHECK(!mprotect(p,CHOS_USER_MAP_MAX_BYTES,0));
 start=nanoseconds();CHECK(!madvise(p,CHOS_USER_MAP_MAX_BYTES,MADV_DONTNEED));metric("discard-none",nanoseconds()-start);
 CHECK(chos_write(p,1)==-CHOS_ERRNO_EFAULT);CHECK(!mprotect(p,CHOS_USER_MAP_MAX_BYTES,1));
 for(unsigned i=0;i<CHOS_USER_MAP_MAX_BYTES;i++)CHECK(!p[i]);
 start=nanoseconds();CHECK(!munmap(p,CHOS_USER_MAP_MAX_BYTES));metric("unmap-read",nanoseconds()-start);return 0;
}
static int quota(void){
 void *regions[16];regions[0]=mmap(NULL,3*4096,3,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(regions[0]!=MAP_FAILED);
 for(unsigned i=1;i<16;i++){regions[i]=mmap(NULL,4096,3,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(regions[i]!=MAP_FAILED);}
 unsigned char *p=regions[0];p[4096]=99;
 CHECK(munmap(p+4096,4096)==-1&&errno==ENOMEM&&p[4096]==99);
 CHECK(!munmap(regions[15],4096));CHECK(!munmap(p+4096,4096));
 CHECK(!munmap(p,4096)&&!munmap(p+8192,4096));
 for(unsigned i=1;i<15;i++)CHECK(!munmap(regions[i],4096));return 0;
}
int main(int argc,char **argv){
 if(argc==3&&!strcmp(argv[1],"foreign")){
  uintptr_t pointer=0;CHECK(strlen(argv[2])==16);
  for(unsigned i=0;i<16;i++){char c=argv[2][i];CHECK((c>='0'&&c<='9')||(c>='a'&&c<='f'));pointer=(pointer<<4)|(uintptr_t)(c<='9'?c-'0':c-'a'+10);}
  CHECK(chos_memory_control((void *)pointer,4096,0)==-CHOS_ERRNO_EINVAL);
  CHECK(chos_unmap_range((void *)pointer,4096)==-CHOS_ERRNO_EINVAL);
  CHECK(chos_write((void *)pointer,1)==-CHOS_ERRNO_EFAULT);
  say("[ OK ] native virtual memory rejects foreign address-space ownership\n");return 0;
 }
 if(argc>1&&!strcmp(argv[1],"deny-control")){
  CHECK(chos_syscall3(CHOS_SYS_MEMORY_CONTROL,0,4096,3)==-CHOS_ERRNO_EPERM);
  CHECK(chos_syscall2(CHOS_SYS_UNMAP_RANGE,0,4096)==-CHOS_ERRNO_EPERM);
  say("[ OK ] native virtual memory reduced permissions deny control and unmap\n");return 0;
 }
 if(argc>1){
  unsigned char *p=mmap(NULL,3*4096,3,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(p!=MAP_FAILED);
  if(!strcmp(argv[1],"read-none")){CHECK(!mprotect(p,4096,0));expect_fault(p);volatile unsigned char v=*p;(void)v;}
  else if(!strcmp(argv[1],"write-ro")){CHECK(!mprotect(p,4096,1));expect_fault(p);*(volatile unsigned char *)p=1;}
  else if(!strcmp(argv[1],"execute-ro")){const unsigned char code[]={0xc0,3,0x5f,0xd6};memcpy(p,code,4);CHECK(!mprotect(p,4096,1));expect_fault(p);((void (*)(void))(uintptr_t)p)();}
  else if(!strcmp(argv[1],"unmapped-middle")){CHECK(!munmap(p+4096,4096));expect_fault(p+4096);volatile unsigned char v=*(p+4096);(void)v;}
  return fail(__LINE__);
 }
 CHECK(sysconf(_SC_PAGESIZE)==4096&&getpagesize()==4096&&sysconf(_SC_NPROCESSORS_ONLN)==1&&sysconf(_SC_CLK_TCK)==100);
 CHECK(sysconf(-1)==-1&&errno==EINVAL&&getpid()==chos_getpid());
 CHECK(mmap(NULL,0,3,MAP_PRIVATE|MAP_ANON,-1,0)==MAP_FAILED&&errno==EINVAL);
 CHECK(mmap(NULL,CHOS_USER_MAP_MAX_BYTES+1u,3,MAP_PRIVATE|MAP_ANON,-1,0)==MAP_FAILED&&errno==ENOMEM);
 CHECK(mmap(NULL,4096,7,MAP_PRIVATE|MAP_ANON,-1,0)==MAP_FAILED&&errno==EACCES);
 CHECK(mmap(NULL,4096,3,MAP_SHARED|MAP_ANON,-1,0)==MAP_FAILED&&errno==ENOTSUP);
 CHECK(mmap(NULL,4096,3,MAP_PRIVATE|MAP_FIXED|MAP_ANON,-1,0)==MAP_FAILED&&errno==ENOTSUP);
 CHECK(mmap(NULL,4096,3,MAP_PRIVATE,3,0)==MAP_FAILED&&errno==ENOTSUP);
 CHECK(mprotect((void *)((uintptr_t)main&~(uintptr_t)4095u),4096,3)==-1&&errno==EINVAL);
 CHECK(chos_syscall3(CHOS_SYS_MEMORY_CONTROL,0,4096,(long)UINT64_C(0x100000003))==-CHOS_ERRNO_EINVAL);
 CHECK(!exercise()&&!quota()&&!largest());
 pthread_t threads[3];for(uintptr_t i=0;i<3;i++)CHECK(!pthread_create(&threads[i],NULL,worker,(void *)(i+2)));
 for(uintptr_t i=0;i<3;i++){void *value=NULL;CHECK(!pthread_join(threads[i],&value)&&value==(void *)(i+2));}
 chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 int fd=(int)chos_open("/BIN/VM.ELF");CHECK(fd>=3);unsigned char buffer[256];
 CHECK(read(fd,buffer,1024)==256&&!memcmp(buffer,"\177ELF",4));CHECK(lseek(fd,0,SEEK_SET)==0&&read(fd,buffer,4)==4&&!memcmp(buffer,"\177ELF",4));
 CHECK(!close(fd));CHECK(read(fd,buffer,1)==-1&&errno==EBADF);CHECK(write(99,buffer,1)==-1&&errno==EBADF);
 unsigned char *owned=mmap(NULL,4096,3,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(owned!=MAP_FAILED);owned[0]=0x7e;
 char arguments[25]="foreign ";const char digits[]="0123456789abcdef";
 for(unsigned i=0;i<16;i++)arguments[8+i]=digits[((uintptr_t)owned>>(60u-4u*i))&15u];arguments[24]=0;
 long foreign=chos_spawn_limited("/BIN/VM.ELF",arguments,CHOS_PROCESS_CAP_ALL);CHECK(foreign>0&&chos_wait((uint32_t)foreign)==0&&owned[0]==0x7e);
 owned[0]=0x6d;CHECK(!munmap(owned,4096));
 uint64_t mask=CHOS_PROCESS_CAP_ALL&~(CHOS_PROCESS_CAP(CHOS_SYS_MAP_MEMORY)|CHOS_PROCESS_CAP(CHOS_SYS_UNMAP_MEMORY));
 long pid=chos_spawn_limited("/BIN/VM.ELF","deny-control",mask);CHECK(pid>0&&chos_wait((uint32_t)pid)==0);
 say("[ OK ] native anonymous mmap, protection, discard, split unmap and cleanup passed\n");return 0;
}
