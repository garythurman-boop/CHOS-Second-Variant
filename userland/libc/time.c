#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include "chos/clock_api.h"
#ifdef CHOS_HOST_TEST
extern long chos_libc_clock_query(chos_clock_info_t *);
#else
#include "chos/user.h"
static long chos_libc_clock_query(chos_clock_info_t *i){return chos_clock_status(i);}
#endif
int clock_gettime(clockid_t id,struct timespec *out){
    if(!out || (id!=CLOCK_REALTIME && id!=CLOCK_MONOTONIC)){errno=EINVAL;return -1;}
    chos_clock_info_t i;long rc=chos_libc_clock_query(&i);
    if(rc<0){errno=(int)-rc;return -1;}
    if(i.version!=CHOS_CLOCK_API_VERSION || i.size_bytes!=sizeof(i)){errno=EIO;return -1;}
    uint32_t flag=id==CLOCK_REALTIME?CHOS_CLOCK_REALTIME_VALID:CHOS_CLOCK_MONOTONIC_VALID;
    if(!(i.flags&flag)){errno=EAGAIN;return -1;}
    uint64_t seconds=id==CLOCK_REALTIME?i.realtime_seconds:i.monotonic_seconds;
    uint32_t nanos=id==CLOCK_REALTIME?i.realtime_nanoseconds:i.monotonic_nanoseconds;
    if(seconds>INT64_MAX || nanos>=1000000000u){errno=EOVERFLOW;return -1;}
    *out=(struct timespec){(time_t)seconds,(long)nanos};return 0;
}
time_t time(time_t *out){
    struct timespec t;if(clock_gettime(CLOCK_REALTIME,&t))return (time_t)-1;
    if(out)*out=t.tv_sec;return t.tv_sec;
}
extern int __secs_to_tm(long long,struct tm *);
struct tm *gmtime_r(const time_t *input,struct tm *out){
    if(!input || !out){errno=EINVAL;return 0;}
    struct tm result={0};
    if(__secs_to_tm(*input,&result)){errno=EOVERFLOW;return 0;}
    *out=result;return out;
}

struct tm *localtime_r(const time_t *input,struct tm *out){return gmtime_r(input,out);}
int gettimeofday(struct timeval *out,void *zone){
    if(!out){errno=EINVAL;return -1;}
    if(zone){errno=ENOTSUP;return -1;}
    struct timespec value;if(clock_gettime(CLOCK_REALTIME,&value))return -1;
    *out=(struct timeval){value.tv_sec,value.tv_nsec/1000};return 0;
}
