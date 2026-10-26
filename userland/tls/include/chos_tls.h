#ifndef CHOS_TLS_H
#define CHOS_TLS_H
#include <stddef.h>
#include <stdint.h>
/* Experimental native client prerequisite, not the browser or a system trust store.
 * All APIs are single-threaded and owned by the creating process. */
typedef struct chos_tls_client chos_tls_client_t;
enum { CHOS_TLS_CONNECTING=1, CHOS_TLS_HANDSHAKE, CHOS_TLS_READY, CHOS_TLS_FAILED, CHOS_TLS_CLOSED };
#define CHOS_TLS_AGAIN (-1)
#define CHOS_TLS_ERROR (-2)
enum { CHOS_TLS_INIT_ARGUMENT=1, CHOS_TLS_INIT_CLOCK, CHOS_TLS_INIT_MEMORY, CHOS_TLS_INIT_CA, CHOS_TLS_INIT_ENTROPY, CHOS_TLS_INIT_LIBRARY, CHOS_TLS_INIT_TRANSPORT };
/* CA is a caller-selected DER trust anchor; hostname is mandatory, never optional.
 * No socket is opened if configuration, clock or cryptographic seeding fails. */
chos_tls_client_t *chos_tls_create(uint32_t ipv4,uint16_t port,const char *hostname,
                                  const void *ca_der,size_t ca_bytes);
chos_tls_client_t *chos_tls_create_ex(uint32_t ipv4,uint16_t port,const char *hostname,
                                     const void *ca_der,size_t ca_bytes,int *initial_error);
int chos_tls_step(chos_tls_client_t *);
int chos_tls_read(chos_tls_client_t *,void *,size_t);
/* On AGAIN retry the identical pointer and length, per MbedTLS's write contract. */
int chos_tls_write(chos_tls_client_t *,const void *,size_t);
int chos_tls_state(const chos_tls_client_t *);
int chos_tls_error(const chos_tls_client_t *);
uint32_t chos_tls_verify_flags(const chos_tls_client_t *);
void chos_tls_destroy(chos_tls_client_t *);
#endif
