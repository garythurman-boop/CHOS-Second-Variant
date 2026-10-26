#include <assert.h>
#include <time.h>
#include <errno.h>
#include "chos/clock_api.h"
static int error;
int *__chos_errno_location(void){return &error;}
static chos_clock_info_t mock;
static long rc;
long chos_libc_clock_query(chos_clock_info_t *out){*out=mock;return rc;}
int main(void){
 struct timespec t={123,456};
 mock=(chos_clock_info_t){.version=1,.size_bytes=48,.flags=3,.realtime_seconds=951782400,.realtime_nanoseconds=123456789,.monotonic_seconds=8};
 assert(!clock_gettime(CLOCK_REALTIME,&t)&&t.tv_sec==951782400&&t.tv_nsec==123456789);
 assert(!clock_gettime(CLOCK_MONOTONIC,&t)&&t.tv_sec==8&&!t.tv_nsec);
 time_t epoch;assert(time(&epoch)==951782400&&epoch==951782400);
 struct tm c;assert(gmtime_r(&epoch,&c)==&c&&c.tm_year==100&&c.tm_mon==1&&c.tm_mday==29&&c.tm_yday==59&&c.tm_wday==2);
 epoch=4107542400;assert(gmtime_r(&epoch,&c)&&c.tm_year==200&&c.tm_mon==2&&c.tm_mday==1&&c.tm_yday==59);
 epoch=253402300799;assert(gmtime_r(&epoch,&c)&&c.tm_year==8099&&c.tm_mon==11&&c.tm_mday==31&&c.tm_sec==59);
 epoch=-1;assert(!gmtime_r(&epoch,&c)&&errno==EOVERFLOW);
 assert(!gmtime_r(0,&c)&&errno==EINVAL);
 assert(clock_gettime(99,&t)==-1&&errno==EINVAL);
 mock.flags=1;t=(struct timespec){123,456};assert(clock_gettime(0,&t)==-1&&errno==EAGAIN&&t.tv_sec==123&&t.tv_nsec==456);
 mock.flags=3;mock.realtime_nanoseconds=1000000000;assert(clock_gettime(0,&t)==-1&&errno==EOVERFLOW);
 mock.version=99;assert(clock_gettime(0,&t)==-1&&errno==EIO);
 rc=-38;assert(clock_gettime(0,&t)==-1&&errno==38);
 return 0;
}
