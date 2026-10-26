#ifndef CHOS_FUTEX_H
#define CHOS_FUTEX_H
#include "chos/address_space.h"
#include <stdint.h>
/* Private, process-owned 32-bit words. All wait deadlines are bounded ticks. */
int chos_futex_wait(address_space_handle_t,uint64_t word,uint32_t expected,uint64_t ticks);
int chos_futex_wake(address_space_handle_t,uint64_t word,unsigned count);
#endif
