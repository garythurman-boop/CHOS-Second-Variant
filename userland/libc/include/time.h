#ifndef CHOS_LIBC_TIME_H
#define CHOS_LIBC_TIME_H
#include <stdint.h>
typedef int64_t time_t;
typedef int clockid_t;
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
struct timespec { time_t tv_sec; long tv_nsec; };
struct tm { int tm_sec,tm_min,tm_hour,tm_mday,tm_mon,tm_year,tm_wday,tm_yday,tm_isdst; };
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(clockid_t,struct timespec *);
time_t time(time_t *);
/* Runtime local time is UTC. Explicit positive DST hints are unsupported. */
time_t mktime(struct tm *);
time_t timegm(struct tm *);
struct tm *gmtime_r(const time_t *,struct tm *);
struct tm *localtime_r(const time_t *,struct tm *);
extern char *tzname[2];
#ifdef __cplusplus
}
#endif
#endif
