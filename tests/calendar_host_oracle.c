#define _GNU_SOURCE
#include <time.h>
#include <stdint.h>
#include <errno.h>
/* Host layout and timezone implementation stay in this separately compiled
 * translation unit. The comparison boundary consists only of integer fields. */
static void export_fields(const struct tm *v,int fields[9]){
 fields[0]=v->tm_sec;fields[1]=v->tm_min;fields[2]=v->tm_hour;
 fields[3]=v->tm_mday;fields[4]=v->tm_mon;fields[5]=v->tm_year;
 fields[6]=v->tm_wday;fields[7]=v->tm_yday;fields[8]=v->tm_isdst;
}
int chos_host_calendar_normalize(const int fields[9],int64_t *seconds,int normalized[9]){
 struct tm v={.tm_sec=fields[0],.tm_min=fields[1],.tm_hour=fields[2],.tm_mday=fields[3],
  .tm_mon=fields[4],.tm_year=fields[5],.tm_wday=fields[6],.tm_yday=fields[7],.tm_isdst=fields[8]};
 errno=0;time_t t=timegm(&v);if(t==(time_t)-1&&errno)return errno;
 *seconds=t;export_fields(&v,normalized);return 0;
}
int chos_host_calendar_split(int64_t seconds,int normalized[9]){
 time_t t=seconds;struct tm v;errno=0;
 if(!gmtime_r(&t,&v))return errno;
 export_fields(&v,normalized);return 0;
}
