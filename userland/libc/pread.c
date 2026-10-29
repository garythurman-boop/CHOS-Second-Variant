#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include "chos/syscall.h"
#ifdef CHOS_PREAD_HOST_TEST
extern long chos_pread_read_at(int,void *,size_t,uint64_t);
#else
#include "chos/user.h"
#define chos_pread_read_at chos_read_at
#endif
ssize_t pread(int fd,void *buffer,size_t bytes,off_t offset){
 if(offset<0){errno=EINVAL;return -1;}
 size_t amount=bytes>CHOS_SYS_MAX_READ?CHOS_SYS_MAX_READ:bytes;
 long result=chos_pread_read_at(fd,buffer,amount,(uint64_t)offset);
 if(result<0){errno=result>=-INT_MAX?(int)-result:EIO;return -1;}
 if((unsigned long)result>amount){errno=EIO;return -1;}
 return result;
}
