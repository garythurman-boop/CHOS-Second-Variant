#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include "chos/file_api.h"
#ifdef CHOS_UNISTD_HOST_TEST
extern long chos_posix_getpid(void);
extern long chos_posix_read(int,void *,size_t);
extern long chos_posix_write(const void *,size_t);
extern long chos_posix_close(int);
extern long chos_posix_seek(int,int64_t,unsigned);
#else
#include "chos/user.h"
#define chos_posix_getpid chos_getpid
#define chos_posix_read chos_read
#define chos_posix_write chos_write
#define chos_posix_close chos_close
#define chos_posix_seek chos_seek
#endif
long sysconf(int name){
 switch(name){
 case _SC_PAGESIZE:return 4096;
 case _SC_NPROCESSORS_ONLN:case _SC_NPROCESSORS_CONF:return 1;
 case _SC_CLK_TCK:return 100;
 case _SC_OPEN_MAX:return CHOS_FILE_OPEN_MAX;
 default:errno=EINVAL;return -1;
 }
}
int getpagesize(void){return 4096;}
static long checked(long n){if(n<0){errno=(int)-n;return -1;}return n;}
pid_t getpid(void){long n=checked(chos_posix_getpid());if(n>INT_MAX){errno=EOVERFLOW;return -1;}return (pid_t)n;}
ssize_t read(int fd,void *p,size_t n){return checked(chos_posix_read(fd,p,n>256?256:n));}
ssize_t write(int fd,const void *p,size_t n){
 if(fd!=STDOUT_FILENO&&fd!=STDERR_FILENO){errno=EBADF;return -1;}
 return checked(chos_posix_write(p,n>256?256:n));
}
int close(int fd){return (int)checked(chos_posix_close(fd));}
off_t lseek(int fd,off_t offset,int origin){
 if(origin<SEEK_SET||origin>SEEK_END){errno=EINVAL;return -1;}
 return checked(chos_posix_seek(fd,offset,(unsigned)origin));
}
