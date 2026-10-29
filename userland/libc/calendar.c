/* Native UTC calendar policy around the maintained conversion cores. */
#include <time.h>
#include <errno.h>
#include <stdint.h>
extern long long __tm_to_secs(const struct tm *);
extern int __secs_to_tm(long long,struct tm *);
static time_t normalized_utc(struct tm *input){
    if(!input){errno=EINVAL;return (time_t)-1;}
    struct tm first=*input;
    /* The original core computes tm_mday-1 as int. Separate that term in
     * 64 bits so an INT_MIN field never invokes signed int overflow. */
    first.tm_mday=1;
    int64_t seconds=__tm_to_secs(&first)+INT64_C(86400)*((int64_t)input->tm_mday-1);
    struct tm result={0};
    if(__secs_to_tm(seconds,&result)){errno=EOVERFLOW;return (time_t)-1;}
    *input=result;return (time_t)seconds;
}
time_t timegm(struct tm *input){return normalized_utc(input);}
time_t mktime(struct tm *input){
    if(input&&input->tm_isdst>0){errno=ENOTSUP;return (time_t)-1;}
    return normalized_utc(input);
}
