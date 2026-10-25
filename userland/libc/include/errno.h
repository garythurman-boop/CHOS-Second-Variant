#ifndef CHOS_LIBC_ERRNO_H
#define CHOS_LIBC_ERRNO_H
/* Single-thread runtime storage; this must become thread-local before pthreads. */
#ifdef __cplusplus
extern "C" {
#endif
int *__chos_errno_location(void);
#ifdef __cplusplus
}
#endif
#define errno (*__chos_errno_location())
#define EIO 5
#define ENOMEM 12
#define EINVAL 22
#endif
