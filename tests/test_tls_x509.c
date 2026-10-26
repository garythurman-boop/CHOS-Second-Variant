/* Actual pinned X.509 parser/verification under host ASan/UBSan.
 * Native transport is independently exercised by qemu_tls.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "psa/crypto_extra.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"
/* Verification fixtures must not ask for random secret-key material. */
psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t *ctx,uint8_t *p,size_t n,size_t *length){
 (void)ctx;(void)p;(void)n;*length=0;return PSA_ERROR_INSUFFICIENT_ENTROPY;
}
static time_t fixture_time=INT64_C(1906545600); /*2030-06-01T12:00:00Z*/
time_t chos_tls_time(time_t *p){if(p)*p=fixture_time;return fixture_time;}
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *p,struct tm *out){return gmtime_r(p,out);}
mbedtls_ms_time_t mbedtls_ms_time(void){return 1;}
void chos_tls_assert_failed(void){abort();}
static unsigned char *read_file(const char *path,size_t *bytes){
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0&&n<=8192);rewind(f);
 unsigned char *p=malloc((size_t)n);assert(p&&fread(p,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));*bytes=(size_t)n;return p;
}
static uint32_t verify(const unsigned char *ca,size_t ca_bytes,const unsigned char *leaf,size_t leaf_bytes,const char *host){
 mbedtls_x509_crt anchor,peer;mbedtls_x509_crt_init(&anchor);mbedtls_x509_crt_init(&peer);
 assert(!mbedtls_x509_crt_parse_der(&anchor,ca,ca_bytes));assert(!mbedtls_x509_crt_parse_der(&peer,leaf,leaf_bytes));
 uint32_t flags=0;int rc=mbedtls_x509_crt_verify(&peer,&anchor,0,host,&flags,0,0);
 assert((rc==0)==(flags==0));mbedtls_x509_crt_free(&peer);mbedtls_x509_crt_free(&anchor);return flags;
}
int main(int argc,char **argv){
 assert(argc==4);assert(psa_crypto_init()==PSA_SUCCESS);size_t ca_bytes,other_bytes,leaf_bytes;
 unsigned char *ca=read_file(argv[1],&ca_bytes),*other=read_file(argv[2],&other_bytes),*leaf=read_file(argv[3],&leaf_bytes);
 assert(!verify(ca,ca_bytes,leaf,leaf_bytes,"fixture.chos.test"));
 assert(verify(ca,ca_bytes,leaf,leaf_bytes,"wrong.chos.test")&MBEDTLS_X509_BADCERT_CN_MISMATCH);
 assert(verify(other,other_bytes,leaf,leaf_bytes,"fixture.chos.test")&MBEDTLS_X509_BADCERT_NOT_TRUSTED);
 fixture_time=INT64_C(2145916800);assert(verify(ca,ca_bytes,leaf,leaf_bytes,"fixture.chos.test")&MBEDTLS_X509_BADCERT_EXPIRED);
 fixture_time=INT64_C(1830297600);assert(verify(ca,ca_bytes,leaf,leaf_bytes,"fixture.chos.test")&MBEDTLS_X509_BADCERT_FUTURE);
 fixture_time=INT64_C(1906545600);
 leaf[leaf_bytes-1]^=1;assert(verify(ca,ca_bytes,leaf,leaf_bytes,"fixture.chos.test"));leaf[leaf_bytes-1]^=1;
 for(size_t n=0;n<leaf_bytes;n++){
  mbedtls_x509_crt crt;mbedtls_x509_crt_init(&crt);assert(mbedtls_x509_crt_parse_der(&crt,leaf,n)!=0);mbedtls_x509_crt_free(&crt);
 }
 for(size_t n=1;n<=8192;n*=2){
  unsigned char *bad=calloc(1,n);assert(bad);mbedtls_x509_crt crt;mbedtls_x509_crt_init(&crt);
  assert(mbedtls_x509_crt_parse_der(&crt,bad,n)!=0);mbedtls_x509_crt_free(&crt);free(bad);
 }
 free(ca);free(other);free(leaf);mbedtls_psa_crypto_free();puts("TLS X.509: pinned parser, hostname/chain/date/signature failures and every truncation passed");return 0;
}
