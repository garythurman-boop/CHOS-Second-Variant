/* Real bounded anonymous mapping adapter; no success stubs for unsupported modes. */
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include "chos/user_memory_limits.h"
#include "chos/memory_api.h"
#ifdef CHOS_MMAN_HOST_TEST
extern long chos_mman_map(size_t);
extern long chos_mman_unmap(void *,size_t);
extern long chos_mman_control(void *,size_t,unsigned);
#else
#include "chos/user.h"
#define chos_mman_map chos_map_memory
#define chos_mman_unmap chos_unmap_range
#define chos_mman_control chos_memory_control
#endif
static int result(long r){if(r<0){errno=(int)-r;return -1;}return 0;}
static int protection(int prot){
 if(prot&PROT_EXEC){errno=EACCES;return -1;}
 if(prot!=PROT_NONE&&prot!=PROT_READ&&prot!=(PROT_READ|PROT_WRITE)){errno=EINVAL;return -1;}
 return 0;
}
void *mmap(void *hint,size_t bytes,int prot,int flags,int fd,off_t offset){
 (void)hint;
 if(protection(prot))return MAP_FAILED;
 if(!bytes){errno=EINVAL;return MAP_FAILED;}
 if(bytes>CHOS_USER_MAP_MAX_BYTES){errno=ENOMEM;return MAP_FAILED;}
 if(flags&(MAP_SHARED|MAP_FIXED|MAP_FIXED_NOREPLACE) || !(flags&MAP_ANONYMOUS)){
  errno=ENOTSUP;return MAP_FAILED;
 }
 if(flags!=(MAP_PRIVATE|MAP_ANONYMOUS) && flags!=(MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE)){
  errno=EINVAL;return MAP_FAILED;
 }
 if(fd!=-1||offset){errno=EINVAL;return MAP_FAILED;}
 long base=chos_mman_map(bytes);
 if(base<0){errno=(int)-base;return MAP_FAILED;}
 void *p=(void *)(uintptr_t)base;
 if(prot!=(PROT_READ|PROT_WRITE)){
  long r=chos_mman_control(p,bytes,(unsigned)prot);
  if(r<0){int error=(int)-r;(void)chos_mman_unmap(p,bytes);errno=error;return MAP_FAILED;}
 }
 return p;
}
int munmap(void *p,size_t bytes){return result(chos_mman_unmap(p,bytes));}
int mprotect(void *p,size_t bytes,int prot){
 if(protection(prot))return -1;
 return result(chos_mman_control(p,bytes,(unsigned)prot));
}
int madvise(void *p,size_t bytes,int advice){
 unsigned operation;
 switch(advice){
 case MADV_DONTNEED:operation=CHOS_MEMORY_DISCARD;break;
 case MADV_NORMAL:case MADV_RANDOM:case MADV_SEQUENTIAL:case MADV_WILLNEED:
  /* Advisory access patterns do not alter this eager, nonswapping profile. */
  operation=CHOS_MEMORY_VALIDATE;break;
 default:errno=EINVAL;return -1;
 }
 return result(chos_mman_control(p,bytes,operation));
}
