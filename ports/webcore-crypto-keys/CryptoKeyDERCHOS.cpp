// DER import/export for the native WebCore key classes. The backend owns the
// parsed Mbed TLS key and validates the envelope, key family and named curve.
#include "config.h"
#include "CryptoKeyEC.h"
#include "CryptoKeyRSA.h"
#include "ExceptionOr.h"
#include "CryptoKeyRSAComponents.h"
#include "CryptoAlgorithmRegistry.h"
#include <JavaScriptCore/TypedArrayInlines.h>
#include "JsonWebKey.h"
#include <wtf/text/Base64.h>
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

RefPtr<CryptoKeyEC> CryptoKeyEC::platformImportRaw(CryptoAlgorithmIdentifier algorithm, NamedCurve curve,
    Vector<uint8_t>&& data, bool extractable, CryptoKeyUsageBitmap usages)
{
    PlatformECKeyContainer key(chos_key_from_ec_point(curveBits(curve), data.span().data(), data.size(), nullptr, 0, nullptr, nullptr));
    if (!key)
        return nullptr;
    return create(algorithm, curve, CryptoKeyType::Public, WTF::move(key), extractable, usages);
}

static PlatformECKeyContainer keyFromFields(unsigned bits, std::span<const uint8_t> x, std::span<const uint8_t> y,
    std::span<const uint8_t> scalar)
{
    size_t width = (bits + 7) / 8;
    if (!bits || width > CHOS_EC_MAX_WIDTH || x.size() != width || y.size() != width)
        return nullptr;
    std::array<uint8_t, 1 + 2 * CHOS_EC_MAX_WIDTH> bytes;
    bytes[0] = 4;
    std::ranges::copy(x, bytes.begin() + 1);
    std::ranges::copy(y, bytes.begin() + 1 + width);
    return PlatformECKeyContainer(chos_key_from_ec_point(bits, bytes.data(), 1 + 2 * width,
        scalar.empty() ? nullptr : scalar.data(), scalar.size(), chos_webcore_key_entropy, nullptr));
}

RefPtr<CryptoKeyEC> CryptoKeyEC::platformImportJWKPublic(CryptoAlgorithmIdentifier algorithm, NamedCurve curve,
    Vector<uint8_t>&& x, Vector<uint8_t>&& y, bool extractable, CryptoKeyUsageBitmap usages)
{
    auto key = keyFromFields(curveBits(curve), x.span(), y.span(), { });
    if (!key)
        return nullptr;
    return create(algorithm, curve, CryptoKeyType::Public, WTF::move(key), extractable, usages);
}

RefPtr<CryptoKeyEC> CryptoKeyEC::platformImportJWKPrivate(CryptoAlgorithmIdentifier algorithm, NamedCurve curve,
    Vector<uint8_t>&& x, Vector<uint8_t>&& y, Vector<uint8_t>&& d, bool extractable, CryptoKeyUsageBitmap usages)
{
    if (d.isEmpty())
        return nullptr;
    auto key = keyFromFields(curveBits(curve), x.span(), y.span(), d.span());
    chos_key_clear_secret(d.mutableSpan().data(), d.size());
    if (!key)
        return nullptr;
    return create(algorithm, curve, CryptoKeyType::Private, WTF::move(key), extractable, usages);
}

Vector<uint8_t> CryptoKeyEC::platformExportRaw() const
{
    std::array<uint8_t, 1 + 2 * CHOS_EC_MAX_WIDTH> point;
    size_t width;
    if (chos_key_ec_fields(platformKey().get(), point.data(), nullptr, &width))
        return { };
    return Vector<uint8_t> { std::span(point).first(1 + 2 * width) };
}

