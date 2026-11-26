#include "config.h"
#include "DataURLDecoder.h"
#include "NetworkLoadMetrics.h"
#include "ResourceError.h"
#include "ResourceHandleInternal.h"
#include "ResourceResponse.h"
#include "SecurityOrigin.h"
#include "SharedBuffer.h"
#include <wtf/RunLoop.h>

namespace WebCore {

ResourceHandleInternal::~ResourceHandleInternal() = default;
ResourceHandle::~ResourceHandle() = default;

static void loadData(const ResourceRequest& request, ResourceError& error, ResourceResponse& response, Vector<uint8_t>& bytes)
{
    response = { };
    bytes.clear();
    const URL& url = request.url();
    // Decode only bounded, self-contained inputs. No ambient filesystem or
    // network authority is involved in this local transport.
    if (!url.protocolIsData() || (request.httpMethod() != "GET"_s && request.httpMethod() != "HEAD"_s)) {
        error = { "CHOSLocalResource"_s, 1, url, "Unsupported local resource or method"_s };
        return;
    }
    if (url.string().length() > 1024 * 1024) {
        error = { "CHOSLocalResource"_s, 2, url, "Data URL exceeds the local decoder limit"_s };
        return;
    }
    auto decoded = DataURLDecoder::decode(url);
    if (!decoded) {
        error = { "CHOSLocalResource"_s, 3, url, "Invalid data URL"_s };
        return;
    }
    error = { };
    response = ResourceResponse { URL { url }, WTF::move(decoded->mimeType), static_cast<long long>(decoded->data.size()), WTF::move(decoded->charset) };
    response.setHTTPStatusCode(200);
    if (request.httpMethod() == "GET"_s)
        bytes = WTF::move(decoded->data);
}

void ResourceHandle::platformLoadResourceSynchronously(NetworkingContext*, const ResourceRequest& request, StoredCredentialsPolicy,
    SecurityOrigin*, ResourceError& error, ResourceResponse& response, Vector<uint8_t>& bytes)
{
    loadData(request, error, response, bytes);
}

void ResourceHandle::platformSetDefersLoading(bool deferred)
{
    if (deferred || !d->m_localDelivery || d->m_localDeliveryQueued)
        return;
    d->m_localDeliveryQueued = true;
    RunLoop::mainSingleton().dispatch([this, protectedThis = Ref { *this }] {
        d->m_localDeliveryQueued = false;
        if (d->m_defersLoading || !d->m_localDelivery || !client())
            return;
        auto deliver = std::exchange(d->m_localDelivery, nullptr);
        deliver();
    });
}

bool ResourceHandle::start()
{
    d->m_localDelivery = [this] {
        ResourceError error;
        ResourceResponse response;
        Vector<uint8_t> bytes;
        loadData(firstRequest(), error, response, bytes);
        if (!error.isNull()) {
            client()->didFail(this, error);
            return;
        }
        didReceiveResponse(WTF::move(response), [this, protectedThis = Ref { *this }, bytes = WTF::move(bytes)]() mutable {
            if (d->m_localCancelled || !client())
                return;
            d->m_localDelivery = [this, bytes = WTF::move(bytes)]() mutable {
                d->m_localDelivery = [this] {
                    NetworkLoadMetrics metrics;
                    client()->didFinishLoading(this, metrics);
                };
                if (!bytes.isEmpty()) {
                    Ref buffer = SharedBuffer::create(WTF::move(bytes));
                    client()->didReceiveData(this, buffer, static_cast<int>(buffer->size()));
                }
                platformSetDefersLoading(d->m_defersLoading);
            };
            platformSetDefersLoading(d->m_defersLoading);
        });
    };
    platformSetDefersLoading(d->m_defersLoading);
    return true;
}

void ResourceHandle::cancel()
{
    d->m_localCancelled = true;
    d->m_localDelivery = nullptr;
    d->m_failureTimer.stop();
    d->m_scheduledFailureType = NoFailure;
    clearClient();
}

// This transport never requests credentials. Refuse an unexpected challenge.
bool ResourceHandle::shouldUseCredentialStorage() { return false; }
void ResourceHandle::didReceiveAuthenticationChallenge(const AuthenticationChallenge&) { cancel(); }
void ResourceHandle::receivedCredential(const AuthenticationChallenge&, const Credential&) { cancel(); }
void ResourceHandle::receivedRequestToContinueWithoutCredential(const AuthenticationChallenge&) { cancel(); }
void ResourceHandle::receivedCancellation(const AuthenticationChallenge&) { cancel(); }
void ResourceHandle::receivedRequestToPerformDefaultHandling(const AuthenticationChallenge&) { cancel(); }
void ResourceHandle::receivedChallengeRejection(const AuthenticationChallenge&) { cancel(); }

} // namespace WebCore
