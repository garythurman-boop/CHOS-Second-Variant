#include "config.h"
#include "Color.h"
#include "GraphicsContext.h"
#include "GraphicsLayer.h"
#include "GraphicsLayerClient.h"
#include "NetworkLoadMetrics.h"
#include "ResourceError.h"
#include "ResourceHandle.h"
#include "ResourceHandleClient.h"
#include "ResourceLoaderOptions.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SecurityOrigin.h"
#include "SharedBuffer.h"
#include "StoredCredentialsPolicy.h"
#include <cstdio>
#include <wtf/RunLoop.h>

using namespace WebCore;

#define CHECK(test) do { if (!(test)) { std::printf("[FAIL] local platform line %u: %s\n", __LINE__, #test); return false; } } while (0)

class PaintClient final : public GraphicsLayerClient {
public:
    void notifyFlushRequired(const GraphicsLayer*) final { ++invalidations; }
    void paintContents(const GraphicsLayer&, GraphicsContext& context, const FloatRect& rect, OptionSet<GraphicsLayerPaintBehavior>) final
    {
        ++paints;
        clip = rect;
        context.fillRect(rect, Color::black);
    }
    unsigned invalidations { 0 };
    unsigned paints { 0 };
    FloatRect clip;
};

class DataClient final : public ResourceHandleClient {
public:
    void willSendRequestAsync(ResourceHandle*, ResourceRequest&& request, ResourceResponse&&, CompletionHandler<void(ResourceRequest&&)>&& done) final
    {
        done(WTF::move(request));
    }
    void didReceiveResponseAsync(ResourceHandle*, ResourceResponse&& response, CompletionHandler<void()>&& done) final
    {
        type = response.mimeType();
        acknowledgment = WTF::move(done);
        ++responses;
    }
    void didReceiveData(ResourceHandle* handle, const SharedBuffer& buffer, int size) final
    {
        body = String::fromUTF8(buffer.span());
        count = size;
        if (pauseInData)
            handle->setDefersLoading(true);
    }
    void didFinishLoading(ResourceHandle*, const NetworkLoadMetrics&) final { ++complete; }
    void didFail(ResourceHandle*, const ResourceError&) final { ++failures; }

    CompletionHandler<void()> acknowledgment;
    String type;
    String body;
    unsigned responses { 0 }, complete { 0 }, failures { 0 };
    int count { 0 };
    bool pauseInData { false };
};

static void tick()
{
    // Bound the generic event loop to one non-blocking iteration, even when
    // a suspended or cancelled handle has correctly scheduled no work.
    RunLoop::mainSingleton().dispatch([] { });
    RunLoop::cycle();
}

static RefPtr<ResourceHandle> load(DataClient& client, const URL& url, bool deferred = false)
{
    return ResourceHandle::create(nullptr, ResourceRequest { URL { url } }, &client, deferred, false,
        ContentEncodingSniffingPolicy::Disable, nullptr, false);
}

bool exerciseLocalPlatform()
{
    PaintClient painter;
    auto layer = GraphicsLayer::create(nullptr, painter);
    layer->setSize({ 16, 8 });
    layer->setDrawsContent(true);
    layer->setNeedsDisplay();
    CHECK(painter.invalidations == 1);
    layer->flushCompositingStateForThisLayerOnly();
    CHECK(painter.paints == 1 && painter.clip == FloatRect(0, 0, 16, 8) && layer->backingStoreMemoryEstimate() > 0);
    layer->setNeedsDisplayInRect({ 12, 1, 20, 3 });
    layer->flushCompositingState({ 0, 0, 16, 8 });
    CHECK(painter.paints == 2 && painter.clip == FloatRect(12, 1, 4, 3));
    layer->flushCompositingStateForThisLayerOnly();
    CHECK(painter.paints == 2);
    layer->setDrawsContent(false);
    CHECK(!layer->backingStoreMemoryEstimate());

    URL url { "data:text/plain;base64,Y2hvcw=="_s };
    DataClient client;
    client.pauseInData = true;
    auto handle = load(client, url, true);
    CHECK(handle);
    tick();
    CHECK(!client.responses);
    handle->setDefersLoading(false);
    tick();
    CHECK(client.responses == 1 && client.type == "text/plain"_s && !client.count && !client.complete);
    tick();
    CHECK(!client.count);
    client.acknowledgment();
    tick();
    CHECK(client.count == 4 && client.body == "chos"_s && !client.complete);
    tick();
    CHECK(!client.complete);
    handle->setDefersLoading(false);
    tick();
    CHECK(client.complete == 1 && !client.failures);

    DataClient cancelled;
    auto second = load(cancelled, url);
    tick();
    CHECK(cancelled.responses == 1);
    second->cancel();
    cancelled.acknowledgment();
    tick();
    CHECK(!cancelled.count && !cancelled.complete && !cancelled.failures);

    DataClient unsupported;
    auto remote = load(unsupported, URL { "https://example.com/"_s });
    CHECK(remote && !unsupported.failures);
    tick();
    CHECK(unsupported.failures == 1 && !unsupported.responses && !unsupported.complete);
    std::puts("[ OK ] native software layer painting and deferred data resource callbacks passed");
    return true;
}

// Keep the diagnostic process's real abort behavior and preserve enough of
// its checked AArch64 frame chain to symbolize early engine startup failures.
#include <chos/user.h>
extern "C" [[noreturn]] void __real_abort();

extern "C" [[noreturn]] void __wrap_abort()
{
    chos_thread_context_t context { };
    std::puts("[FAIL] local page abort");
    if (!chos_thread_context(&context) && (context.flags & CHOS_THREAD_CONTEXT_STACK_VALID)) {
        auto frame = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
        auto end = context.stack_base + context.stack_bytes;
        for (unsigned i = 0; i < 24 && !(frame & 15) && frame >= context.stack_base && frame + 16 <= end; ++i) {
            auto* record = reinterpret_cast<const uintptr_t*>(frame);
            std::printf("[PAGE RETURN] %llx\n", static_cast<unsigned long long>(record[1]));
            if (record[0] <= frame)
                break;
            frame = record[0];
        }
    }
    __real_abort();
}
