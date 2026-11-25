/* CHOS WebCore key ownership adapter, 2026-11-20.
 * Actual maintained MbedTLS objects; no TLS transport or trust policy. */
#ifndef CHOS_WEBCORE_KEY_BACKEND_H
#define CHOS_WEBCORE_KEY_BACKEND_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct chos_webcore_key chos_webcore_key;
typedef int (*chos_webcore_key_rng)(void *, unsigned char *, size_t);
#ifdef __CHOS__
int chos_webcore_key_entropy(void *, unsigned char *, size_t);
#endif
enum chos_webcore_key_family { CHOS_KEY_RSA=1, CHOS_KEY_EC=2 };
enum chos_webcore_key_curve { CHOS_KEY_NO_CURVE=0, CHOS_KEY_P256=256, CHOS_KEY_P384=384, CHOS_KEY_P521=521 };
enum chos_webcore_key_status { CHOS_KEY_BAD_INPUT=-1, CHOS_KEY_UNSUPPORTED=-2, CHOS_KEY_NO_MEMORY=-3, CHOS_KEY_NO_ENTROPY=-4 };
typedef struct { unsigned family,curve,bits,is_private; } chos_webcore_key_info;

/* Only unencrypted version-0 PKCS8 and public SPKI are accepted. Inputs must
 * contain exactly one DER envelope; raw PKCS1/SEC1 and trailing bytes fail.
 * A private-key consistency check uses the supplied real RNG; no fallback. */
chos_webcore_key *chos_webcore_key_import_spki(unsigned family,unsigned curve,
    const uint8_t *,size_t,int *error);
chos_webcore_key *chos_webcore_key_import_pkcs8(unsigned family,unsigned curve,
    const uint8_t *,size_t,chos_webcore_key_rng,void *,int *error);
typedef struct { const uint8_t *data; size_t size; } chos_key_number;
typedef struct { chos_key_number n, e, d, p, q, dp, dq, qi; } chos_rsa_components;
#define CHOS_EC_MAX_WIDTH 66u
/* SEC1 uncompressed point and optional fixed-width private scalar. */
chos_webcore_key *chos_key_from_ec_point(unsigned curve, const uint8_t *point, size_t bytes,
    const uint8_t *scalar, size_t scalar_bytes, chos_webcore_key_rng, void *);
int chos_key_ec_fields(const chos_webcore_key *, uint8_t point[1 + 2 * CHOS_EC_MAX_WIDTH],
    uint8_t *scalar, size_t *width);
chos_webcore_key *chos_key_from_rsa_numbers(const chos_rsa_components *, int private_key);
enum chos_rsa_field { CHOS_RSA_N, CHOS_RSA_E, CHOS_RSA_D, CHOS_RSA_P, CHOS_RSA_Q, CHOS_RSA_DP, CHOS_RSA_DQ, CHOS_RSA_QI };
#define CHOS_RSA_MAX_WIDTH 1024u
int chos_key_rsa_field(const chos_webcore_key *, enum chos_rsa_field, uint8_t out[CHOS_RSA_MAX_WIDTH], size_t *);
void chos_key_clear_secret(void *, size_t);
int chos_webcore_key_get_info(const chos_webcore_key *,chos_webcore_key_info *);
int chos_webcore_key_export_spki(const chos_webcore_key *,uint8_t **,size_t *);
int chos_webcore_key_export_pkcs8(const chos_webcore_key *,uint8_t **,size_t *);
void chos_webcore_key_free_bytes(uint8_t *,size_t);
void chos_webcore_key_destroy(chos_webcore_key *);
#ifdef __cplusplus
}
#endif
#endif
