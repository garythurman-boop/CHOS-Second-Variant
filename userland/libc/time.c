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
static int leap(int y){return y%4==0 && (y%100!=0 || y%400==0);}
struct tm *gmtime_r(const time_t *input,struct tm *out){
    if(!input || !out){errno=EINVAL;return 0;}
    if(*input<0 || *input>INT64_C(253402300799)){errno=EOVERFLOW;return 0;}
    uint64_t days=(uint64_t)*input/86400u,remain=days,part=(uint64_t)*input%86400u;
    int y=1970;
    while(remain>=(unsigned)(365+leap(y))){remain-=(unsigned)(365+leap(y));++y;}
    int day_of_year=(int)remain,m=0;
    static const unsigned char lengths[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    while(m<11){unsigned n=lengths[m]+(m==1 && leap(y));if(remain<n)break;remain-=n;++m;}
    *out=(struct tm){.tm_sec=(int)(part%60),.tm_min=(int)((part/60)%60),.tm_hour=(int)(part/3600),
        .tm_mday=(int)remain+1,.tm_mon=m,.tm_year=y-1900,.tm_wday=(int)((days+4)%7),.tm_yday=day_of_year,.tm_isdst=0};
    return out;
}

struct tm *localtime_r(const time_t *input,struct tm *out){return gmtime_r(input,out);}
int gettimeofday(struct timeval *out,void *zone){
    if(!out){errno=EINVAL;return -1;}
    if(zone){errno=ENOTSUP;return -1;}
    struct timespec value;if(clock_gettime(CLOCK_REALTIME,&value))return -1;
    *out=(struct timeval){value.tv_sec,value.tv_nsec/1000};return 0;
}
