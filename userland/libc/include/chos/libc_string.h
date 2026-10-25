#ifndef CHOS_LIBC_STRING_CORE_H
#define CHOS_LIBC_STRING_CORE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *chos_libc_memcpy(void *,const void *,size_t);
void *chos_libc_memmove(void *,const void *,size_t);
void *chos_libc_memset(void *,int,size_t);
int chos_libc_memcmp(const void *,const void *,size_t);
void *chos_libc_memchr(const void *,int,size_t);
size_t chos_libc_strlen(const char *);
size_t chos_libc_strnlen(const char *,size_t);
int chos_libc_strcmp(const char *,const char *);
int chos_libc_strncmp(const char *,const char *,size_t);
char *chos_libc_strcpy(char *,const char *);
char *chos_libc_strncpy(char *,const char *,size_t);
char *chos_libc_strcat(char *,const char *);
char *chos_libc_strncat(char *,const char *,size_t);
char *chos_libc_strchr(const char *,int);
char *chos_libc_strrchr(const char *,int);
char *chos_libc_strstr(const char *,const char *);
#ifdef __cplusplus
}
#endif
#endif
