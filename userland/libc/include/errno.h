#ifndef CHOS_LIBC_ERRNO_H
#define CHOS_LIBC_ERRNO_H
/* Native runtime TCB storage, preserved through TPIDR_EL0 by the kernel. */
#ifdef __cplusplus
extern "C" {
#endif
int *__chos_errno_location(void);
#ifdef __cplusplus
}
#endif
#define errno (*__chos_errno_location())
#define ENOENT 2
#define EFAULT 14
#define EEXIST 17
#define ENOTDIR 20
#define EISDIR 21
#define ENOSPC 28
#define ENOTEMPTY 39
#define EIO 5
#define ENOMEM 12
#define EINVAL 22
#define EAGAIN 11
#define EOVERFLOW 75
#define EPERM 1
#define ESRCH 3
#define EINTR 4
#define EBUSY 16
#define EDEADLK 35
#define ENOTSUP 95
#define ETIMEDOUT 110
#define ERANGE 34
#define EILSEQ 84
#endif
