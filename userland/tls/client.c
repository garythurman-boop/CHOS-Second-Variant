#include "chos_tls.h"
#include "chos/user.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mbedtls/ssl.h"
#include "mbedtls/hmac_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"
#include "psa/crypto_extra.h"
#define TLS_TIMEOUT_MS UINT64_C(15000)
struct chos_tls_client {
 mbedtls_ssl_context ssl;
 mbedtls_ssl_config config;
 mbedtls_x509_crt anchors;
 mbedtls_hmac_drbg_context random;
 uint64_t deadline;
 uint32_t flags;
 int socket,state,error,psa_reference;
};
/* All native client APIs are single-threaded. PSA's external RNG draws from
 * the currently executing, already-seeded client. Never from a hidden source.
 * Separate EL0 processes have separate globals. Threads require a new contract. */
static mbedtls_hmac_drbg_context *active_random;
static unsigned psa_clients;
psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t *context,
                                            uint8_t *out,size_t bytes,size_t *length){
 (void)context;
 if(!length||(!out&&bytes))return PSA_ERROR_INVALID_ARGUMENT;
 *length=0;
 if(!active_random||bytes>MBEDTLS_HMAC_DRBG_MAX_REQUEST){
  if(out)mbedtls_platform_zeroize(out,bytes);
  return PSA_ERROR_INSUFFICIENT_ENTROPY;
 }
 if(mbedtls_hmac_drbg_random(active_random,out,bytes)){
  mbedtls_platform_zeroize(out,bytes);return PSA_ERROR_INSUFFICIENT_ENTROPY;
 }
 *length=bytes;return PSA_SUCCESS;
}
static int now_ms(uint64_t *out){
 struct timespec t;
 if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0 || (uint64_t)t.tv_sec>(UINT64_MAX-TLS_TIMEOUT_MS)/1000u-1u)return -1;
 *out=(uint64_t)t.tv_sec*1000u+(unsigned long)t.tv_nsec/1000000u;return 0;
}
static int clock_valid(void){
 time_t t=chos_tls_time(0);struct tm date;
 /* Availability is required; trustworthy system-time provisioning remains open.
  * This profile does not assert authenticity of firmware/RTC time. */
 return t>=INT64_C(1735689600) && gmtime_r(&t,&date)!=0;
}
static int fail(chos_tls_client_t *c,int error){
 c->error=error;c->flags=mbedtls_ssl_get_verify_result(&c->ssl);c->state=CHOS_TLS_FAILED;
 if(c->socket>0){(void)chos_net_tcp_abort(c->socket);c->socket=0;}
 return CHOS_TLS_ERROR;
}
static int deadline(chos_tls_client_t *c){
 uint64_t now;
 if(now_ms(&now)||!clock_valid())return fail(c,MBEDTLS_ERR_SSL_INTERNAL_ERROR);
 if(now>=c->deadline)return fail(c,MBEDTLS_ERR_SSL_TIMEOUT);
 return 0;
}
static void progress(chos_tls_client_t *c){uint64_t now;if(!now_ms(&now))c->deadline=now+TLS_TIMEOUT_MS;}
static int entropy(void *context,unsigned char *out,size_t count){
 (void)context;
 /* One bounded source read: temporary unavailability never uses timing noise. */
 long n=chos_random_bytes(out,count);
 if(n!=(long)count){mbedtls_platform_zeroize(out,count);return MBEDTLS_ERR_HMAC_DRBG_ENTROPY_SOURCE_FAILED;}
 return 0;
}
static int send_bytes(void *context,const unsigned char *p,size_t n){
 chos_tls_client_t *c=context;
 if(n>CHOS_TCP_API_MAX_TRANSFER)n=CHOS_TCP_API_MAX_TRANSFER;
 long rc=chos_net_tcp_send(c->socket,p,n);
 if(rc==-CHOS_ERRNO_EAGAIN)return MBEDTLS_ERR_SSL_WANT_WRITE;
 return rc<0?MBEDTLS_ERR_SSL_INTERNAL_ERROR:(int)rc;
}
static int receive_bytes(void *context,unsigned char *p,size_t n){
 chos_tls_client_t *c=context;
 if(n>CHOS_TCP_API_MAX_TRANSFER)n=CHOS_TCP_API_MAX_TRANSFER;
 long rc=chos_net_tcp_recv(c->socket,p,n);
 if(rc==-CHOS_ERRNO_EAGAIN)return MBEDTLS_ERR_SSL_WANT_READ;
 return rc<0?MBEDTLS_ERR_SSL_INTERNAL_ERROR:(int)rc;
}
static int hostname_valid(const char *s){
 if(!s)return 0;size_t n=strnlen(s,254);if(!n||n>253)return 0;
 for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)s[i];
  if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return 0;}
 return 1;
}
chos_tls_client_t *chos_tls_create(uint32_t ipv4,uint16_t port,const char *hostname,const void *ca,size_t bytes){
 return chos_tls_create_ex(ipv4,port,hostname,ca,bytes,0);
}
chos_tls_client_t *chos_tls_create_ex(uint32_t ipv4,uint16_t port,const char *hostname,const void *ca,size_t bytes,int *initial_error){
 uint64_t now;
 int reason=CHOS_TLS_INIT_ARGUMENT;
 if(initial_error)*initial_error=reason;
 if(!ipv4||!port||!hostname_valid(hostname)||!ca||!bytes||bytes>65536u)return 0;
 if(initial_error)*initial_error=CHOS_TLS_INIT_CLOCK;
 if(!clock_valid()||now_ms(&now))return 0;
 if(initial_error)*initial_error=CHOS_TLS_INIT_MEMORY;
 chos_tls_client_t *c=calloc(1,sizeof(*c));if(!c)return 0;
 mbedtls_ssl_init(&c->ssl);mbedtls_ssl_config_init(&c->config);
 mbedtls_x509_crt_init(&c->anchors);mbedtls_hmac_drbg_init(&c->random);
 static const unsigned char personal[]="CHOS isolated TLS client v1";
 static const char *alpn[]={"http/1.1",0};
 reason=CHOS_TLS_INIT_LIBRARY;
 if(psa_crypto_init()!=PSA_SUCCESS)goto failed;
 ++psa_clients;c->psa_reference=1;
 reason=CHOS_TLS_INIT_CA;
 if(mbedtls_x509_crt_parse_der(&c->anchors,ca,bytes))goto failed;
 reason=CHOS_TLS_INIT_ENTROPY;
 if(mbedtls_hmac_drbg_seed(&c->random,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),entropy,0,personal,sizeof(personal)-1))goto failed;
 reason=CHOS_TLS_INIT_LIBRARY;
 if(mbedtls_ssl_config_defaults(&c->config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT))goto failed;
 mbedtls_ssl_conf_authmode(&c->config,MBEDTLS_SSL_VERIFY_REQUIRED);
 mbedtls_ssl_conf_ca_chain(&c->config,&c->anchors,0);
 mbedtls_ssl_conf_rng(&c->config,mbedtls_hmac_drbg_random,&c->random);
 if(mbedtls_ssl_conf_alpn_protocols(&c->config,alpn)||mbedtls_ssl_setup(&c->ssl,&c->config)||mbedtls_ssl_set_hostname(&c->ssl,hostname))goto failed;
 reason=CHOS_TLS_INIT_TRANSPORT;
 c->socket=(int)chos_net_tcp_open(0);
 if(c->socket<=0)goto failed;
 mbedtls_ssl_set_bio(&c->ssl,c,send_bytes,receive_bytes,0);
 if(chos_net_tcp_connect(c->socket,ipv4,port)<0)goto failed;
 c->state=CHOS_TLS_CONNECTING;c->deadline=now+TLS_TIMEOUT_MS;
 if(initial_error)*initial_error=0;
 return c;
