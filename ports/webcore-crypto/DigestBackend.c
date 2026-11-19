/* CHOS PAL digest backend, 2026-11-16. Cryptographic implementations are the
 * original pinned MbedTLS functions. Contexts and cached digests are erased
 * through the original platform zeroization API before memory is released. */
#include "DigestBackend.h"
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <mbedtls/platform_util.h>
#include <stdlib.h>
#include <string.h>

struct chos_digest {
    enum chos_digest_algorithm algorithm;
    mbedtls_sha1_context sha1;
    mbedtls_sha256_context sha256;
    mbedtls_sha512_context sha512;
    uint8_t result[64];
    size_t length;
    unsigned finished, failed;
};

void chos_digest_destroy(struct chos_digest *digest)
{
    if (!digest) return;
    mbedtls_sha1_free(&digest->sha1);
    mbedtls_sha256_free(&digest->sha256);
    mbedtls_sha512_free(&digest->sha512);
    mbedtls_platform_zeroize(digest, sizeof(*digest));
    free(digest);
}

struct chos_digest *chos_digest_create(enum chos_digest_algorithm algorithm)
{
    if (algorithm < CHOS_DIGEST_SHA1 || algorithm > CHOS_DIGEST_SHA512)
        return NULL;
    struct chos_digest *digest = calloc(1, sizeof(*digest));
    if (!digest) return NULL;
    digest->algorithm = algorithm;
    mbedtls_sha1_init(&digest->sha1);
    mbedtls_sha256_init(&digest->sha256);
    mbedtls_sha512_init(&digest->sha512);
    int result;
    switch (algorithm) {
    case CHOS_DIGEST_SHA1:
        digest->length = 20;
        result = mbedtls_sha1_starts(&digest->sha1); break;
    case CHOS_DIGEST_SHA224:
    case CHOS_DIGEST_SHA256:
        digest->length = algorithm == CHOS_DIGEST_SHA224 ? 28 : 32;
        result = mbedtls_sha256_starts(&digest->sha256, algorithm == CHOS_DIGEST_SHA224); break;
    case CHOS_DIGEST_SHA384:
    case CHOS_DIGEST_SHA512:
        digest->length = algorithm == CHOS_DIGEST_SHA384 ? 48 : 64;
        result = mbedtls_sha512_starts(&digest->sha512, algorithm == CHOS_DIGEST_SHA384); break;
    default:
        result = -1;
    }
    if (result) { chos_digest_destroy(digest); return NULL; }
    return digest;
}

static int invalidate(struct chos_digest *digest)
{
    digest->failed = 1;
    mbedtls_platform_zeroize(digest->result, sizeof(digest->result));
    return -1;
}

int chos_digest_add(struct chos_digest *digest, const uint8_t *input, size_t length)
{
    if (!digest) return -1;
    if (digest->failed || digest->finished || (!input && length))
        return invalidate(digest);
    if (!length) return 0;
    int result;
    switch (digest->algorithm) {
    case CHOS_DIGEST_SHA1:
        result = mbedtls_sha1_update(&digest->sha1, input, length); break;
    case CHOS_DIGEST_SHA224:
    case CHOS_DIGEST_SHA256:
        result = mbedtls_sha256_update(&digest->sha256, input, length); break;
    case CHOS_DIGEST_SHA384:
    case CHOS_DIGEST_SHA512:
        result = mbedtls_sha512_update(&digest->sha512, input, length); break;
    default:
        return invalidate(digest);
    }
    return result ? invalidate(digest) : 0;
}

int chos_digest_finish(struct chos_digest *digest, uint8_t output[64], size_t *length)
{
    if (length) *length = 0;
    if (output) memset(output, 0, 64);
    if (!digest || !output || !length || digest->failed) return -1;
    if (!digest->finished) {
        int result;
        switch (digest->algorithm) {
        case CHOS_DIGEST_SHA1:
            result = mbedtls_sha1_finish(&digest->sha1, digest->result); break;
        case CHOS_DIGEST_SHA224:
        case CHOS_DIGEST_SHA256:
            result = mbedtls_sha256_finish(&digest->sha256, digest->result); break;
        case CHOS_DIGEST_SHA384:
        case CHOS_DIGEST_SHA512:
            result = mbedtls_sha512_finish(&digest->sha512, digest->result); break;
        default:
            return invalidate(digest);
        }
        if (result) return invalidate(digest);
        digest->finished = 1;
    }
    memcpy(output, digest->result, digest->length);
    *length = digest->length;
    return 0;
}
