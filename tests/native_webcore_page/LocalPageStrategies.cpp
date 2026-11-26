// The embedded page has no network authority. Only data URLs can be scheduled.
#include "config.h"
#include "CachedResource.h"
#include "LoaderStrategy.h"
#include "LocalFrame.h"
#include "MediaStrategy.h"
#include "PlatformStrategies.h"
#include "ResourceError.h"
#include "ResourceHandle.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "StoredCredentialsPolicy.h"
#include "SubresourceLoader.h"
#include <wtf/NeverDestroyed.h>

namespace WebCore {

enum class LocalLoadError {
    Cancelled, Blocked, ContentBlocked, UnsupportedURL, PolicyChange, UnsupportedType,
    MissingFile, RedirectLoop, InsecureNavigation, Plugin
};

static ResourceError failure(LocalLoadError code, const URL& url, const String& message, ResourceError::Type type = ResourceError::Type::General)
{
    return { "CHOSLocalPage"_s, static_cast<int>(code), url, message, type };
}

class LocalLoader final : public LoaderStrategy {
    void loadResource(LocalFrame& frame, CachedResource& resource, ResourceRequest&& request, const ResourceLoaderOptions& options,
        CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& done) final
    {
        if (!request.url().protocolIsData() || request.url().string().length() > 1024 * 1024) {
            done(nullptr);
            return;
        }
        SubresourceLoader::create(frame, resource, WTF::move(request), options, [this, done = WTF::move(done)](RefPtr<SubresourceLoader>&& loader) mutable {
            if (loader) {
                m_pending.append(*loader);
                servePendingRequests(ResourceLoadPriority::VeryLow);
            }
            done(WTF::move(loader));
        });
    }

    void loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions&,
        const HTTPHeaderMap&, ResourceError& error, ResourceResponse& response, Vector<uint8_t>& bytes) final
    {
        ResourceHandle::loadResourceSynchronously(nullptr, request, StoredCredentialsPolicy::DoNotUse, nullptr, error, response, bytes);
    }

    void remove(ResourceLoader* loader) final
    {
        m_pending.removeAllMatching([loader](auto& item) { return item.ptr() == loader; });
    }
    void browsingContextRemoved(LocalFrame& frame) final
    {
        Vector<Ref<SubresourceLoader>> abandoned;
        m_pending.removeAllMatching([&](auto& item) {
            if (item->frame() != &frame)
                return false;
            abandoned.append(item.copyRef());
            return true;
        });
        for (auto& item : abandoned)
            item->cancel();
    }
    void pageLoadCompleted(Page&) final { servePendingRequests(ResourceLoadPriority::VeryLow); }
    void setDefersLoading(ResourceLoader& loader, bool deferred) final
    {
        if (!deferred && !loader.deferredRequest().isNull()) {
            loader.setRequest(loader.takeDeferredRequest());
            loader.start();
        }
    }
    void crossOriginRedirectReceived(ResourceLoader* loader, const URL&) final
    {
        if (loader)
            loader->cancel(blockedError(loader->request()));
    }
    void servePendingRequests(ResourceLoadPriority minimum) final
    {
        while (!m_suspensions) {
            auto index = m_pending.findIf([minimum](auto& loader) { return loader->request().priority() >= minimum; });
            if (index == notFound)
                return;
            auto loader = m_pending[index].copyRef();
            m_pending.removeAt(index);
            if (!loader->reachedTerminalState())
                loader->start();
        }
    }
    void suspendPendingRequests() final { ++m_suspensions; }
    void resumePendingRequests() final
    {
        RELEASE_ASSERT(m_suspensions);
        --m_suspensions;
        servePendingRequests(ResourceLoadPriority::VeryLow);
    }
    void startPingLoad(LocalFrame&, ResourceRequest& request, const HTTPHeaderMap&, const FetchOptions&,
        ContentSecurityPolicyImposition, PingLoadCompletionHandler&& done) final
    {
        if (done)
            done(blockedError(request), { });
    }
    void preconnectTo(FrameLoader&, ResourceRequest&& request, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& done) final
    {
        if (done)
            done(blockedError(request));
    }
    // A local-only page has no connection transitions or network timing data.
    void addOnlineStateChangeListener(Function<void(bool)>&&) final { }
    bool isOnLine() const final { return false; }
    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }
    void isResourceLoadFinished(CachedResource& resource, CompletionHandler<void(bool)>&& done) final { done(resource.isLoaded()); }

    ResourceError cancelledError(const ResourceRequest& r) const final { return failure(LocalLoadError::Cancelled, r.url(), "Load cancelled"_s, ResourceError::Type::Cancellation); }
    ResourceError blockedError(const ResourceRequest& r) const final { return failure(LocalLoadError::Blocked, r.url(), "Resource blocked by local page policy"_s, ResourceError::Type::AccessControl); }
    bool isBlockedError(const ResourceError& e) const final { return e.domain() == "CHOSLocalPage"_s && e.errorCode() == static_cast<int>(LocalLoadError::Blocked); }
    ResourceError blockedByContentBlockerError(const ResourceRequest& r) const final { return failure(LocalLoadError::ContentBlocked, r.url(), "Content blocked"_s); }
    ResourceError cannotShowURLError(const ResourceRequest& r) const final { return failure(LocalLoadError::UnsupportedURL, r.url(), "Unsupported URL"_s); }
    ResourceError interruptedForPolicyChangeError(const ResourceRequest& r) const final { return failure(LocalLoadError::PolicyChange, r.url(), "Navigation interrupted"_s); }
#if ENABLE(CONTENT_FILTERING)
    ResourceError blockedByContentFilterError(const ResourceRequest& r) const final { return blockedByContentBlockerError(r); }
#endif
    ResourceError cannotShowMIMETypeError(const ResourceResponse& r) const final { return failure(LocalLoadError::UnsupportedType, r.url(), "Unsupported MIME type"_s); }
    ResourceError fileDoesNotExistError(const ResourceResponse& r) const final { return failure(LocalLoadError::MissingFile, r.url(), "File does not exist"_s); }
    ResourceError httpsUpgradeRedirectLoopError(const ResourceRequest& r) const final { return failure(LocalLoadError::RedirectLoop, r.url(), "HTTPS upgrade redirect loop"_s); }
    ResourceError httpNavigationWithHTTPSOnlyError(const ResourceRequest& r) const final { return failure(LocalLoadError::InsecureNavigation, r.url(), "Insecure navigation blocked"_s); }
    bool isHttpNavigationWithHTTPSOnlyError(const ResourceError& e) const final { return e.domain() == "CHOSLocalPage"_s && e.errorCode() == static_cast<int>(LocalLoadError::InsecureNavigation); }
    ResourceError pluginWillHandleLoadError(const ResourceResponse& r) const final { return failure(LocalLoadError::Plugin, r.url(), "Plugin unavailable"_s); }
    unsigned m_suspensions { 0 };
    Vector<Ref<SubresourceLoader>> m_pending;
};

class LocalMedia final : public MediaStrategy { };
class LocalStrategies final : public PlatformStrategies {
    LoaderStrategy* createLoaderStrategy() final { return &m_loader; }
    MediaStrategy* createMediaStrategy() final { return &m_media; }
    PasteboardStrategy* createPasteboardStrategy() final { return nullptr; }
    BlobRegistry* createBlobRegistry() final { return nullptr; }
    LocalLoader m_loader;
    LocalMedia m_media;
};

void installLocalPageStrategies()
{
    static NeverDestroyed<LocalStrategies> strategies;
    setPlatformStrategies(&strategies.get());
}

} // namespace WebCore