failed:
 if(initial_error)*initial_error=reason;
 chos_tls_destroy(c);return 0;
}
int chos_tls_step(chos_tls_client_t *c){
 if(!c||c->state==CHOS_TLS_FAILED||c->state==CHOS_TLS_CLOSED)return CHOS_TLS_ERROR;
 if(deadline(c))return CHOS_TLS_ERROR;
 if(c->state==CHOS_TLS_CONNECTING){
  chos_tcp_socket_info_t info;
  if(chos_net_tcp_status(c->socket,&info)<0||info.state==CHOS_TCP_FAILED||info.state==CHOS_TCP_CLOSED)return fail(c,MBEDTLS_ERR_SSL_INTERNAL_ERROR);
  if(info.state!=CHOS_TCP_ESTABLISHED)return CHOS_TLS_AGAIN;
  c->state=CHOS_TLS_HANDSHAKE;
 }
 if(c->state==CHOS_TLS_READY)return 0;
 active_random=&c->random;
 int rc=mbedtls_ssl_handshake(&c->ssl);
 active_random=0;
 if(rc==MBEDTLS_ERR_SSL_WANT_READ||rc==MBEDTLS_ERR_SSL_WANT_WRITE)return CHOS_TLS_AGAIN;
 if(rc)return fail(c,rc);
 c->flags=mbedtls_ssl_get_verify_result(&c->ssl);
 if(c->flags)return fail(c,MBEDTLS_ERR_X509_CERT_VERIFY_FAILED);
 c->state=CHOS_TLS_READY;progress(c);return 0;
}
int chos_tls_read(chos_tls_client_t *c,void *p,size_t n){
 if(!c||!p||!n||n>16384u||c->state!=CHOS_TLS_READY||deadline(c))return CHOS_TLS_ERROR;
 active_random=&c->random;
 int rc=mbedtls_ssl_read(&c->ssl,p,n);
 active_random=0;
 if(rc==MBEDTLS_ERR_SSL_WANT_READ||rc==MBEDTLS_ERR_SSL_WANT_WRITE)return CHOS_TLS_AGAIN;
 /* Raw TCP EOF is an unauthenticated truncation, never successful TLS EOF. */
 if(rc==0)return fail(c,MBEDTLS_ERR_SSL_CONN_EOF);
 if(rc==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY){c->state=CHOS_TLS_CLOSED;return 0;}
 if(rc<0)return fail(c,rc);
 progress(c);return rc;
}
int chos_tls_write(chos_tls_client_t *c,const void *p,size_t n){
 if(!c||!p||!n||n>16384u||c->state!=CHOS_TLS_READY||deadline(c))return CHOS_TLS_ERROR;
 active_random=&c->random;
 int rc=mbedtls_ssl_write(&c->ssl,p,n);
 active_random=0;
 if(rc==MBEDTLS_ERR_SSL_WANT_READ||rc==MBEDTLS_ERR_SSL_WANT_WRITE)return CHOS_TLS_AGAIN;
 if(rc<=0)return fail(c,rc?rc:MBEDTLS_ERR_SSL_INTERNAL_ERROR);
 progress(c);return rc;
}
int chos_tls_state(const chos_tls_client_t *c){return c?c->state:CHOS_TLS_FAILED;}
int chos_tls_error(const chos_tls_client_t *c){return c?c->error:MBEDTLS_ERR_SSL_BAD_INPUT_DATA;}
uint32_t chos_tls_verify_flags(const chos_tls_client_t *c){return c?c->flags:UINT32_MAX;}
void chos_tls_destroy(chos_tls_client_t *c){
 if(!c)return;
 if(c->socket>0)(void)chos_net_tcp_abort(c->socket);
 mbedtls_ssl_free(&c->ssl);mbedtls_ssl_config_free(&c->config);
 mbedtls_x509_crt_free(&c->anchors);mbedtls_hmac_drbg_free(&c->random);
 if(c->psa_reference&&psa_clients&&!--psa_clients)mbedtls_psa_crypto_free();
 mbedtls_platform_zeroize(c,sizeof(*c));free(c);
}
