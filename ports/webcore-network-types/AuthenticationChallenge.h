#pragma once

#include <WebCore/AuthenticationChallengeBase.h>
#include <WebCore/AuthenticationClient.h>
#include <wtf/RefPtr.h>

namespace WebCore {

class AuthenticationChallenge final : public AuthenticationChallengeBase {
public:
    AuthenticationChallenge() = default;

    AuthenticationChallenge(const ProtectionSpace& space, const Credential& credential,
        unsigned failures, const ResourceResponse& response, const ResourceError& error,
        AuthenticationClient& client)
        : AuthenticationChallengeBase(space, credential, failures, response, error)
        , m_client(&client)
    {
    }

    AuthenticationClient* authenticationClient() const { return m_client.get(); }

    static bool platformCompare(const AuthenticationChallenge& a, const AuthenticationChallenge& b)
    {
        return a.m_client == b.m_client;
    }

private:
    RefPtr<AuthenticationClient> m_client;
};

} // namespace WebCore
