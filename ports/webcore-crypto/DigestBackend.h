/* CHOS PAL digest backend, 2026-11-16. Private C interface, not a WebCrypto API. */
#ifndef CHOS_DIGEST_BACKEND_H
#define CHOS_DIGEST_BACKEND_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum chos_digest_algorithm {
    CHOS_DIGEST_SHA1 = 1, CHOS_DIGEST_SHA224, CHOS_DIGEST_SHA256,
    CHOS_DIGEST_SHA384, CHOS_DIGEST_SHA512
};
struct chos_digest;
struct chos_digest *chos_digest_create(enum chos_digest_algorithm);
void chos_digest_destroy(struct chos_digest *);
int chos_digest_add(struct chos_digest *, const uint8_t *, size_t);
/* Requires a 64-byte output buffer. Error leaves zero bytes and length zero.
 * Repeated finish returns the same result; adding data afterwards fails and
 * invalidates that result. Failures remain sticky until destruction. */
int chos_digest_finish(struct chos_digest *, uint8_t output[64], size_t *length);
#ifdef __cplusplus
}
#endif
#endif
