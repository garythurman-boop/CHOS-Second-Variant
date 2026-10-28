#ifndef CHOS_BMALLOC_HOST_TRANSPORT_H
#define CHOS_BMALLOC_HOST_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>
#include "chos/syscall.h"
/* Host-only model for memory/bound/error tests; real services run separately. */
extern "C" long chos_write(const void *,size_t);
extern "C" long chos_random_bytes(void *,size_t);
extern "C" long chos_sleep_ticks(uint64_t);
extern "C" long chos_yield(void);
#endif
