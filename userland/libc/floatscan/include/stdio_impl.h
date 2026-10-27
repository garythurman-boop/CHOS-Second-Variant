#ifndef CHOS_PRIVATE_FLOAT_SCAN_IMPL_H
#define CHOS_PRIVATE_FLOAT_SCAN_IMPL_H
#include <stddef.h>
#include <stdint.h>
#define hidden __attribute__((visibility("hidden")))
typedef ptrdiff_t off_t;
_Static_assert(sizeof(off_t)==8,"native LP64 scan offsets");
typedef struct __chos_float_scan_stream {
 unsigned char *buf,*rpos,*rend,*shend;
 off_t shlim,shcnt;
} FILE;
#endif
