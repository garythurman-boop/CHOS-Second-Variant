#ifndef CHOS_MBEDTLS_CONFIG_H
#define CHOS_MBEDTLS_CONFIG_H
#include <stddef.h>
void *chos_crypto_calloc(size_t,size_t);
void chos_crypto_free(void *);
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_CALLOC_MACRO chos_crypto_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO chos_crypto_free
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_HMAC_DRBG_C
#define MBEDTLS_SELF_TEST
int chos_crypto_printf(const char *,...);
#define MBEDTLS_PLATFORM_PRINTF_MACRO chos_crypto_printf
#endif
