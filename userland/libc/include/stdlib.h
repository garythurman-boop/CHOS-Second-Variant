#ifndef CHOS_LIBC_STDLIB_H
#define CHOS_LIBC_STDLIB_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *malloc(size_t);
void free(void *);
void *calloc(size_t,size_t);
void *realloc(void *,size_t);
void *aligned_alloc(size_t,size_t);
int posix_memalign(void **,size_t,size_t);
void abort(void) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
