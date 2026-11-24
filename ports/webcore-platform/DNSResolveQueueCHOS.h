#pragma once

#include "DNSResolveQueue.h"
#include <wtf/CompletionHandler.h>
#include <wtf/MonotonicTime.h>
#include <wtf/text/CString.h>

namespace WebCore {

class DNSResolveQueueCHOS final : public DNSResolveQueue {
public:
    DNSResolveQueueCHOS();

private:
    enum class Phase { Start, Poll, Done };
    struct Request {
        CString host;
        std::optional<uint64_t> identifier;
        DNSCompletionHandler completion;
        MonotonicTime deadline;
        Phase ipv4 { Phase::Start };
        Phase ipv6 { Phase::Start };
        std::optional<uint32_t> queryID;
        Vector<IPAddress> addresses;
    };
    void resolve(const String&, uint64_t, DNSCompletionHandler&&) final;
    void stopResolve(uint64_t) final;
    void platformResolve(const String&) final;
    void updateIsUsingProxy() final;
    void enqueue(const String&, std::optional<uint64_t>, DNSCompletionHandler&&);
    void poll();
    static void pollIPv4(Request&);
    static void pollIPv6(Request&);
    Vector<Request> m_queue;
    Timer m_pollTimer;
};

using DNSResolveQueuePlatform = DNSResolveQueueCHOS;

} // namespace WebCore
