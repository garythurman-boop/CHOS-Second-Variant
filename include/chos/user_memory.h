#ifndef CHOS_USER_MEMORY_H
#define CHOS_USER_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#include "chos/address_space.h"
#define CHOS_USER_MAP_BASE (CHOS_AS_USER_VA_BASE+UINT64_C(0x01000000))
#define CHOS_USER_MAP_BYTES (UINT64_C(2)*1024u*1024u)
#define CHOS_USER_MAP_MAX_BYTES (128u*1024u)
#define CHOS_USER_MAP_REGIONS 16u
/* Anonymous zeroed RW/XN pages only. Nonzero flags are rejected. */
int64_t chos_user_memory_map(address_space_handle_t,size_t,unsigned flags);
int chos_user_memory_unmap(address_space_handle_t,uint64_t,size_t);
/* Process reaper: unmap before address_space_destroy frees all owned frames. */
int chos_user_memory_pin(address_space_handle_t,uint64_t,size_t);
int chos_user_memory_unpin(address_space_handle_t,uint64_t,size_t);
int chos_user_memory_release(address_space_handle_t);
#endif
