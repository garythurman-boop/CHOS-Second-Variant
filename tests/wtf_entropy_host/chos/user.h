#ifndef CHOS_WTF_HOST_BROKER_H
#define CHOS_WTF_HOST_BROKER_H
#include <stddef.h>
#include <stdint.h>
#include "chos/syscall.h"
/* Host transport/error model only; guest builds use the real SVC header. */
extern "C" long chos_random_bytes(void *,size_t);
extern "C" long chos_sleep_ticks(uint64_t);
#endif
