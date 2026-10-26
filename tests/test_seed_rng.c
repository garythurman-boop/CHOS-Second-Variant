#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "chos/seed_rng.h"
#include "mbedtls/hmac_drbg.h"
#include "mbedtls/sha256.h"
extern void *calloc(size_t,size_t);
extern void free(void *);
static unsigned live,fail_alloc;
void *chos_crypto_calloc(size_t n,size_t size){if(fail_alloc)return NULL;void *p=calloc(n,size);if(p)++live;return p;}
void chos_crypto_free(void *p){if(p){assert(live);--live;free(p);}}
static unsigned unhex(const char *s,uint8_t *p){unsigned n=0;while(*s){unsigned v=0;for(unsigned i=0;i<2;i++){unsigned c=(unsigned char)*s++;v=(v<<4)|(c<='9'?c-'0':c-'a'+10);}p[n++]=(uint8_t)v;}return n;}
int main(int argc,char **argv){
 uint8_t seed[64],out[256],before[256];for(unsigned i=0;i<64;i++)seed[i]=(uint8_t)i;
 memset(out,0xa5,sizeof(out));memcpy(before,out,sizeof(out));
 assert(!chos_seed_rng_ready()&&!chos_seed_rng_read(out,32)&&!memcmp(out,before,sizeof(out)));
 if(argc>1){
  if(argv[1][0]=='z')memset(seed,0,64);
  else if(argv[1][0]=='r')memcpy(seed+32,seed,32);
  else fail_alloc=1;
  assert(!chos_seed_rng_init(seed,64));for(unsigned i=0;i<64;i++)assert(!seed[i]);
  assert(!live&&!chos_seed_rng_ready()&&!chos_seed_rng_read(out,32));return 0;
 }
 assert(!mbedtls_sha256_self_test(0));
 /* Upstream NIST CAVS14.3 SHA256 no-reseed vector0, two128-byte generations. */
 mbedtls_hmac_drbg_context kat;mbedtls_hmac_drbg_init(&kat);
 uint8_t input[64],wanted[128];unsigned count=unhex("ca851911349384bffe89de1cbdc46e6831e44d34a4fb935ee285dd14b71a7488659ba96c601dc69fc902940805ec0ca8",input);
 assert(unhex("e528e9abf2dece54d47c7e75e5fe302149f817ea9fb4bee6f4199697d04d5b89d54fbb978a15b5c443c9ec21036d2460b6f73ebad0dc2aba6e624abf07745bc107694bb7547bb0995f70de25d6b29e2d3011bb19d27676c07162c8b5ccde0668961df86803482cb37ed6d5c0bb8d50cf1f50d476aa0458bdaba806f48be9dcb8",wanted)==128);
 assert(!mbedtls_hmac_drbg_seed_buf(&kat,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),input,count));
 assert(!mbedtls_hmac_drbg_random(&kat,out,128));assert(!mbedtls_hmac_drbg_random(&kat,out,128));
 assert(!memcmp(out,wanted,128));mbedtls_hmac_drbg_free(&kat);assert(!live);
 assert(chos_seed_rng_init(seed,64));for(unsigned i=0;i<64;i++)assert(!seed[i]);
 assert(chos_seed_rng_ready());assert(!chos_seed_rng_read(NULL,32)&&!chos_seed_rng_read(out,257)&&!chos_seed_rng_read(out,0));
 assert(chos_seed_rng_read(out,256)==256);memcpy(before,out,256);
 assert(chos_seed_rng_read(out,256)==256&&memcmp(out,before,256));
 for(unsigned i=2;i<CHOS_SEED_RNG_CALL_LIMIT;i++)assert(chos_seed_rng_read(out,32)==32);
 assert(!chos_seed_rng_ready()&&!live);memcpy(before,out,256);
 assert(!chos_seed_rng_read(out,32)&&!memcmp(out,before,256));
 memset(seed,0x42,64);assert(!chos_seed_rng_init(seed,64));for(unsigned i=0;i<64;i++)assert(!seed[i]);
 return 0;
}
