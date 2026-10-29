#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include "chos/user_memory_limits.h"
#include "chos/memory_api.h"
#include "chos/file_api.h"
static int error;
int *__chos_errno_location(void){return &error;}
static unsigned maps,unmaps,controls,last_operation;static size_t last_bytes;static long map_result=0x21001000,control_result,unmap_result;
long chos_mman_map(size_t n){maps++;last_bytes=n;return map_result;}
long chos_mman_unmap(void *p,size_t n){assert(p==(void *)0x21001000);unmaps++;last_bytes=n;return unmap_result;}
long chos_mman_control(void *p,size_t n,unsigned op){assert(p==(void *)0x21001000);controls++;last_bytes=n;last_operation=op;return control_result;}
static long io_result;static size_t io_bytes;static int io_fd;static int64_t io_offset;static unsigned io_origin;
long chos_posix_getpid(void){return io_result;}
long chos_posix_read(int fd,void *p,size_t n){(void)p;io_fd=fd;io_bytes=n;return io_result;}
long chos_posix_write(const void *p,size_t n){(void)p;io_bytes=n;return io_result;}
long chos_posix_close(int fd){io_fd=fd;return io_result;}
static uint64_t sleep_ticks;static unsigned sleep_calls,fail_sleep_call;
long chos_posix_sleep(uint64_t n){sleep_ticks=n;++sleep_calls;return sleep_calls==fail_sleep_call?-ENODEV:io_result;}
long chos_posix_seek(int fd,int64_t off,unsigned origin){io_fd=fd;io_offset=off;io_origin=origin;return io_result;}
int main(void){
 const int flags=MAP_PRIVATE|MAP_ANON;void *p=(void *)0x21001000;
 assert(mmap(NULL,0,3,flags,-1,0)==MAP_FAILED&&errno==EINVAL&&!maps);
 assert(mmap(NULL,CHOS_USER_MAP_MAX_BYTES+1u,3,flags,-1,0)==MAP_FAILED&&errno==ENOMEM&&!maps);
 assert(mmap(NULL,4096,7,flags,-1,0)==MAP_FAILED&&errno==EACCES&&!maps);
 assert(mmap(NULL,4096,2,flags,-1,0)==MAP_FAILED&&errno==EINVAL&&!maps);
 const int unsupported[]={MAP_SHARED|MAP_ANON,MAP_PRIVATE,flags|MAP_FIXED,flags|MAP_FIXED_NOREPLACE};
 for(unsigned i=0;i<4;i++)assert(mmap(NULL,4096,3,unsupported[i],-1,0)==MAP_FAILED&&errno==ENOTSUP&&!maps);
 assert(mmap(NULL,4096,3,flags|0x100,-1,0)==MAP_FAILED&&errno==EINVAL&&!maps);
 assert(mmap(NULL,4096,3,flags,1,0)==MAP_FAILED&&errno==EINVAL&&!maps);
 assert(mmap(NULL,4096,3,flags,-1,1)==MAP_FAILED&&errno==EINVAL&&!maps);
 map_result=-ENOMEM;assert(mmap(NULL,4096,3,flags,-1,0)==MAP_FAILED&&errno==ENOMEM&&maps==1);
 map_result=(long)p;assert(mmap((void *)1,1,3,flags,-1,0)==p&&last_bytes==1&&!controls);
 assert(mmap(NULL,4096,0,flags|MAP_NORESERVE,-1,0)==p&&controls==1&&last_operation==0);
 assert(mmap(NULL,4096,1,flags,-1,0)==p&&last_operation==1);
 control_result=-EBUSY;unmap_result=-EIO;
 assert(mmap(NULL,4096,0,flags,-1,0)==MAP_FAILED&&errno==EBUSY&&unmaps==1);
 unsigned before=controls;assert(mprotect(p,4096,4)==-1&&errno==EACCES&&controls==before);
 assert(mprotect(p,4096,3)==-1&&errno==EBUSY&&last_operation==3);
 assert(munmap(p,1)==-1&&errno==EIO&&last_bytes==1);
 control_result=unmap_result=0;
 assert(!mprotect(p,4096,0)&&!munmap(p,4096));
 assert(!madvise(p,4096,MADV_DONTNEED)&&last_operation==CHOS_MEMORY_DISCARD);
 for(int advice=0;advice<4;advice++)assert(!madvise(p,4096,advice)&&last_operation==CHOS_MEMORY_VALIDATE);
 before=controls;assert(madvise(p,4096,99)==-1&&errno==EINVAL&&controls==before);
 assert(sysconf(_SC_PAGESIZE)==4096&&getpagesize()==4096);
 assert(sysconf(_SC_NPROCESSORS_ONLN)==1&&sysconf(_SC_NPROCESSORS_CONF)==1&&sysconf(_SC_CLK_TCK)==100&&sysconf(_SC_OPEN_MAX)==CHOS_FILE_OPEN_MAX);
 assert(sysconf(-1)==-1&&errno==EINVAL);io_result=123;assert(getpid()==123);
 io_result=INT64_MAX;assert(getpid()==-1&&errno==EOVERFLOW);
 io_result=-ESRCH;assert(getpid()==-1&&errno==ESRCH);
 io_result=17;assert(read(7,NULL,1024)==17&&io_bytes==256&&io_fd==7);
 assert(write(1,NULL,1000)==17&&io_bytes==256);assert(write(2,NULL,7)==17&&io_bytes==7);
 assert(write(3,NULL,7)==-1&&errno==EBADF);
 io_result=-EBADF;assert(close(8)==-1&&errno==EBADF&&io_fd==8);
 io_result=45;assert(lseek(9,-12,SEEK_END)==45&&io_fd==9&&io_offset==-12&&io_origin==2);
 assert(lseek(9,0,3)==-1&&errno==EINVAL);
 unsigned before_sleep=sleep_calls;assert(!sleep(0)&&sleep_calls==before_sleep);
 assert(sleep(UINT32_MAX)==UINT32_MAX&&errno==EINVAL&&sleep_calls==before_sleep);
 assert(sleep(601)==601&&errno==EINVAL&&sleep_calls==before_sleep);
 io_result=0;assert(!sleep(1)&&sleep_ticks==101&&sleep_calls==before_sleep+1);
 unsigned calls_before_boundary=sleep_calls;assert(!sleep(600)&&sleep_ticks==1&&sleep_calls==calls_before_boundary+2);
 io_result=-EPERM;assert(sleep(3)==3&&errno==EPERM&&sleep_ticks==301);
 io_result=17;assert(sleep(1)==1&&errno==EIO);io_result=INT64_MIN;assert(sleep(1)==1&&errno==EIO);
 io_result=0;fail_sleep_call=sleep_calls+2;assert(sleep(600)==1&&errno==ENODEV&&sleep_ticks==1);return 0;
}
