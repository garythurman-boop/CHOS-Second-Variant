// The persisted chain records a past observation. Network trust decisions
// still require the verifier for the current origin, time and trust store.
#include "config.h"
#include "CertificateBackend.h"
#include "CertificateInfo.h"
#include "WebCorePersistentCoders.h"
#include <wtf/persistence/PersistentDecoder.h>
#include <wtf/persistence/PersistentEncoder.h>

namespace WTF::Persistence {

static constexpr uint8_t formatVersion = 1;
static constexpr uint32_t noAnchor = UINT32_MAX;
static constexpr uint32_t maxChainLength = 32;

void Coder<WebCore::CertificateInfo>::encodeForPersistence(Encoder& encoder, const WebCore::CertificateInfo& info)
{
    auto& chain = info.certificateChain();
    if (chain.size() > maxChainLength || std::ranges::any_of(chain, [](auto& der) { return der.size() > CHOS_CERTIFICATE_MAX_DER_BYTES; })) {
        // A decoder rejects version zero, including when this record is nested.
        encoder << uint8_t { 0 };
        return;
    }
    encoder << formatVersion << static_cast<int32_t>(info.verificationError())
        << static_cast<uint32_t>(info.verifiedTrustAnchorIndex().value_or(noAnchor))
        << static_cast<uint32_t>(chain.size());
    for (auto& der : chain) {
        encoder << static_cast<uint32_t>(der.size());
        encoder.encodeFixedLengthData(der.span());
    }
}

std::optional<WebCore::CertificateInfo> Coder<WebCore::CertificateInfo>::decodeForPersistence(Decoder& decoder)
{
    std::optional<uint8_t> version;
    decoder >> version;
    if (version != formatVersion)
        return std::nullopt;
    std::optional<int32_t> flags;
    std::optional<uint32_t> anchor, count;
    decoder >> flags >> anchor >> count;
    if (!flags || *flags < -1 || !count || *count > maxChainLength || (!*count && *flags != -1)
        || !anchor || (*anchor != noAnchor && *anchor >= *count))
        return std::nullopt;
    WebCore::CertificateInfo::CertificateChain chain;
    for (uint32_t i = 0; i < *count; ++i) {
        std::optional<uint32_t> length;
        decoder >> length;
        if (!length || *length > CHOS_CERTIFICATE_MAX_DER_BYTES || !decoder.bufferIsLargeEnoughToContain<uint8_t>(*length))
            return std::nullopt;
        WebCore::CertificateInfo::Certificate bytes;
        if (!bytes.tryGrow(*length) || !decoder.decodeFixedLengthData(bytes.mutableSpan()) || !chain.tryAppend(WTF::move(bytes)))
            return std::nullopt;
    }
    return WebCore::CertificateInfo(*flags, WTF::move(chain), *anchor == noAnchor ? std::nullopt : std::optional<size_t>(*anchor));
}

} // namespace WTF::Persistence
