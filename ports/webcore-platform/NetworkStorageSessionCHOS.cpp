#include "config.h"
#include "CookieJar.h"
#include "NetworkStorageSession.h"
#include "NetworkingContext.h"
#include "SessionCookieStoreCHOS.h"
#include <wtf/text/StringBuilder.h>

namespace WebCore {

NetworkStorageSession::NetworkStorageSession(PAL::SessionID id, NetworkingContext* context)
    : m_sessionID(id)
    , m_context(context)
    , m_cookieStore(makeUnique<SessionCookieStoreCHOS>())
{
}

NetworkStorageSession::~NetworkStorageSession()
{
    clearCookiesVersionChangeCallbacks();
}

NetworkingContext* NetworkStorageSession::context() const
{
    return m_context.get();
}

static bool allowed(const NetworkStorageSession& session, const URL& firstParty, const URL& url, std::optional<FrameIdentifier> frame,
    std::optional<PageIdentifier> page, ApplyTrackingPrevention apply, ShouldRelaxThirdPartyCookieBlocking relax, IsKnownCrossSiteTracker tracker)
{
    return NetworkStorageSession::processMayUseCookieAPI() && url.protocolIsInHTTPFamily()
        && (apply == ApplyTrackingPrevention::No || !session.shouldBlockCookies(firstParty, url, frame, page, relax, tracker));
}

static std::pair<String, bool> header(const Vector<Cookie>& cookies)
{
    StringBuilder out;
    bool secure = false;
    for (auto& cookie : cookies) {
        if (!out.isEmpty())
            out.append("; "_s);
        out.append(cookie.name, '=', cookie.value);
        secure |= cookie.secure;
    }
    return { out.toString(), secure };
}

void NetworkStorageSession::setCookiesFromDOM(const URL& firstParty, const SameSiteInfo& site, const URL& url,
    std::optional<FrameIdentifier> frame, std::optional<PageIdentifier> page, ApplyTrackingPrevention apply,
    RequiresScriptTrackingPrivacy privacy, const String& value, ShouldRelaxThirdPartyCookieBlocking relax, IsKnownCrossSiteTracker tracker) const
{
    if (!allowed(*this, firstParty, url, frame, page, apply, relax, tracker))
        return;
    m_cookieStore->setFromDOM(url, site, value, clientSideCookieCap(RegistrableDomain { firstParty }, privacy, page));
}

std::pair<String, bool> NetworkStorageSession::cookiesForDOM(const URL& firstParty, const SameSiteInfo& site, const URL& url,
    std::optional<FrameIdentifier> frame, std::optional<PageIdentifier> page, IncludeSecureCookies secure,
    ApplyTrackingPrevention apply, ShouldRelaxThirdPartyCookieBlocking relax, IsKnownCrossSiteTracker tracker) const
{
    if (!allowed(*this, firstParty, url, frame, page, apply, relax, tracker))
        return { };
    return header(m_cookieStore->matching(url, site, false, secure == IncludeSecureCookies::Yes));
}

std::pair<String, bool> NetworkStorageSession::cookieRequestHeaderFieldValue(const URL& firstParty, const SameSiteInfo& site, const URL& url,
    std::optional<FrameIdentifier> frame, std::optional<PageIdentifier> page, IncludeSecureCookies secure,
    ApplyTrackingPrevention apply, ShouldRelaxThirdPartyCookieBlocking relax, IsKnownCrossSiteTracker tracker) const
{
    if (!allowed(*this, firstParty, url, frame, page, apply, relax, tracker))
        return { };
    return header(m_cookieStore->matching(url, site, true, secure == IncludeSecureCookies::Yes));
}

bool NetworkStorageSession::getRawCookies(const URL& firstParty, const SameSiteInfo& site, const URL& url,
    std::optional<FrameIdentifier> frame, std::optional<PageIdentifier> page, ApplyTrackingPrevention apply,
    ShouldRelaxThirdPartyCookieBlocking relax, Vector<Cookie>& result) const
{
    result.clear();
    if (!allowed(*this, firstParty, url, frame, page, apply, relax, IsKnownCrossSiteTracker::No))
        return false;
    result = m_cookieStore->matching(url, site, true, true);
    return true;
}

void NetworkStorageSession::setCookie(const Cookie& cookie)
{
    if (processMayUseCookieAPI())
        m_cookieStore->setTrusted(cookie);
}

void NetworkStorageSession::deleteCookie(const URL& firstParty, const URL& url, const String& name, CompletionHandler<void()>&& completion) const
{
    if (allowed(*this, firstParty, url, std::nullopt, std::nullopt, ApplyTrackingPrevention::Yes,
        ShouldRelaxThirdPartyCookieBlocking::No, IsKnownCrossSiteTracker::No))
        m_cookieStore->eraseVisible(url, name);
    completion();
}

} // namespace WebCore
