#ifndef CHOS_PRIVATE_CALENDAR_IMPL_H
#define CHOS_PRIVATE_CALENDAR_IMPL_H
/* Original pure calendar cores use the native public tm layout. No TZ/DST,
 * locale, environment or host libc machinery is imported through this header. */
#include <time.h>
long long __year_to_secs(long long,int *);
int __month_to_secs(int,int);
long long __tm_to_secs(const struct tm *);
int __secs_to_tm(long long,struct tm *);
#endif
