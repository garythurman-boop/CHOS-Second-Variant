#include "chos/user.h"
#include <time.h>
#include "mbedtls/platform_util.h"
#include "mbedtls/platform_time.h"
void chos_tls_assert_failed(void){chos_exit(94);}
time_t chos_tls_time(time_t *out){return time(out);}
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *t,struct tm *out){return gmtime_r(t,out);}
mbedtls_ms_time_t mbedtls_ms_time(void){
 struct timespec t;
 /* Public client operations separately reject unavailable clocks. */
 if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0||t.tv_sec>INT64_MAX/1000-1)return 0;
 return t.tv_sec*1000+t.tv_nsec/1000000;
}
