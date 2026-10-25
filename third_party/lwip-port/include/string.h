#ifndef CHOS_LWIP_STRING_H
#define CHOS_LWIP_STRING_H
#include <stddef.h>
#define strlen chos_lwip_strlen
#define strncmp chos_lwip_strncmp
void *memcpy(void *,const void *,size_t);
void *memmove(void *,const void *,size_t);
void *memset(void *,int,size_t);
int memcmp(const void *,const void *,size_t);
size_t strlen(const char *);
int strncmp(const char *,const char *,size_t);
#endif
