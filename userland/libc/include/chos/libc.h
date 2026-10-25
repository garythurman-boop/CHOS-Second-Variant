#ifndef CHOS_LIBC_H
#define CHOS_LIBC_H
#include "chos/allocator.h"
/* Diagnostics only; no device or kernel administration capability. */
#ifdef __cplusplus
extern "C" {
#endif
int chos_libc_heap_info(chos_heap_stats_t *);
#ifdef __cplusplus
}
#endif
#endif
