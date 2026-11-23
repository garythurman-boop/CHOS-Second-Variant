/* Owned MbedTLS DER inspection. No chain authentication is performed here. */
#ifndef CHOS_CERTIFICATE_BACKEND_H
#define CHOS_CERTIFICATE_BACKEND_H
#include <stddef.h>
#include <stdint.h>
#define CHOS_CERTIFICATE_MAX_DER_BYTES (256u * 1024u)
#ifdef __cplusplus
extern "C" {
#endif
typedef struct chos_certificate chos_certificate;
/* Returns NULL for malformed, unsupported or over-limit DER/allocation failure.
 * Exact DER consumption is required; PEM and trailing bytes are rejected. */
chos_certificate *chos_certificate_parse(const uint8_t *, size_t);
void chos_certificate_destroy(chos_certificate *);
const unsigned char *chos_certificate_subject(const chos_certificate *, size_t *, int *);
int64_t chos_certificate_valid_from(const chos_certificate *);
int64_t chos_certificate_valid_until(const chos_certificate *);
int chos_certificate_signature_is_sha1(const chos_certificate *);
size_t chos_certificate_san_count(const chos_certificate *);
/* type 2 = DNS IA5 bytes, 7 = binary IPv4/IPv6. Other SAN kinds are omitted.
 * Result borrows the parsed context and is invalid after destroy. */
const unsigned char *chos_certificate_san(const chos_certificate *, size_t, size_t *, int *);
#ifdef __cplusplus
}
#endif
#endif
