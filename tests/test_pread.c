#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "chos/syscall.h"
static _Thread_local int model_errno;
int *__chos_errno_location(void){return &model_errno;}
static int calls,seen_fd;static void *seen_buffer;static size_t seen_size;static uint64_t seen_offset;static long reply;
long chos_pread_read_at(int fd,void *buffer,size_t size,uint64_t offset){++calls;seen_fd=fd;seen_buffer=buffer;seen_size=size;seen_offset=offset;return reply;}
int main(void){
 unsigned char b[4]={1,2,3,4};
 for(size_t n=0;n<300;n++){
  reply=n>CHOS_SYS_MAX_READ?CHOS_SYS_MAX_READ:(long)n;errno=EBUSY;calls=0;
  assert(pread(17,b,n,INT64_MAX)==reply&&errno==EBUSY&&calls==1);
  assert(seen_fd==17&&seen_buffer==b&&seen_size==(n>256?256:n)&&seen_offset==INT64_MAX);
 }
 calls=0;errno=0;assert(pread(-1,b,SIZE_MAX,-1)==-1&&errno==EINVAL&&!calls);
 assert(pread(4,b,1,INT64_MIN)==-1&&errno==EINVAL&&!calls);
 for(long error=-1;error>=-128;error--){reply=error;assert(pread(3,b,5,7)==-1&&errno==-error);}
 reply=LONG_MIN;assert(pread(3,b,5,7)==-1&&errno==EIO);
 reply=LONG_MAX;assert(pread(3,b,5,7)==-1&&errno==EIO);
 reply=6;assert(pread(3,b,5,7)==-1&&errno==EIO);
 reply=1;assert(pread(3,b,0,7)==-1&&errno==EIO);
 reply=0;errno=EBUSY;assert(!pread(3,NULL,0,7)&&errno==EBUSY);
 assert(b[0]==1&&b[3]==4);
 puts("pread adapter: signed offsets, bounded partial requests, errno/zero-length and malformed broker results passed");
}
