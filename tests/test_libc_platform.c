#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>
#include <errno.h>
#include "chos/clock_api.h"
static int error;
int *__chos_errno_location(void){return &error;}
static chos_clock_info_t clock_info;
static long clock_result;
long chos_libc_clock_query(chos_clock_info_t *out){*out=clock_info;return clock_result;}
static void environments(void){
 assert(environ && !environ[0]&&!getenv("PATH"));
 char a[]="A=one",b[]="AB=two",empty[]="EMPTY=",bad[]="MALFORMED",duplicate[]="A=later";
 char *values[]={bad,a,b,empty,duplicate,NULL};environ=values;
 assert(getenv("A")==a+2&&getenv("AB")==b+3&&getenv("EMPTY")==empty+6);
 assert(!getenv(NULL)&&!getenv("")&&!getenv("A=")&&!getenv("MALFORMED")&&!getenv("ABC"));
 assert(!getenv("an-extremely-long-name-that-is-longer-than-every-entry-in-the-table"));
 char tiny[]="Q";char *malformed[]={tiny,NULL};environ=malformed;assert(!getenv("Q")&&!getenv("QR"));
 environ=NULL;assert(!getenv("A"));
}
static void locales(void){
 char *empty[]={NULL};environ=empty;
 for(int c=LC_CTYPE;c<=LC_ALL;c++){
  assert(!strcmp(setlocale(c,NULL),"C"));assert(!strcmp(setlocale(c,"C"),"C"));
  assert(!strcmp(setlocale(c,"POSIX"),"C"));assert(!strcmp(setlocale(c,""),"C"));
  assert(!setlocale(c,"de_DE")&&errno==ENOENT);assert(!strcmp(setlocale(c,NULL),"C"));
 }
 assert(!setlocale(-1,"C")&&errno==EINVAL);assert(!setlocale(LC_ALL+1,NULL)&&errno==EINVAL);
 char *lang[]={"LANG=fr_FR",NULL};environ=lang;assert(!setlocale(LC_ALL,"")&&errno==ENOENT);
 assert(!strcmp(setlocale(LC_ALL,"C"),"C")); // Explicit C ignores environment.
 char *priority[]={"LANG=fr_FR","LC_ALL=POSIX","LC_CTYPE=de_DE",NULL};environ=priority;
 assert(!strcmp(setlocale(LC_ALL,""),"C"));
 char *category[]={"LANG=POSIX","LC_ALL=","LC_NUMERIC=de_DE",NULL};environ=category;
 assert(!setlocale(LC_ALL,"")&&errno==ENOENT);assert(!setlocale(LC_NUMERIC,"")&&errno==ENOENT);
 assert(!strcmp(setlocale(LC_CTYPE,""),"C"));
 assert(!strcmp(setlocale(LC_ALL,NULL),"C"));environ=empty;
 struct lconv *v=localeconv();assert(!strcmp(v->decimal_point,".")&&!v->thousands_sep[0]&&!v->grouping[0]);
 assert(!v->int_curr_symbol[0]&&!v->currency_symbol[0]&&!v->positive_sign[0]&&!v->negative_sign[0]);
 assert(v->frac_digits==CHAR_MAX&&v->int_frac_digits==CHAR_MAX&&v->p_cs_precedes==CHAR_MAX&&v->n_cs_precedes==CHAR_MAX&&v->int_n_sign_posn==CHAR_MAX);
 assert(!strcmp(tzname[0],"UTC")&&!strcmp(tzname[1],"UTC"));
}
static void clocks(void){
 clock_info=(chos_clock_info_t){.version=1,.size_bytes=sizeof(clock_info),.flags=3,.realtime_seconds=951782400,.realtime_nanoseconds=123456789};
 struct timeval v={7,8};assert(!gettimeofday(&v,NULL)&&v.tv_sec==951782400&&v.tv_usec==123456);
 struct tm out,utc;time_t t=v.tv_sec;
 assert(localtime_r(&t,&out)==&out&&gmtime_r(&t,&utc)==&utc&&!memcmp(&out,&utc,sizeof(out)));
 assert(out.tm_year==100&&out.tm_mon==1&&out.tm_mday==29&&!out.tm_isdst);
 v=(struct timeval){7,8};assert(gettimeofday(&v,&out)==-1&&errno==ENOTSUP&&v.tv_sec==7&&v.tv_usec==8);
 assert(gettimeofday(NULL,NULL)==-1&&errno==EINVAL);
 clock_info.flags=1;assert(gettimeofday(&v,NULL)==-1&&errno==EAGAIN&&v.tv_sec==7&&v.tv_usec==8);
 clock_info.flags=3;clock_info.realtime_nanoseconds=1000000000;assert(gettimeofday(&v,NULL)==-1&&errno==EOVERFLOW&&v.tv_sec==7&&v.tv_usec==8);
 clock_info.realtime_nanoseconds=999999999;assert(!gettimeofday(&v,NULL)&&v.tv_usec==999999);
 clock_result=-ENOSYS;v=(struct timeval){7,8};assert(gettimeofday(&v,NULL)==-1&&errno==ENOSYS&&v.tv_sec==7&&v.tv_usec==8);
 t=-1;assert(localtime_r(&t,&out)==&out&&out.tm_year==69&&out.tm_mon==11&&out.tm_mday==31&&out.tm_sec==59);
 t=INT64_MAX;assert(!localtime_r(&t,&out)&&errno==EOVERFLOW);
}
int main(void){environments();locales();clocks();return 0;}
