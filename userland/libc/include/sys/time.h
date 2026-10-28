#ifndef CHOS_LIBC_SYS_TIME_H
#define CHOS_LIBC_SYS_TIME_H
#include <time.h>
typedef long suseconds_t;
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
#ifdef __cplusplus
extern "C" {
#endif
/* Real UTC clock query. The obsolete timezone argument must be NULL. */
int gettimeofday(struct timeval *,void *);
#ifdef __cplusplus
}
#endif
#endif
