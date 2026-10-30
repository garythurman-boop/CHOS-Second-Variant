#ifndef CHOS_LIBC_SYS_MMAN_H
#define CHOS_LIBC_SYS_MMAN_H
#include <stddef.h>
#include <sys/types.h>
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FAILED ((void *)-1)
#define MADV_NORMAL 0
#define MADV_RANDOM 1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4
#ifdef __cplusplus
extern "C" {
#endif
/* Bounded private anonymous pages only, eagerly backed. Nonfixed address hints
 * may be ignored. Fixed/file/shared/executable requests fail explicitly. */
void *mmap(void *,size_t,int,int,int,off_t);
int munmap(void *,size_t);
/* Anonymous protections remain reversible. Image data supports only one-way
 * READ/NONE reductions; no writable/executable restoration or fixed remap. */
int mprotect(void *,size_t,int);
int madvise(void *,size_t,int);
#ifdef __cplusplus
}
#endif
#endif
