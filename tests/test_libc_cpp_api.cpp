#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "chos/libc.h"
#include "chos/user.h"
/* Freestanding C++ linkage/header smoke test, not libstdc++ or exception support. */
extern "C" int chos_native_cpp_api_smoke(void){
    long rights=chos_capabilities();if(rights<0 || !(static_cast<uint64_t>(rights)&CHOS_PROCESS_CAP(CHOS_SYS_GETPID)))return 1;
    struct timespec stamp;if(clock_gettime(CLOCK_MONOTONIC,&stamp))return 1;
    char *p=static_cast<char *>(calloc(1,32));
    if(!p)return errno;
    strcpy(p,"CHOS");int result=strcmp(p,"CHOS");
    free(p);chos_heap_stats_t stats;return result||chos_libc_heap_info(&stats);
}
