/* CHOS PAL adaptation, 2026-11-16. Uses the unchanged original CryptoDigest
 * interface and original MbedTLS implementations through DigestBackend. */
#include "config.h"
#include <pal/ExportMacros.h>
#include <pal/crypto/CryptoDigest.h>
#include "DigestBackend.h"

namespace PAL::Crypto {

struct CryptoDigestContext {
    WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(CryptoDigestContext);
    explicit CryptoDigestContext(chos_digest* value) : digest(value) { }
    ~CryptoDigestContext() { chos_digest_destroy(digest); }
    chos_digest* digest;
};

CryptoDigest::CryptoDigest() = default;
CryptoDigest::~CryptoDigest() = default;

std::unique_ptr<CryptoDigest> CryptoDigest::create(Algorithm algorithm)
{
    chos_digest_algorithm selected;
    switch (algorithm) {
    case Algorithm::SHA_1: selected = CHOS_DIGEST_SHA1; break;
    case Algorithm::DEPRECATED_SHA_224: selected = CHOS_DIGEST_SHA224; break;
    case Algorithm::SHA_256: selected = CHOS_DIGEST_SHA256; break;
    case Algorithm::SHA_384: selected = CHOS_DIGEST_SHA384; break;
    case Algorithm::SHA_512: selected = CHOS_DIGEST_SHA512; break;
    default: return nullptr;
    }
    auto* backend = chos_digest_create(selected);
    if (!backend) return nullptr;
    auto result = WTF::makeUnique<CryptoDigest>();
    result->m_context = WTF::makeUnique<CryptoDigestContext>(backend);
    return result;
}

void CryptoDigest::addBytes(std::span<const uint8_t> input)
{
    if (m_context)
        chos_digest_add(m_context->digest, input.data(), input.size());
}

Vector<uint8_t> CryptoDigest::computeHash()
{
    uint8_t result[64];
    size_t length;
    if (!m_context || chos_digest_finish(m_context->digest, result, &length))
        return { };
    return Vector<uint8_t>(std::span<const uint8_t>(result, length));
}

} // namespace PAL::Crypto
