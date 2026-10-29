#ifndef CHOS_RESOURCE_API_H
#define CHOS_RESOURCE_API_H
#include <stdint.h>
#define CHOS_RESOURCE_VERSION 1u
#define CHOS_RESOURCE_MEMORY_VALID 1u
#define CHOS_RESOURCE_CPU_VALID 2u
/* Calling process/current thread only. CPU ticks are architectural counter
 * intervals while dispatched; blocked/idle time is excluded. Process totals
 * retain exited threads. Kernel-root service tasks are not charged to a client.
 * Resident bytes charge owned user frames, including inaccessible reservations;
 * mapped bytes count user mappings. Page tables/kernel buffers are excluded. */
typedef struct {
 uint32_t version,size_bytes,flags,reserved0;
 uint64_t counter_frequency;
 uint64_t process_user_ticks,process_system_ticks;
 uint64_t thread_user_ticks,thread_system_ticks;
 uint64_t mapped_bytes,resident_bytes;
 uint64_t reserved[3];
} chos_resource_info_t;
_Static_assert(sizeof(chos_resource_info_t)==96u,"resource ABI size");
#endif
