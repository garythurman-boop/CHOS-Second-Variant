#include "chos/seed_rng.h"
#include "chos/spinlock.h"
#include "mbedtls/hmac_drbg.h"
#include "mbedtls/platform_util.h"
#ifndef CHOS_HOST_TEST
#include "chos/kheap.h"
#endif
static mbedtls_hmac_drbg_context context;
static chos_spinlock_t lock=CHOS_SPINLOCK_INITIALIZER;
static unsigned attempted,ready,calls;
#ifndef CHOS_HOST_TEST
void *chos_crypto_calloc(size_t n,size_t size){
    if(!n||!size||n>SIZE_MAX/size)return NULL;
    uint8_t *p=kheap_alloc(n*size);if(p)for(size_t i=0;i<n*size;i++)p[i]=0;return p;
}
void chos_crypto_free(void *p){if(p)(void)kheap_free(p);}
#endif
int chos_crypto_printf(const char *format,...){(void)format;return 0;}
typedef struct {uint8_t *bytes;size_t used;} seed_source_t;
static int entropy(void *arg,unsigned char *out,size_t bytes){
    seed_source_t *s=arg;if(bytes>CHOS_SEED_RNG_BYTES-s->used)return -1;
    for(size_t i=0;i<bytes;i++)out[i]=s->bytes[s->used++];return 0;
}
int chos_seed_rng_init(uint8_t *seed,size_t bytes){
    if(!seed)return 0;
    if(bytes!=CHOS_SEED_RNG_BYTES){/* Only a known-size writable handoff is accepted. */return 0;}
    uint64_t flags=chos_spin_lock_irqsave(&lock);
    if(attempted){mbedtls_platform_zeroize(seed,bytes);chos_spin_unlock_irqrestore(&lock,flags);return 0;}
    attempted=1;unsigned any=0,difference=0;
    for(unsigned i=0;i<64;i++)any|=seed[i];
    for(unsigned i=0;i<32;i++)difference|=seed[i]^seed[i+32];
    if(any&&difference){
        seed_source_t s={seed,0};mbedtls_hmac_drbg_init(&context);
        static const unsigned char personal[]="CHOS firmware RNG HMAC-DRBG v1";
        ready=mbedtls_hmac_drbg_seed(&context,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),entropy,&s,personal,sizeof(personal)-1)==0;
        /* The boot callback cannot outlive its seed. A finite output budget
         * enforces reseed failure rather than reusing an erased stack pointer. */
        context.MBEDTLS_PRIVATE(f_entropy)=NULL;context.MBEDTLS_PRIVATE(p_entropy)=NULL;
        if(!ready)mbedtls_hmac_drbg_free(&context);
    }
    mbedtls_platform_zeroize(seed,bytes);chos_spin_unlock_irqrestore(&lock,flags);return ready;
}
int chos_seed_rng_ready(void){uint64_t f=chos_spin_lock_irqsave(&lock);int r=ready;chos_spin_unlock_irqrestore(&lock,f);return r;}
size_t chos_seed_rng_read(void *out,size_t bytes){
    if(!out||!bytes||bytes>256)return 0;
    uint64_t f=chos_spin_lock_irqsave(&lock);size_t n=0;
    if(ready){
        if(mbedtls_hmac_drbg_random(&context,out,bytes)==0){++calls;n=bytes;}
        else mbedtls_platform_zeroize(out,bytes);
        if(!n || calls==CHOS_SEED_RNG_CALL_LIMIT){ready=0;mbedtls_hmac_drbg_free(&context);}
    }
    chos_spin_unlock_irqrestore(&lock,f);return n;
}
int chos_crypto_strcmp(const char *a,const char *b){
    while(*a && *a==*b){++a;++b;}return (unsigned char)*a-(unsigned char)*b;
}
