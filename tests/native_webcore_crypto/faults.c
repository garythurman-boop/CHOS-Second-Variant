/* Linker-only negative-path injection; production objects use original APIs. */
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <stdatomic.h>
#include <stdlib.h>
static _Atomic unsigned selected,hits;
void digest_arm_fault(unsigned id){atomic_store(&hits,0);atomic_store(&selected,id);}
unsigned digest_fault_hits(void){return atomic_load(&hits);}
static int fail(unsigned id){unsigned expected=id;if(atomic_compare_exchange_strong(&selected,&expected,0)){atomic_fetch_add(&hits,1);return 1;}return 0;}
unsigned digest_fault_id(unsigned algorithm,unsigned operation){return (algorithm==1?0:algorithm<=3?3:6)+operation;}
#define START(name,type,id) int __real_##name(type*);int __wrap_##name(type*c){return fail(id)?-1:__real_##name(c);}
#define START_VARIANT(name,type,id) int __real_##name(type*,int);int __wrap_##name(type*c,int v){return fail(id)?-1:__real_##name(c,v);}
#define UPDATE(name,type,id) int __real_##name(type*,const unsigned char*,size_t);int __wrap_##name(type*c,const unsigned char*b,size_t n){return fail(id)?-1:__real_##name(c,b,n);}
#define FINISH(name,type,id) int __real_##name(type*,unsigned char*);int __wrap_##name(type*c,unsigned char*b){return fail(id)?-1:__real_##name(c,b);}
START(chos_pal_mbedtls_sha1_starts,mbedtls_sha1_context,1)
UPDATE(chos_pal_mbedtls_sha1_update,mbedtls_sha1_context,2)
FINISH(chos_pal_mbedtls_sha1_finish,mbedtls_sha1_context,3)
START_VARIANT(chos_pal_mbedtls_sha256_starts,mbedtls_sha256_context,4)
UPDATE(chos_pal_mbedtls_sha256_update,mbedtls_sha256_context,5)
FINISH(chos_pal_mbedtls_sha256_finish,mbedtls_sha256_context,6)
START_VARIANT(chos_pal_mbedtls_sha512_starts,mbedtls_sha512_context,7)
UPDATE(chos_pal_mbedtls_sha512_update,mbedtls_sha512_context,8)
FINISH(chos_pal_mbedtls_sha512_finish,mbedtls_sha512_context,9)
void *__real_calloc(size_t,size_t);
void *__wrap_calloc(size_t n,size_t s){return fail(10)?NULL:__real_calloc(n,s);}
