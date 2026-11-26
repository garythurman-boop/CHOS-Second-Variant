#include "config.h"
#include "CommonAtomStrings.h"
#include "Cookie.h"
#include "CookieJar.h"
#include "NetworkStorageSession.h"
#include "SameSiteInfo.h"
#include "ShouldRelaxThirdPartyCookieBlocking.h"
#include <chos/libc.h>
#include <cstdio>
#include <wtf/MainThread.h>

using namespace WebCore;

#define CHECK(test) do { if (!(test)) { std::printf("[FAIL] cookie line %u: %s\n", __LINE__, #test); return false; } } while (0)

static bool exercise()
{
    NetworkStorageSession session(PAL::SessionID::defaultSessionID(), nullptr);
    NetworkStorageSession other(PAL::SessionID::defaultSessionID(), nullptr);
    URL page { "https://app.example.com/a/page"_s };
    URL sub { "https://sub.app.example.com/a/page"_s };
    URL http { "http://app.example.com/a/page"_s };
    SameSiteInfo same { true, false, true };
    SameSiteInfo cross { false, false, false };
    SameSiteInfo top { false, true, true };
    auto policy = ApplyTrackingPrevention::Yes;
    auto relax = ShouldRelaxThirdPartyCookieBlocking::No;
    auto tracker = IsKnownCrossSiteTracker::No;
    auto put = [&](const URL& url, const String& value) {
        session.setCookiesFromDOM(url, same, url, { }, { }, policy, RequiresScriptTrackingPrivacy::No, value, relax, tracker);
    };
    auto read = [&](auto& store, const URL& url, SameSiteInfo site) {
        return store.cookiesForDOM(url, site, url, { }, { }, IncludeSecureCookies::Yes, policy, relax, tracker);
    };
    auto request = [&](const URL& url, SameSiteInfo site) {
        return session.cookieRequestHeaderFieldValue(url, site, url, { }, { }, IncludeSecureCookies::Yes, policy, relax, tracker);
    };
    CHECK(!NetworkStorageSession::processMayUseCookieAPI());
    put(page, "forbidden=1"_s);
    NetworkStorageSession::permitProcessToUseCookieAPI(true);
    CHECK(read(session, page, same).first.isEmpty());
    put(page, "site=1; Domain=.example.com; Path=/; SameSite=Strict"_s);
    put(page, "local=1"_s);
    put(page, "shared=1; Domain=app.example.com; Path=/; Secure; SameSite=None"_s);
    CHECK(read(session, page, same).first == "local=1; site=1; shared=1"_s);
    CHECK(read(session, sub, same).first == "site=1; shared=1"_s);
    CHECK(read(session, http, same).first == "local=1; site=1"_s);
    CHECK(read(session, page, cross).first == "shared=1"_s);
    CHECK(read(session, page, top).first == "local=1; shared=1"_s);
    CHECK(read(session, page, same).second && !read(session, http, same).second);
    CHECK(read(other, page, same).first.isEmpty());
    put(page, "bad=1; Domain=.com"_s);
    put(page, "bad=2; Domain=badexample.com"_s);
    put(page, "__Host-key=1; Secure"_s);
    put(page, "__Http-only=1; Secure"_s);
    put(page, "bad=3; Partitioned; Secure"_s);
    put(page, "bad=4; SameSite=None"_s);
    put(http, "shared=overwrite; Path=/"_s);
    put(page, "bad=5; HttpOnly"_s);
    CHECK(read(session, page, same).first == "local=1; site=1; shared=1"_s);
    put(page, "__Host-key=1; Secure; Path=/"_s);
    put(page, "local=gone; Max-Age=0"_s);
    CHECK(read(session, page, same).first == "site=1; shared=1; __Host-key=1"_s);
    Cookie hidden;
    hidden.name = "login"_s;
    hidden.value = "server"_s;
    hidden.domain = page.host().toString();
    hidden.path = "/"_s;
    hidden.httpOnly = hidden.secure = true;
    hidden.sameSite = Cookie::SameSitePolicy::Strict;
    session.setCookie(hidden);
    put(page, "login=script; Path=/"_s);
    put(page, "login=; Path=/; Max-Age=0"_s);
    bool deleted = false;
    session.deleteCookie(page, page, hidden.name, [&] { deleted = true; });
    CHECK(deleted && !read(session, page, same).first.contains("login"_s));
    CHECK(request(page, same).first.endsWith("login=server"_s));
    Vector<Cookie> raw;
    CHECK(session.getRawCookies(page, same, page, { }, { }, policy, relax, raw) && raw.size() == 4);
    NetworkStorageSession::permitProcessToUseCookieAPI(false);
    CHECK(!session.getRawCookies(page, same, page, { }, { }, policy, relax, raw) && raw.isEmpty());
    CHECK(request(page, same).first.isEmpty());
    return true;
}

int main()
{
    WTF::initializeMainThread();
    initializeCommonAtomStrings();
    for (unsigned round = 0; round < 3; ++round) {
        if (!exercise())
            return 1;
        chos_heap_stats_t stats;
        if (chos_libc_heap_info(&stats))
            return 1;
        std::printf("[VALUES HEAP] round=%u allocations=%zu mapped=%zu\n", round, stats.allocations, stats.mapped_bytes);
    }
    std::puts("[ OK ] WebCore session cookie scope, policy, privileges and isolation passed");
    return 0;
}
