#ifndef CHOS_LIBC_UNISTD_H
#define CHOS_LIBC_UNISTD_H
#include <stddef.h>
#include <sys/types.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
/* This header exports the implemented subset, not a POSIX-conformance claim. */
#define _SC_PAGESIZE 1
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN 2
#define _SC_NPROCESSORS_CONF 3
#define _SC_CLK_TCK 4
#define _SC_OPEN_MAX 5
#ifdef __cplusplus
extern "C" {
#endif
long sysconf(int);
int getpagesize(void);
pid_t getpid(void);
ssize_t read(int,void *,size_t);
ssize_t write(int,const void *,size_t);
int close(int);
/* At most600 seconds per call;refused waits return the unslept request. */
unsigned sleep(unsigned);
void _exit(int) __attribute__((noreturn));
off_t lseek(int,off_t,int);
#ifdef __cplusplus
}
#endif
#endif
