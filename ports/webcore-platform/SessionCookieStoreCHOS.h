#pragma once
#include "Cookie.h"
#include "SameSiteInfo.h"
#include <wtf/Seconds.h>
#include <wtf/Lock.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

class SessionCookieStoreCHOS {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(SessionCookieStoreCHOS);
public:
    void setFromDOM(const URL&, const SameSiteInfo&, const String&, std::optional<Seconds> lifetimeCap);
    void setTrusted(Cookie);
    Vector<Cookie> matching(const URL&, const SameSiteInfo&, bool includeHTTPOnly, bool includeSecure);
    void eraseVisible(const URL&, const String& name);
private:
    void store(Cookie&&, bool fromDOM, bool secureOrigin);
    void prune(double now);
    Lock m_lock;
    Vector<Cookie> m_cookies;
};

} // namespace WebCore
