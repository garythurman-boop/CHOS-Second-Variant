// DER import/export for the native WebCore key classes. The backend owns the
// parsed Mbed TLS key and validates the envelope, key family and named curve.
#include "config.h"
#include "CryptoKeyEC.h"
#include "CryptoKeyRSA.h"
#include "ExceptionOr.h"
#include "KeyBackend.h"

namespace WebCore {

static unsigned curveBits(CryptoKeyEC::NamedCurve curve)
{
    switch (curve) {
    case CryptoKeyEC::NamedCurve::P256: return CHOS_KEY_P256;
    case CryptoKeyEC::NamedCurve::P384: return CHOS_KEY_P384;
    case CryptoKeyEC::NamedCurve::P521: return CHOS_KEY_P521;
    }
    return 0;
}

static size_t keyBits(const chos_webcore_key* key)
{
    chos_webcore_key_info info;
    return chos_webcore_key_get_info(key, &info) ? 0 : info.bits;
}

static Vector<uint8_t> exportDER(const chos_webcore_key* key, CryptoKeyType type)
{
    uint8_t* bytes = nullptr;
    size_t length = 0;
    int rc = type == CryptoKeyType::Private
        ? chos_webcore_key_export_pkcs8(key, &bytes, &length)
        : chos_webcore_key_export_spki(key, &bytes, &length);
    if (rc)
        return {};
    Vector<uint8_t> result;
    bool copied = result.tryAppend(std::span(bytes, length));
    chos_webcore_key_free_bytes(bytes, length);
    if (!copied)
        return {};
    return result;
}

bool CryptoKeyEC::platformSupportedCurve(NamedCurve curve)
{
    return curveBits(curve);
}

size_t CryptoKeyEC::keySizeInBits() const
{
    return keyBits(platformKey().get());
}

RefPtr<CryptoKeyEC> CryptoKeyEC::platformImportSpki(CryptoAlgorithmIdentifier algorithm, NamedCurve curve,
    Vector<uint8_t>&& data, bool extractable, CryptoKeyUsageBitmap usages)
{
    PlatformECKeyContainer key(chos_webcore_key_import_spki(CHOS_KEY_EC, curveBits(curve),
        data.span().data(), data.size(), nullptr));
    if (!key)
        return nullptr;
    return create(algorithm, curve, CryptoKeyType::Public, WTF::move(key), extractable, usages);
}

RefPtr<CryptoKeyEC> CryptoKeyEC::platformImportPkcs8(CryptoAlgorithmIdentifier algorithm, NamedCurve curve,
    Vector<uint8_t>&& data, bool extractable, CryptoKeyUsageBitmap usages)
{
    PlatformECKeyContainer key(chos_webcore_key_import_pkcs8(CHOS_KEY_EC, curveBits(curve),
        data.span().data(), data.size(), chos_webcore_key_entropy, nullptr, nullptr));
    if (!key)
        return nullptr;
    return create(algorithm, curve, CryptoKeyType::Private, WTF::move(key), extractable, usages);
}

Vector<uint8_t> CryptoKeyEC::platformExportSpki() const
{
    return exportDER(platformKey().get(), CryptoKeyType::Public);
}

Vector<uint8_t> CryptoKeyEC::platformExportPkcs8() const
{
    return exportDER(platformKey().get(), CryptoKeyType::Private);
}

size_t CryptoKeyRSA::keySizeInBits() const
{
    return keyBits(platformKey());
}

RefPtr<CryptoKeyRSA> CryptoKeyRSA::importSpki(CryptoAlgorithmIdentifier algorithm,
    std::optional<CryptoAlgorithmIdentifier> hash, Vector<uint8_t>&& data, bool extractable, CryptoKeyUsageBitmap usages)
{
    PlatformRSAKeyContainer key(chos_webcore_key_import_spki(CHOS_KEY_RSA, CHOS_KEY_NO_CURVE,
        data.span().data(), data.size(), nullptr));
    if (!key)
        return nullptr;
    return create(algorithm, hash.value_or(CryptoAlgorithmIdentifier::SHA_1), hash.has_value(),
        CryptoKeyType::Public, WTF::move(key), extractable, usages);
}

RefPtr<CryptoKeyRSA> CryptoKeyRSA::importPkcs8(CryptoAlgorithmIdentifier algorithm,
    std::optional<CryptoAlgorithmIdentifier> hash, Vector<uint8_t>&& data, bool extractable, CryptoKeyUsageBitmap usages)
{
    PlatformRSAKeyContainer key(chos_webcore_key_import_pkcs8(CHOS_KEY_RSA, CHOS_KEY_NO_CURVE,
        data.span().data(), data.size(), chos_webcore_key_entropy, nullptr, nullptr));
    if (!key)
        return nullptr;
    return create(algorithm, hash.value_or(CryptoAlgorithmIdentifier::SHA_1), hash.has_value(),
        CryptoKeyType::Private, WTF::move(key), extractable, usages);
}

static ExceptionOr<Vector<uint8_t>> exportRSA(const CryptoKeyRSA& key, CryptoKeyType requiredType)
{
    if (key.type() != requiredType)
        return Exception { ExceptionCode::InvalidAccessError };
    auto result = exportDER(key.platformKey(), requiredType);
    if (result.isEmpty())
        return Exception { ExceptionCode::OperationError };
    return result;
}

ExceptionOr<Vector<uint8_t>> CryptoKeyRSA::exportSpki() const
{
    return exportRSA(*this, CryptoKeyType::Public);
}

ExceptionOr<Vector<uint8_t>> CryptoKeyRSA::exportPkcs8() const
{
    return exportRSA(*this, CryptoKeyType::Private);
}

} // namespace WebCore
