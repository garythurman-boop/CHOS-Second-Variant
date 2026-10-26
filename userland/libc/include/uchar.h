#ifndef CHOS_LIBC_UCHAR_H
#define CHOS_LIBC_UCHAR_H
#include <stddef.h>
#include <stdint.h>
#ifndef __cplusplus
typedef uint16_t char16_t;
typedef uint32_t char32_t;
#endif
/* UTF-8 partial scalar, minimum scalar, remaining bytes and pending UTF-16 unit. */
typedef struct {uint32_t scalar,minimum,remaining,pending;} mbstate_t;
#ifdef __cplusplus
extern "C" {
#endif
size_t mbrtoc32(char32_t *,const char *,size_t,mbstate_t *);
size_t c32rtomb(char *,char32_t,mbstate_t *);
size_t mbrtoc16(char16_t *,const char *,size_t,mbstate_t *);
size_t c16rtomb(char *,char16_t,mbstate_t *);
int mbsinit(const mbstate_t *);
#ifdef __cplusplus
}
#endif
#endif
