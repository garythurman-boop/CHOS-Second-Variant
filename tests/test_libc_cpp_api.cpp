#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "chos/libc.h"
/* Freestanding C++ linkage/header smoke test, not libstdc++ or exception support. */
extern "C" int chos_native_cpp_api_smoke(void){
    struct timespec stamp;if(clock_gettime(CLOCK_MONOTONIC,&stamp))return 1;
    char *p=static_cast<char *>(calloc(1,32));
    if(!p)return errno;
    strcpy(p,"CHOS");int result=strcmp(p,"CHOS");
    free(p);chos_heap_stats_t stats;return result||chos_libc_heap_info(&stats);
}
