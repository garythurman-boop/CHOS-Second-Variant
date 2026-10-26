#ifndef CHOS_CLOCK_API_H
#define CHOS_CLOCK_API_H
#include <stdint.h>
#define CHOS_CLOCK_API_VERSION 1u
#define CHOS_CLOCK_MONOTONIC_VALID 1u
#define CHOS_CLOCK_REALTIME_VALID 2u
/* Availability is not authenticated wall-clock trust. No clock setters. */
typedef struct {
 uint32_t version,size_bytes,flags,source;
 uint64_t monotonic_seconds,realtime_seconds;
 uint32_t monotonic_nanoseconds,realtime_nanoseconds;
 uint64_t reserved;
} chos_clock_info_t;
_Static_assert(sizeof(chos_clock_info_t)==48u,"clock ABI");
#endif
