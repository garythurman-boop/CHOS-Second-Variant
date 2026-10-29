#include <time.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "chos/clock_api.h"
extern int host_printf(const char *,...) __asm__("printf");
extern int chos_host_calendar_normalize(const int[9],int64_t *,int[9]);
extern int chos_host_calendar_split(int64_t,int[9]);
extern int chos_host_readers(int (*)(void *),void *);
static _Thread_local int thread_errno;
int *__chos_errno_location(void){return &thread_errno;}
long chos_libc_clock_query(chos_clock_info_t *out){(void)out;return -38;}
#define CHECK(v) do{if(!(v)){host_printf("calendar check failed line %u: %s\n",__LINE__,#v);abort();}}while(0)
static uint64_t next(uint64_t *s){*s^=*s<<13;*s^=*s>>7;*s^=*s<<17;return *s;}
static void export_fields(const struct tm *v,int f[9]){
 f[0]=v->tm_sec;f[1]=v->tm_min;f[2]=v->tm_hour;f[3]=v->tm_mday;f[4]=v->tm_mon;f[5]=v->tm_year;f[6]=v->tm_wday;f[7]=v->tm_yday;f[8]=v->tm_isdst;
}
static void normalize(struct tm input){
 int fields[9],expected[9],observed[9];int64_t seconds=0;export_fields(&input,fields);
 int error=chos_host_calendar_normalize(fields,&seconds,expected);
 struct tm result=input;errno=0;time_t value=timegm(&result);
 if(error){CHECK(error==EOVERFLOW&&value==-1&&errno==EOVERFLOW&&!memcmp(&result,&input,sizeof input));}
 else{export_fields(&result,observed);CHECK(value==seconds&&!errno&&!memcmp(expected,observed,sizeof expected));}
 result=input;errno=0;value=mktime(&result);
 if(input.tm_isdst>0)CHECK(value==-1&&errno==ENOTSUP&&!memcmp(&result,&input,sizeof input));
 else if(error)CHECK(value==-1&&errno==EOVERFLOW&&!memcmp(&result,&input,sizeof input));
 else{export_fields(&result,observed);CHECK(value==seconds&&!errno&&!memcmp(expected,observed,sizeof expected));}
}
static void split(int64_t seconds){
 int expected[9],observed[9];int error=chos_host_calendar_split(seconds,expected);
 struct tm result;memset(&result,0x5a,sizeof result);struct tm before=result;
 errno=0;struct tm *p=gmtime_r(&seconds,&result);
 if(error)CHECK(error==EOVERFLOW&&!p&&errno==EOVERFLOW&&!memcmp(&result,&before,sizeof result));
 else{CHECK(p==&result&&!errno);export_fields(&result,observed);CHECK(!memcmp(expected,observed,sizeof expected));
  errno=0;CHECK(timegm(&result)==seconds&&!errno);}
}
static void fuzz(uint64_t seed){
 for(unsigned i=0;i<50000;i++){
  struct tm v={.tm_sec=(int)next(&seed),.tm_min=(int)next(&seed),.tm_hour=(int)next(&seed),.tm_mday=(int)next(&seed),
   .tm_mon=(int)next(&seed),.tm_year=(int)next(&seed),.tm_wday=(int)next(&seed),.tm_yday=(int)next(&seed),.tm_isdst=(int)(i%3)-1};
  normalize(v);int64_t seconds=(int64_t)next(&seed);if(i%2)seconds%=INT64_C(60000000000000000);split(seconds);
 }
}
static void edges(void){
 const int values[]={INT_MIN,INT_MIN+1,-100000,-400,-1,0,1,28,29,30,59,60,69,70,100,200,8099,INT_MAX-1,INT_MAX};
 for(unsigned field=0;field<6;field++)for(unsigned i=0;i<sizeof values/sizeof *values;i++){
  struct tm v={.tm_mday=1,.tm_year=100};
  switch(field){case 0:v.tm_sec=values[i];break;case 1:v.tm_min=values[i];break;case 2:v.tm_hour=values[i];break;
   case 3:v.tm_mday=values[i];break;case 4:v.tm_mon=values[i];break;default:v.tm_year=values[i];break;}
  normalize(v);
 }
 for(int year=1600;year<=2400;year+=100)normalize((struct tm){.tm_year=year-1900,.tm_mon=1,.tm_mday=29});
 const int64_t seconds[]={INT64_MIN,INT64_MAX,-2208988801LL,-2208988800LL,-1,0,1,951782400,4107542400LL,253402300799LL,253402300800LL};
 for(unsigned i=0;i<sizeof seconds/sizeof *seconds;i++)split(seconds[i]);
 CHECK(timegm(NULL)==-1&&errno==EINVAL);CHECK(mktime(NULL)==-1&&errno==EINVAL);
 struct tm v;memset(&v,0x5a,sizeof v);struct tm old=v;
 CHECK(!gmtime_r(NULL,&v)&&errno==EINVAL&&!memcmp(&v,&old,sizeof v));
 time_t t=0;CHECK(!gmtime_r(&t,NULL)&&errno==EINVAL);
}
static atomic_uint sequence;
static int worker(void *p){(void)p;unsigned n=atomic_fetch_add(&sequence,1);fuzz(UINT64_C(0x3917264)+n);return 0;}
int main(void){edges();fuzz(UINT64_C(0x1762954));CHECK(!chos_host_readers(worker,NULL));
 host_printf("UTC calendar:200000 normalization and200000 split comparisons against glibc,extreme fields,centuries and concurrent error isolation passed\n");return 0;}
