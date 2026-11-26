#include "config.h"
#include "SessionCookieStoreCHOS.h"
#include "HTTPParsers.h"
#include "PublicSuffixStore.h"
#include <wtf/WallTime.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringToIntegerConversion.h>

namespace WebCore {

static StringView domainName(StringView domain)
{
    return domain.startsWith('.') ? domain.substring(1) : domain;
}

static bool matchesHost(StringView host, StringView stored)
{
    auto domain = domainName(stored);
    return host == domain || (stored.startsWith('.') && host.endsWith(stored) && !URL::hostIsIPAddress(host));
}

// RFC 6265bis protects an existing Secure cookie even when one scope is
// host-only and the other host lies below it.
static bool overlapsDomain(StringView a, StringView b)
{
    a = domainName(a);
    b = domainName(b);
    auto below = [](StringView host, StringView parent) {
        return host.length() > parent.length() && host.endsWith(parent)
            && host[host.length() - parent.length() - 1] == '.' && !URL::hostIsIPAddress(host);
    };
    return a == b || below(a, b) || below(b, a);
}

static bool matchesPath(StringView request, StringView scope)
{
    return request == scope || (request.startsWith(scope) && (scope.endsWith('/') || request[scope.length()] == '/'));
}

static bool hasControl(StringView value)
{
    for (auto ch : value.codeUnits()) {
        if (ch < 0x20 || ch == 0x7f)
            return true;
    }
    return false;
}

static bool validCookieValue(StringView value)
{
    if (value.length() >= 2 && value.startsWith('"') && value.endsWith('"'))
        value = value.substring(1, value.length() - 2);
    for (auto ch : value.codeUnits()) {
        if (ch <= 0x20 || ch >= 0x7f || ch == '"' || ch == ',' || ch == ';' || ch == '\\')
            return false;
    }
    return true;
}

static double nowMS()
{
    return WallTime::now().secondsSinceEpoch().milliseconds();
}

void SessionCookieStoreCHOS::prune(double now)
{
    m_cookies.removeAllMatching([&](auto& cookie) { return cookie.expires && *cookie.expires <= now; });
}

void SessionCookieStoreCHOS::setFromDOM(const URL& url, const SameSiteInfo& site, const String& line, std::optional<Seconds> lifetimeCap)
{
    if (!url.protocolIsInHTTPFamily() || url.host().isEmpty() || line.length() > 4096 || hasControl(line))
        return;
    bool first = true;
    bool explicitRoot = false;
    bool domainAttribute = false;
    std::optional<double> maxAge;
    Cookie cookie;
    cookie.sameSite = Cookie::SameSitePolicy::Lax;
    cookie.domain = url.host().toString();
    cookie.path = CookieUtil::defaultPathForURL(url);
    for (auto item : StringView(line).splitAllowingEmptyEntries(';')) {
        auto at = item.find('=');
        auto name = item.left(at == notFound ? item.length() : at).trim(isASCIIWhitespace).toString();
        auto value = at == notFound ? emptyString() : item.substring(at + 1).trim(isASCIIWhitespace).toString();
        if (first) {
            if (at == notFound || !isValidHTTPToken(name) || !validCookieValue(value))
                return;
            cookie.name = WTF::move(name);
            cookie.value = WTF::move(value);
            first = false;
        } else if (equalLettersIgnoringASCIICase(name, "httponly"_s))
            return;
        else if (equalLettersIgnoringASCIICase(name, "secure"_s))
            cookie.secure = true;
        else if (equalLettersIgnoringASCIICase(name, "partitioned"_s))
            return; // This session store cannot isolate a partitioned cookie yet.
        else if (equalLettersIgnoringASCIICase(name, "domain"_s)) {
            domainAttribute = true;
            auto domain = domainName(value).convertToASCIILowercase();
            if (domain.isEmpty() || domain.startsWith('.') || domain.endsWith('.') || URL::hostIsIPAddress(url.host()))
                return;
            auto scope = makeString('.', domain);
            if (!matchesHost(url.host(), scope))
                return;
            if (PublicSuffixStore::singleton().isPublicSuffix(domain)) {
                if (domain != url.host())
                    return;
                cookie.domain = url.host().toString();
            } else
                cookie.domain = WTF::move(scope);
        } else if (equalLettersIgnoringASCIICase(name, "path"_s)) {
            explicitRoot = value == "/"_s;
            cookie.path = value.startsWith('/') ? value : CookieUtil::defaultPathForURL(url);
        } else if (equalLettersIgnoringASCIICase(name, "samesite"_s)) {
            if (equalLettersIgnoringASCIICase(value, "strict"_s))
                cookie.sameSite = Cookie::SameSitePolicy::Strict;
            else if (equalLettersIgnoringASCIICase(value, "none"_s))
                cookie.sameSite = Cookie::SameSitePolicy::None;
            else
                cookie.sameSite = Cookie::SameSitePolicy::Lax;
        } else if (equalLettersIgnoringASCIICase(name, "max-age"_s)) {
            if (auto seconds = parseInteger<int64_t>(value))
                maxAge = std::clamp<double>(*seconds, 0, 400 * 86400) * 1000;
        } else if (equalLettersIgnoringASCIICase(name, "expires"_s)) {
            if (auto time = parseHTTPDate(value))
                cookie.expires = time->secondsSinceEpoch().milliseconds();
        }
    }
    if (first || (cookie.secure && !url.protocolIs("https"_s)) || (cookie.sameSite == Cookie::SameSitePolicy::None && !cookie.secure)
        || (!site.isSameSite && cookie.sameSite != Cookie::SameSitePolicy::None))
        return;
    if (cookie.name.startsWithIgnoringASCIICase("__Host-"_s) && (domainAttribute || !explicitRoot))
        return;
    if (maxAge)
        cookie.expires = nowMS() + *maxAge;
    if (cookie.expires) {
        double cap = lifetimeCap ? std::min(400. * 86400000, lifetimeCap->milliseconds()) : 400. * 86400000;
        cookie.expires = std::min(*cookie.expires, nowMS() + cap);
    }
    store(WTF::move(cookie), true, url.protocolIs("https"_s));
}

void SessionCookieStoreCHOS::setTrusted(Cookie cookie)
{
    store(WTF::move(cookie), false, true);
}

void SessionCookieStoreCHOS::store(Cookie&& cookie, bool fromDOM, bool secureOrigin)
{
    if (!isValidHTTPToken(cookie.name) || !validCookieValue(cookie.value) || cookie.domain.isEmpty()
        || !cookie.partitionKey.isEmpty() || !cookie.path.startsWith('/') || hasControl(cookie.path)
        || hasControl(cookie.domain) || cookie.name.length() > 4096 || cookie.value.length() > 4096 - cookie.name.length()
        || cookie.domain.length() > 254 || cookie.path.length() > 4096
        || (cookie.expires && !std::isfinite(*cookie.expires)))
        return;
    cookie.domain = cookie.domain.convertToASCIILowercase();
    auto host = domainName(cookie.domain);
    if (host.isEmpty() || host.startsWith('.') || host.endsWith('.') || host.contains(".."_s))
        return;
    if (cookie.sameSite == Cookie::SameSitePolicy::None && !cookie.secure)
        return;
    if ((cookie.name.startsWithIgnoringASCIICase("__Http-"_s) || cookie.name.startsWithIgnoringASCIICase("__Host-Http-"_s))
        && (!cookie.secure || !cookie.httpOnly))
        return;
    if (cookie.domain.startsWith('.') && (PublicSuffixStore::singleton().isPublicSuffix(domainName(cookie.domain))
        || URL::hostIsIPAddress(domainName(cookie.domain))))
        return;
    if ((cookie.name.startsWithIgnoringASCIICase("__Secure-"_s) || cookie.name.startsWithIgnoringASCIICase("__Host-"_s)) && !cookie.secure)
        return;
    if (cookie.name.startsWithIgnoringASCIICase("__Host-"_s) && (cookie.domain.startsWith('.') || cookie.path != "/"_s))
        return;
    Locker locker { m_lock };
    double now = nowMS();
    if (cookie.expires)
        cookie.expires = std::min(*cookie.expires, now + 400. * 86400000);
    prune(now);
    size_t existing = notFound;
    for (size_t i = 0; i < m_cookies.size(); ++i) {
        auto& old = m_cookies[i];
        if (old.name != cookie.name)
            continue;
        if (!secureOrigin && old.secure && overlapsDomain(old.domain, cookie.domain)
            && matchesPath(cookie.path, old.path))
            return;
        if (domainName(old.domain) == domainName(cookie.domain) && old.path == cookie.path) {
            if (fromDOM && old.httpOnly)
                return;
            existing = i;
        }
    }
    cookie.session = !cookie.expires;
    if (existing != notFound) {
        cookie.created = m_cookies[existing].created;
        if (cookie.expires && *cookie.expires <= now)
            m_cookies.removeAt(existing);
        else
            m_cookies[existing] = WTF::move(cookie);
    } else if (!cookie.expires || *cookie.expires > now) {
        cookie.created = now;
        // Bound session memory. Prefer retiring the oldest member of a full
        // domain before applying the total browser-session limit.
        unsigned inDomain = 0;
        size_t oldest = notFound;
        for (size_t i = 0; i < m_cookies.size(); ++i) {
            if (domainName(m_cookies[i].domain) == domainName(cookie.domain)) {
                if (!inDomain++)
                    oldest = i;
            }
        }
        if (inDomain >= 180)
            m_cookies.removeAt(oldest);
        else if (m_cookies.size() >= 3000)
            m_cookies.removeAt(0);
        m_cookies.append(WTF::move(cookie));
    }
}

Vector<Cookie> SessionCookieStoreCHOS::matching(const URL& url, const SameSiteInfo& site, bool includeHTTPOnly, bool includeSecure)
{
    Locker locker { m_lock };
    prune(nowMS());
    Vector<Cookie> result;
    if (!url.protocolIsInHTTPFamily())
        return result;
    for (auto& cookie : m_cookies) {
        if (!matchesHost(url.host(), cookie.domain) || !matchesPath(url.path(), cookie.path)
            || (cookie.httpOnly && !includeHTTPOnly) || (cookie.secure && (!includeSecure || !url.protocolIs("https"_s))))
            continue;
        if (!site.isSameSite && (cookie.sameSite == Cookie::SameSitePolicy::Strict ||
            (cookie.sameSite == Cookie::SameSitePolicy::Lax && !(site.isTopSite && site.isSafeHTTPMethod))))
            continue;
        result.append(cookie);
    }
    std::stable_sort(result.begin(), result.end(), [](auto& a, auto& b) { return a.path.length() > b.path.length(); });
    return result;
}

void SessionCookieStoreCHOS::eraseVisible(const URL& url, const String& name)
{
    if (!url.protocolIsInHTTPFamily())
        return;
    Locker locker { m_lock };
    m_cookies.removeAllMatching([&](auto& cookie) {
        return cookie.name == name && !cookie.httpOnly && (!cookie.secure || url.protocolIs("https"_s))
            && matchesHost(url.host(), cookie.domain) && matchesPath(url.path(), cookie.path);
    });
}

} // namespace WebCore
