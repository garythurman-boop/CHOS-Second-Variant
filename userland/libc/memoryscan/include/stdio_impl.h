#ifndef CHOS_PRIVATE_MEMORY_SCAN_IMPL_H
#define CHOS_PRIVATE_MEMORY_SCAN_IMPL_H
/* Same real bounded byte-view used by the native floating parser. This is
 * private to one scanner invocation, never a CHOS public FILE object. */
#include "../../floatscan/include/stdio_impl.h"
#include <stdarg.h>
#include <errno.h>
#define EOF (-1)
#define vfscanf __chos_memory_vfscanf
#define __intscan __chos_memory_intscan
#define weak_alias(original,alias)
#define FLOCK(f) ((void)(f))
#define FUNLOCK(f) ((void)(f))
hidden int vfscanf(FILE *,const char *,va_list);
hidden unsigned long long __intscan(FILE *,unsigned,int,unsigned long long);
/* A valid view is fully initialized. Missing input state cannot turn into a
 * successful I/O refill: this adapter does not own a filesystem descriptor. */
static inline int __toread(FILE *f){if(f)f->rpos=NULL;errno=EBADF;return EOF;}
int sscanf(const char *,const char *,...);
int vsscanf(const char *,const char *,va_list);
#endif
