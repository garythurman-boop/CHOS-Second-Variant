#ifndef CHOS_LIBC_STRINGS_H
#define CHOS_LIBC_STRINGS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native invariant C locale: ASCII case folding; other bytes compare unsigned. */
int strcasecmp(const char *,const char *);
int strncasecmp(const char *,const char *,size_t);
#ifdef __cplusplus
}
#endif
#endif
