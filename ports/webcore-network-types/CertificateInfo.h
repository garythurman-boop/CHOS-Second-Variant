/* CHOS WebCore certificate metadata, 2026-11-16.
 * Owns exact DER bytes. Parsing a summary does not authenticate a certificate. */
#pragma once
#include <WebCore/CertificateSummary.h>
#include <optional>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/Vector.h>

namespace WebCore {

class CertificateInfo {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(CertificateInfo);
public:
    using Certificate = Vector<uint8_t>;
    using CertificateChain = Vector<Certificate>;

    CertificateInfo() = default;
    // Flags and an optional trust-anchor index are facts reported by a caller's
    // verifier. This value type never performs or invents trust validation.
    // Unknown anchor: examine every supplied certificate for weak signatures.
    WEBCORE_EXPORT CertificateInfo(int verificationFlags, CertificateChain&&,
        std::optional<size_t> verifiedTrustAnchorIndex = std::nullopt);
    WEBCORE_EXPORT CertificateInfo isolatedCopy() const;
    bool verificationWasReported() const { return m_verificationWasReported; }
    int verificationError() const { return m_verificationError; }
    WEBCORE_EXPORT String verificationErrorDescription() const;
    const CertificateChain& certificateChain() const LIFETIME_BOUND { return m_certificateChain; }
    std::optional<size_t> verifiedTrustAnchorIndex() const { return m_verifiedTrustAnchorIndex; }
    bool isEmpty() const { return m_certificateChain.isEmpty(); }
    WEBCORE_EXPORT bool containsNonRootSHA1SignedCertificate() const;
    WEBCORE_EXPORT std::optional<CertificateSummary> summary() const;
    WEBCORE_EXPORT static Certificate makeCertificate(std::span<const uint8_t>);
    friend bool operator==(const CertificateInfo&, const CertificateInfo&) = default;

private:
    int m_verificationError { -1 };
    bool m_verificationWasReported { false };
    std::optional<size_t> m_verifiedTrustAnchorIndex;
    CertificateChain m_certificateChain;
};

} // namespace WebCore
