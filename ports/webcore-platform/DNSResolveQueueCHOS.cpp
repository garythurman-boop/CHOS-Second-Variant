#include "config.h"
#include "DNSResolveQueueCHOS.h"
#include <arpa/inet.h>
#include <chos/user.h>
#include <cstring>
#include <wtf/MainThread.h>

namespace WebCore {

DNSResolveQueueCHOS::DNSResolveQueueCHOS()
    : m_pollTimer(*this, &DNSResolveQueueCHOS::poll)
{
}

void DNSResolveQueueCHOS::updateIsUsingProxy()
{
    // The native network ABI sends DNS directly to the configured server.
    m_isUsingProxy = false;
}

void DNSResolveQueueCHOS::resolve(const String& host, uint64_t identifier, DNSCompletionHandler&& completion)
{
    enqueue(host, identifier, WTF::move(completion));
}

void DNSResolveQueueCHOS::platformResolve(const String& host)
{
    enqueue(host, std::nullopt, [this](DNSAddressesOrError&&) { decrementRequestCount(); });
}

void DNSResolveQueueCHOS::enqueue(const String& host, std::optional<uint64_t> identifier, DNSCompletionHandler&& completion)
{
    ASSERT(isMainThread());
    if (host.isEmpty() || host.length() >= CHOS_NET_DNS_API_HOST_MAX || !host.containsOnlyASCII()
        || host.contains(static_cast<char16_t>(0)) || m_queue.size() >= 64)
        return completion(makeUnexpected(DNSError::CannotResolve));
    if (identifier) {
        for (const auto& request : m_queue) {
            if (request.identifier == identifier)
                return completion(makeUnexpected(DNSError::CannotResolve));
        }
    }
    Request request { host.utf8(), identifier, WTF::move(completion), MonotonicTime::now() + 15_s,
        Phase::Start, Phase::Start, std::nullopt, {} };
    if (auto literal = IPAddress::fromString(host)) {
        request.addresses.append(*literal);
        request.ipv4 = request.ipv6 = Phase::Done;
    } else if (request.host.length() && request.host.data()[request.host.length() - 1] == '.') {
        request.host = CString(request.host.span().first(request.host.length() - 1));
    }
    m_queue.append(WTF::move(request));
    if (!m_pollTimer.isActive())
        m_pollTimer.startOneShot(0_s);
}

void DNSResolveQueueCHOS::stopResolve(uint64_t identifier)
{
    ASSERT(isMainThread());
    for (size_t i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].identifier != identifier)
            continue;
        auto completion = WTF::move(m_queue[i].completion);
        m_queue.removeAt(i);
        if (m_queue.isEmpty())
            m_pollTimer.stop();
        // An in-flight kernel query may finish. Only a live request can deliver it.
        return completion(makeUnexpected(DNSError::Cancelled));
    }
}

void DNSResolveQueueCHOS::pollIPv4(Request& request)
{
    if (request.ipv4 == Phase::Done)
        return;
    if (request.ipv4 == Phase::Start) {
        auto rc = chos_net_dns_lookup(request.host.data());
        if (rc == -CHOS_ERRNO_EBUSY)
            return;
        if (rc) {
            request.ipv4 = Phase::Done;
            return;
        }
        request.ipv4 = Phase::Poll;
    }
    chos_dns_api_info_t status;
    if (!chos_net_dns_status(&status) && !std::strcmp(status.hostname, request.host.data())
        && (!request.queryID || *request.queryID == status.query_id)) {
        request.queryID = status.query_id;
        if (status.state == CHOS_DNS_API_PENDING)
            return;
        if (status.state == CHOS_DNS_API_RESOLVED)
            request.addresses.append(IPAddress(in_addr { htonl(status.ipv4_address) }));
    }
    request.ipv4 = Phase::Done;
}

void DNSResolveQueueCHOS::pollIPv6(Request& request)
{
    if (request.ipv6 == Phase::Done)
        return;
    if (request.ipv6 == Phase::Start) {
        auto rc = chos_net_dns6_lookup(request.host.data());
        if (rc == -CHOS_ERRNO_EBUSY)
            return;
        if (rc) {
            request.ipv6 = Phase::Done;
            return;
        }
        request.ipv6 = Phase::Poll;
    }
    chos_dns6_info_t status;
    if (!chos_net_dns6_status(&status) && status.version == CHOS_DNS6_VERSION
        && status.size_bytes == sizeof(status) && !std::strcmp(status.hostname, request.host.data())) {
        if (status.state == CHOS_DNS6_PENDING)
            return;
        if (status.state == CHOS_DNS6_RESOLVED) {
            in6_addr address;
            std::memcpy(address.s6_addr, status.address, sizeof(address.s6_addr));
            request.addresses.append(IPAddress(address));
        }
    }
    request.ipv6 = Phase::Done;
}

void DNSResolveQueueCHOS::poll()
{
    ASSERT(isMainThread());
    if (m_queue.isEmpty())
        return;
    auto& request = m_queue[0];
    if (MonotonicTime::now() < request.deadline) {
        pollIPv4(request);
        pollIPv6(request);
        if (request.ipv4 != Phase::Done || request.ipv6 != Phase::Done) {
            m_pollTimer.startOneShot(20_ms);
            return;
        }
    }
    auto completion = WTF::move(request.completion);
    auto addresses = WTF::move(request.addresses);
    m_queue.removeAt(0);
    if (!m_queue.isEmpty())
        m_pollTimer.startOneShot(0_s);
    // Callbacks may synchronously enqueue or cancel other requests.
    if (addresses.isEmpty())
        return completion(makeUnexpected(DNSError::CannotResolve));
    completion(WTF::move(addresses));
}

} // namespace WebCore
