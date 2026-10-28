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
/* POSIX errc vocabulary; constants do not imply the corresponding API exists. */
#define E2BIG 7
#define EACCES 13
#define EADDRINUSE 98
#define EADDRNOTAVAIL 99
#define EAFNOSUPPORT 97
#define EALREADY 114
#define EBADF 9
#define EBADMSG 74
#define ECANCELED 125
#define ECHILD 10
#define ECONNABORTED 103
#define ECONNREFUSED 111
#define ECONNRESET 104
#define EDESTADDRREQ 89
#define EDOM 33
#define EFBIG 27
#define EHOSTUNREACH 113
#define EIDRM 43
#define EINPROGRESS 115
#define EISCONN 106
#define ELOOP 40
#define EMFILE 24
#define EMLINK 31
#define EMSGSIZE 90
#define ENAMETOOLONG 36
#define ENETDOWN 100
#define ENETRESET 102
#define ENETUNREACH 101
#define ENFILE 23
#define ENOBUFS 105
#define ENODEV 19
#define ENOEXEC 8
#define ENOLCK 37
#define ENOLINK 67
#define ENOMSG 42
#define ENOPROTOOPT 92
#define ENOSYS 38
#define ENOTCONN 107
#define ENOTRECOVERABLE 131
#define ENOTSOCK 88
#define ENOTTY 25
#define ENXIO 6
#define EOPNOTSUPP 95
#define EOWNERDEAD 130
#define EPIPE 32
#define EPROTO 71
#define EPROTONOSUPPORT 93
#define EPROTOTYPE 91
#define EROFS 30
#define ESPIPE 29
#define ETXTBSY 26
#define EWOULDBLOCK 11
#define EXDEV 18
/* Additional standard error-message vocabulary; constants do not add services. */
#define EBADFD 77
#define EDQUOT 122
#define EHOSTDOWN 112
#define EKEYEXPIRED 127
#define EKEYREJECTED 129
#define EKEYREVOKED 128
#define EMEDIUMTYPE 124
#define EMULTIHOP 72
#define ENODATA 61
#define ENOKEY 126
#define ENOMEDIUM 123
#define ENOSR 63
#define ENOSTR 60
#define ENOTBLK 15
#define EPFNOSUPPORT 96
#define EREMOTEIO 121
#define ESHUTDOWN 108
#define ESOCKTNOSUPPORT 94
#define ESTALE 116
#define ETIME 62
#endif
