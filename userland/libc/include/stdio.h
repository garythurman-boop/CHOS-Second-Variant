#ifndef CHOS_LIBC_STDIO_H
#define CHOS_LIBC_STDIO_H
#if defined(CHOS_HOST_TEST) && !defined(CHOS_FILENAME_HOST_TEST)
#include_next <stdio.h>
#else
/* Opaque FILE permits standard wide prototypes; there are no FILE objects or
 * stream/formatting implementations in this bounded profile. */
typedef struct __chos_FILE FILE;
#define EOF (-1)
#ifdef __cplusplus
extern "C" {
#endif
int remove(const char *);
/* CHFS refuses replacement of an existing destination. */
int rename(const char *,const char *);
#ifdef __cplusplus
}
#endif
#endif
#endif
