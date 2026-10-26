#ifndef CHOS_CRYPTO_STRING_H
#define CHOS_CRYPTO_STRING_H
#include <stddef.h>
void *memcpy(void *,const void *,size_t);
void *memset(void *,int,size_t);
int memcmp(const void *,const void *,size_t);
size_t strlen(const char *);
int strcmp(const char *,const char *);
#endif
#define strcmp chos_crypto_strcmp
int chos_crypto_strcmp(const char *,const char *);
void explicit_bzero(void *,size_t);
