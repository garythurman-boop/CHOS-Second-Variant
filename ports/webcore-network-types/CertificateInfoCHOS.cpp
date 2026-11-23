/* CHOS WebCore certificate metadata, 2026-11-16. Maintained MbedTLS parses DER;
 * trust/hostname/time validation remains the transport verifier's responsibility. */
#include "config.h"
#include "CertificateInfo.h"
#include "CertificateBackend.h"
#include <memory>
#include <cstdio>
#include <wtf/HexNumber.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

CertificateInfo::CertificateInfo(int flags, CertificateChain&& chain, std::optional<size_t> anchor)
    : m_verificationError(flags >= 0 && !chain.isEmpty() ? flags : -1)
    , m_verificationWasReported(flags >= 0 && !chain.isEmpty())
    , m_verifiedTrustAnchorIndex(anchor && *anchor < chain.size() ? anchor : std::nullopt)
    , m_certificateChain(WTF::move(chain))
{
}

CertificateInfo CertificateInfo::isolatedCopy() const
{
    CertificateChain copy;
    for (const auto& certificate : m_certificateChain)
        copy.append(makeCertificate(certificate.span()));
    auto result = CertificateInfo(m_verificationError, WTF::move(copy), m_verifiedTrustAnchorIndex);
    result.m_verificationWasReported = m_verificationWasReported;
    return result;
}

CertificateInfo::Certificate CertificateInfo::makeCertificate(std::span<const uint8_t> bytes)
{
    return Certificate(bytes);
}

String CertificateInfo::verificationErrorDescription() const
{
    if (!m_verificationWasReported)
        return "Certificate verification has not been reported"_s;
    if (!m_verificationError)
        return "Caller reported successful certificate verification"_s;
    return makeString("Caller reported certificate verification flags 0x"_s,
        WTF::hex(static_cast<unsigned>(m_verificationError)));
}

bool CertificateInfo::containsNonRootSHA1SignedCertificate() const
{
    // The bool API cannot express unknown. Conservatively treat malformed,
    // unsupported and oversized chains as unsafe rather than returning false.
    if (m_certificateChain.size() > 32)
        return true;
    for (size_t i = 0; i < m_certificateChain.size(); ++i) {
        const auto& der = m_certificateChain[i];
        std::unique_ptr<chos_certificate, decltype(&chos_certificate_destroy)> parsed(
            chos_certificate_parse(der.span().data(), der.size()), chos_certificate_destroy);
        if (!parsed)
            return true;
        if (m_verificationWasReported && !m_verificationError && m_verifiedTrustAnchorIndex == i)
            continue;
        if (chos_certificate_signature_is_sha1(parsed.get()))
            return true;
    }
    return false;
}

static std::optional<String> subjectString(const unsigned char* bytes, size_t length, int tag)
{
    if (!length)
        return String { ""_s };
    if (!bytes)
        return std::nullopt;
    // ASN.1 UTF8String, BMPString, or a single-byte name representation.
    if (tag == 12) {
        auto result = String::fromUTF8(std::span(bytes, length));
        if (result.isNull()) return std::nullopt;
        return result;
    }
    if (tag == 30) {
        if (length % 2) return std::nullopt;
        Vector<UChar> text;
        for (size_t i = 0; i < length; i += 2) {
            UChar unit = (bytes[i] << 8) | bytes[i + 1];
            // BMPString cannot contain UTF-16 surrogates.
            if (unit >= 0xd800 && unit <= 0xdfff) return std::nullopt;
            text.append(unit);
        }
        return String { text.span() };
    }
    if (tag != 19 && tag != 20 && tag != 22)
        return std::nullopt;
    // PrintableString/IA5String are ASCII. T.61 has stateful/non-Latin1
    // characters outside ASCII: do not mislabel those bytes as Latin1.
    for (size_t i = 0; i < length; ++i)
        if (bytes[i] >= 0x80)
            return std::nullopt;
    return String { std::span(bytes, length) };
}

std::optional<CertificateSummary> CertificateInfo::summary() const
{
    if (m_certificateChain.isEmpty())
        return std::nullopt;
    const auto& der = m_certificateChain[0];
    std::unique_ptr<chos_certificate, decltype(&chos_certificate_destroy)> parsed(
        chos_certificate_parse(der.span().data(), der.size()), chos_certificate_destroy);
    if (!parsed)
        return std::nullopt;
    size_t length = 0; int tag = 0;
    auto* bytes = chos_certificate_subject(parsed.get(), &length, &tag);
    auto subject = subjectString(bytes, length, tag);
    if (!subject)
        return std::nullopt;
    CertificateSummary result;
    result.subject = WTF::move(*subject);
    result.validFrom = Seconds { static_cast<double>(chos_certificate_valid_from(parsed.get())) };
    result.validUntil = Seconds { static_cast<double>(chos_certificate_valid_until(parsed.get())) };
    auto count = chos_certificate_san_count(parsed.get());
    for (size_t i = 0; i < count; ++i) {
        int type = 0;
        bytes = chos_certificate_san(parsed.get(), i, &length, &type);
        if (!bytes)
            return std::nullopt;
        if (type == 2) {
            for (size_t n = 0; n < length; ++n)
                if (bytes[n] <= 0x20 || bytes[n] >= 0x7f)
                    return std::nullopt;
            result.dnsNames.append(String { std::span(bytes, length) });
        } else {
            char text[80]; int written;
            if (length == 4)
                written = std::snprintf(text, sizeof(text), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
            else if (length == 16)
                written = std::snprintf(text, sizeof(text), "%x:%x:%x:%x:%x:%x:%x:%x",
                    (bytes[0] << 8) | bytes[1], (bytes[2] << 8) | bytes[3],
                    (bytes[4] << 8) | bytes[5], (bytes[6] << 8) | bytes[7],
                    (bytes[8] << 8) | bytes[9], (bytes[10] << 8) | bytes[11],
                    (bytes[12] << 8) | bytes[13], (bytes[14] << 8) | bytes[15]);
            else
                return std::nullopt;
            if (written < 0 || static_cast<size_t>(written) >= sizeof(text))
                return std::nullopt;
            result.ipAddresses.append(String::fromLatin1(text));
        }
    }
    return result;
}

} // namespace WebCore