bool CryptoKeyEC::platformAddFieldElements(JsonWebKey& jwk) const
{
    std::array<uint8_t, 1 + 2 * CHOS_EC_MAX_WIDTH> point;
    std::array<uint8_t, CHOS_EC_MAX_WIDTH> scalar;
    size_t width;
    bool isPrivate = type() == CryptoKeyType::Private;
    if (chos_key_ec_fields(platformKey().get(), point.data(), isPrivate ? scalar.data() : nullptr, &width))
        return false;
    jwk.x = base64URLEncodeToString(std::span(point).subspan(1, width));
    jwk.y = base64URLEncodeToString(std::span(point).subspan(1 + width, width));
    if (isPrivate) {
        jwk.d = base64URLEncodeToString(std::span(scalar).first(width));
        chos_key_clear_secret(scalar.data(), scalar.size());
    }
    return true;
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

CryptoKeyRSA::CryptoKeyRSA(CryptoAlgorithmIdentifier identifier, CryptoAlgorithmIdentifier hash, bool hasHash,
    CryptoKeyType type, PlatformRSAKeyContainer&& key, bool extractable, CryptoKeyUsageBitmap usages)
    : CryptoKey(identifier, type, extractable, usages)
    , m_platformKey(WTF::move(key))
    , m_restrictedToSpecificHash(hasHash)
    , m_hash(hash)
{
}

bool CryptoKeyRSA::isRestrictedToHash(CryptoAlgorithmIdentifier& hash) const
{
    if (m_restrictedToSpecificHash)
        hash = m_hash;
    return m_restrictedToSpecificHash;
}

static Vector<uint8_t> rsaNumber(const chos_webcore_key* key, chos_rsa_field field)
{
    std::array<uint8_t, CHOS_RSA_MAX_WIDTH> scratch;
    size_t size = 0;
    Vector<uint8_t> value;
    if (!chos_key_rsa_field(key, field, scratch.data(), &size))
        value = Vector<uint8_t> { std::span(scratch).first(size) };
    chos_key_clear_secret(scratch.data(), scratch.size());
    return value;
}

auto CryptoKeyRSA::algorithm() const -> KeyAlgorithm
{
    auto& registry = CryptoAlgorithmRegistry::singleton();
    auto exponent = rsaNumber(platformKey(), CHOS_RSA_E);
    CryptoRsaKeyAlgorithm parameters { CryptoKeyAlgorithm { registry.name(algorithmIdentifier()) },
        static_cast<unsigned>(keySizeInBits()), Uint8Array::create(exponent.span()) };
    if (m_restrictedToSpecificHash)
        return CryptoRsaHashedKeyAlgorithm { WTF::move(parameters), CryptoKeyAlgorithm { registry.name(m_hash) } };
    return parameters;
}

std::unique_ptr<CryptoKeyRSAComponents> CryptoKeyRSA::exportData() const
{
    auto n = rsaNumber(platformKey(), CHOS_RSA_N);
    auto e = rsaNumber(platformKey(), CHOS_RSA_E);
    if (n.isEmpty() || e.isEmpty())
        return nullptr;
    if (type() == CryptoKeyType::Public)
        return CryptoKeyRSAComponents::createPublic(WTF::move(n), WTF::move(e));
    auto d = rsaNumber(platformKey(), CHOS_RSA_D);
    CryptoKeyRSAComponents::PrimeInfo first { rsaNumber(platformKey(), CHOS_RSA_P), rsaNumber(platformKey(), CHOS_RSA_DP), { } };
    CryptoKeyRSAComponents::PrimeInfo second { rsaNumber(platformKey(), CHOS_RSA_Q), rsaNumber(platformKey(), CHOS_RSA_DQ), rsaNumber(platformKey(), CHOS_RSA_QI) };
    if (d.isEmpty() || first.primeFactor.isEmpty() || first.factorCRTExponent.isEmpty() || second.primeFactor.isEmpty()
        || second.factorCRTExponent.isEmpty() || second.factorCRTCoefficient.isEmpty())
        return nullptr;
    return CryptoKeyRSAComponents::createPrivateWithAdditionalData(WTF::move(n), WTF::move(e), WTF::move(d),
        WTF::move(first), WTF::move(second), { });
}

RefPtr<CryptoKeyRSA> CryptoKeyRSA::create(CryptoAlgorithmIdentifier algorithm, CryptoAlgorithmIdentifier hash,
    bool hasHash, const CryptoKeyRSAComponents& components, bool extractable, CryptoKeyUsageBitmap usages)
{
    bool isPrivate = components.type() == CryptoKeyRSAComponents::Type::Private;
    if (!components.otherPrimeInfos().isEmpty())
        return nullptr;
    auto number = [](const Vector<uint8_t>& value) -> chos_key_number { return { value.span().data(), value.size() }; };
    chos_rsa_components values { };
    values.n = number(components.modulus());
    values.e = number(components.exponent());
    if (isPrivate) {
        values.d = number(components.privateExponent());
        if (components.hasAdditionalPrivateKeyParameters()) {
            auto& first = components.firstPrimeInfo();
            auto& second = components.secondPrimeInfo();
            if (first.primeFactor.isEmpty() || second.primeFactor.isEmpty())
                return nullptr;
            values.p = number(first.primeFactor);
            values.q = number(second.primeFactor);
            values.dp = number(first.factorCRTExponent);
            values.dq = number(second.factorCRTExponent);
            values.qi = number(second.factorCRTCoefficient);
        }
    }
    PlatformRSAKeyContainer key(chos_key_from_rsa_numbers(&values, isPrivate));
    if (!key)
        return nullptr;
    return create(algorithm, hash, hasHash, isPrivate ? CryptoKeyType::Private : CryptoKeyType::Public,
        WTF::move(key), extractable, usages);
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
