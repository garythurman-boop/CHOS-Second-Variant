#include "config.h"
#include <WebCore/FormData.h>
#include <WebCore/PublicSuffixStore.h>
#include "WebCorePersistentCoders.h"
#include <wtf/persistence/PersistentEncoder.h>
#include <wtf/persistence/PersistentDecoder.h>
#include <WebCore/HTTPHeaderNames.h>
#include <WebCore/IPAddressSpace.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/ResourceError.h>
#include <wtf/Threading.h>
#include <wtf/text/WTFString.h>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <atomic>
#include <chos/libc.h>

using namespace WebCore;
#define CHECK(x) do { if (!(x)) { std::printf("[FAIL] WebCore network values line %u: %s\n", __LINE__, #x); return false; } } while (0)
static URL url(ASCIILiteral text) { return URL { String { text } }; }
static const uint8_t bodyBytes[] = {0, 1, 127, 128, 255};
static std::atomic<unsigned> checks;

static ResourceRequest populatedRequest()
{
    ResourceRequest request(url("https://example.com/path?q=1#fragment"_s));
    request.setHTTPMethod("POST"_s);
    request.setCachePolicy(ResourceRequestCachePolicy::DoNotUseAnyCache);
    request.setTimeoutInterval(7.25);
    request.setFirstPartyForCookies(url("https://first.example/"_s));
    request.setHTTPHeaderField(HTTPHeaderName::ContentType, "application/octet-stream"_s);
    request.setHTTPHeaderField("X-Value"_s, "original"_s);
    request.setHTTPReferrer("https://example.com/referrer"_s);
    request.setPriority(ResourceLoadPriority::VeryHigh);
    request.setRequester(ResourceRequestRequester::Fetch);
    request.setInitiatorIdentifier("initiator"_s);
    request.setInspectorInitiatorNodeIdentifier(17);
    request.setSameSiteDisposition(ResourceRequestBase::SameSiteDisposition::CrossSite);
    request.setIsTopSite(true);
    request.setShouldBlockThirdPartyStorage(false);
    request.setHiddenFromInspector(true);
    request.setTargetAddressSpace(IPAddressSpace::Local);
    request.setWasSchemeOptimisticallyUpgraded(true);
    request.setIsAppInitiated(false);
    request.setPrivacyProxyFailClosedForUnreachableNonMainHosts(true);
    request.setUseAdvancedPrivacyProtections(true);
    request.setDidFilterLinkDecoration(true);
    request.setIsPrivateTokenUsageByThirdPartyAllowed(true);
    request.setAllowCookies(false);
    request.setResponseContentDispositionEncodingFallbackArray(Vector<String> { "UTF-8"_s, "ISO-8859-1"_s, "windows-1252"_s, "UTF-16"_s });
    Vector<FormDataElement> elements;
    elements.append(FormDataElement { Vector<uint8_t> { std::span<const uint8_t>(bodyBytes) } });
    request.setHTTPBody(FormData::create(WTF::move(elements), 37, true, Vector<uint8_t> { 45, 45, 88, 0 }));
    return request;
}

static bool policy(const ResourceRequest& request)
{
    CHECK(request.url() == url("https://example.com/path?q=1#fragment"_s));
    CHECK(request.httpMethod() == "POST"_s && request.cachePolicy() == ResourceRequestCachePolicy::DoNotUseAnyCache);
    CHECK(request.timeoutInterval() == 7.25 && request.firstPartyForCookies() == url("https://first.example/"_s));
    CHECK(request.priority() == ResourceLoadPriority::VeryHigh && request.requester() == ResourceRequestRequester::Fetch);
    CHECK(request.initiatorIdentifier() == "initiator"_s && request.inspectorInitiatorNodeIdentifier() == 17);
    CHECK(request.sameSiteDisposition() == ResourceRequestBase::SameSiteDisposition::CrossSite && request.isTopSite());
    CHECK(!request.shouldBlockThirdPartyStorage() && request.hiddenFromInspector());
    CHECK(request.targetAddressSpace() == IPAddressSpace::Local && request.wasSchemeOptimisticallyUpgraded());
    CHECK(!request.isAppInitiated() && request.privacyProxyFailClosedForUnreachableNonMainHosts());
    CHECK(request.useAdvancedPrivacyProtections() && request.didFilterLinkDecoration());
    CHECK(request.isPrivateTokenUsageByThirdPartyAllowed() && !request.allowCookies());
    const auto& encodings = request.responseContentDispositionEncodingFallbackArray();
    CHECK(encodings.size() == 4 && encodings[0] == "UTF-8"_s && encodings[3] == "UTF-16"_s);
    return true;
}

static bool requests()
{
    auto source = populatedRequest();
    auto copy = source.isolatedCopy();
    CHECK(policy(copy));
    CHECK(copy.httpBody() && copy.httpBody() != source.httpBody());
    CHECK(copy.httpBody()->identifier() == 37 && copy.httpBody()->alwaysStream());
    CHECK(copy.httpBody()->boundary() == Vector<uint8_t>({45, 45, 88, 0}));
    CHECK(copy.httpBody()->boundary().span().data() != source.httpBody()->boundary().span().data());
    auto bytes = copy.httpBody()->flatten();
    CHECK(bytes.size() == sizeof(bodyBytes) && !std::memcmp(bytes.span().data(), bodyBytes, sizeof(bodyBytes)));
    source.setHTTPHeaderField("X-Value"_s, "changed"_s);
    const uint8_t extra[] = {42}; source.httpBody()->appendData(extra);
    CHECK(copy.httpHeaderField("x-value"_s) == "original"_s && copy.httpBody()->flatten().size() == sizeof(bodyBytes));
    ResourceRequestBase& base = copy;
    base.setAsIsolatedCopy(copy);
    CHECK(policy(copy) && copy.httpBody()->flatten().size() == sizeof(bodyBytes));

    auto dirty = populatedRequest();
    ResourceRequest empty(url("https://empty.example/"_s));
    static_cast<ResourceRequestBase&>(dirty).setAsIsolatedCopy(empty);
    CHECK(dirty.url() == empty.url() && !dirty.httpBody());
    CHECK(dirty.responseContentDispositionEncodingFallbackArray().isEmpty() && !dirty.inspectorInitiatorNodeIdentifier());
    CHECK(dirty.isSameSiteUnspecified() && !dirty.hiddenFromInspector());
    CHECK(dirty.targetAddressSpace() == IPAddressSpace::Public && !dirty.wasSchemeOptimisticallyUpgraded());
    CHECK(dirty.httpHeaderFields().isEmpty() && !dirty.didFilterLinkDecoration());

    auto serialSource = populatedRequest();
    auto data = serialSource.getRequestDataToSerialize();
    auto restored = ResourceRequest::fromResourceRequestData(WTF::move(data));
    CHECK(restored.url() == serialSource.url() && restored.httpMethod() == "POST"_s);
    CHECK(restored.httpHeaderField("X-Value"_s) == "original"_s);
    CHECK(restored.targetAddressSpace() == IPAddressSpace::Local && restored.wasSchemeOptimisticallyUpgraded());
    CHECK(restored.responseContentDispositionEncodingFallbackArray().size() == 4);
    // RequestData deliberately excludes the separately serialized upload body.
    CHECK(!restored.httpBody());

    auto delegateTarget = populatedRequest();
    ResourceRequest delegate(url("https://delegate.example/"_s));
    delegate.setInspectorInitiatorNodeIdentifier(99);
    delegateTarget.updateFromDelegatePreservingOldProperties(delegate);
    CHECK(delegateTarget.url() == delegate.url() && delegateTarget.inspectorInitiatorNodeIdentifier() == 17);
    CHECK(delegateTarget.httpBody() && delegateTarget.priority() == ResourceLoadPriority::VeryHigh);
    CHECK(delegateTarget.hiddenFromInspector() && delegateTarget.requester() == ResourceRequestRequester::Fetch);
    CHECK(delegateTarget.initiatorIdentifier() == "initiator"_s);
    ResourceRequest noNode(url("https://no-node.example/"_s));
    noNode.updateFromDelegatePreservingOldProperties(delegate);
    CHECK(!noNode.inspectorInitiatorNodeIdentifier());
    ++checks; return true;
}

static bool redirects()
{
    for (int status : {301, 302, 303, 307, 308}) {
        ResourceRequest request(url("https://example.com/start#kept"_s));
        request.setHTTPMethod("POST"_s);
        request.setHTTPBody(FormData::create(bodyBytes));
        request.setHTTPHeaderField(HTTPHeaderName::ContentType, "application/octet-stream"_s);
        request.setHTTPHeaderField(HTTPHeaderName::Authorization, "secret"_s);
        request.setHTTPHeaderField(HTTPHeaderName::ProxyAuthorization, "proxy-secret"_s);
        request.setHTTPOrigin("https://example.com"_s);
        request.setHTTPReferrer("https://example.com/referrer"_s);
        ResourceResponse response(url("https://example.com/start#kept"_s), "text/html"_s, 0, "UTF-8"_s);
        response.setHTTPStatusCode(status);
        response.setHTTPHeaderField(HTTPHeaderName::Location, "http://other.example/next"_s);
        auto next = request.redirectedRequest(response, true, ResourceRequestBase::ShouldSetHash::Yes);
        CHECK(next.url() == url("http://other.example/next#kept"_s));
        CHECK(next.httpHeaderField(HTTPHeaderName::Authorization).isEmpty() && next.httpHeaderField(HTTPHeaderName::ProxyAuthorization).isEmpty());
        CHECK(next.httpOrigin().isEmpty() && next.httpReferrer().isEmpty());
        bool becomesGET = status == 301 || status == 302 || status == 303;
        CHECK(next.httpMethod() == (becomesGET ? "GET"_s : "POST"_s));
        CHECK((next.httpBody() == nullptr) == becomesGET);
        CHECK(next.httpHeaderField(HTTPHeaderName::ContentType).isEmpty() == becomesGET);
        ++checks;
    }
    return true;
}

static bool responses()
{
    ResourceResponse empty; CHECK(empty.isNull());
    ResourceResponse response(url("https://example.com/file"_s), "text/plain"_s, 123, "UTF-8"_s);
    response.setHTTPStatusCode(206);
    response.setHTTPStatusText("Partial Content"_s);
    response.setHTTPVersion("HTTP/1.1"_s);
    response.setHTTPHeaderField(HTTPHeaderName::ContentDisposition, "attachment; filename=report.txt"_s);
    response.setHTTPHeaderField(HTTPHeaderName::CacheControl, "no-store, max-age=60"_s);
    response.setHTTPHeaderField(HTTPHeaderName::ContentRange, "bytes 0-122/1000"_s);
    response.setSource(ResourceResponse::Source::Network);
    response.setType(ResourceResponse::Type::Cors);
    response.setTainting(ResourceResponse::Tainting::Cors);
    response.setRedirected(true); response.setAsRangeRequested();
    response.setIPAddressSpace(IPAddressSpace::Local);
    CHECK(!response.isNull() && response.isSuccessful() && response.isAttachment());
    CHECK(response.suggestedFilename() == "report.txt"_s && response.cacheControlContainsNoStore());
    CHECK(response.cacheControlMaxAge() && response.cacheControlMaxAge()->seconds() == 60);
    auto restored = ResourceResponse::fromCrossThreadData(response.crossThreadData());
    CHECK(restored.url() == response.url() && restored.mimeType() == "text/plain"_s);
    CHECK(restored.expectedContentLength() == 123 && restored.textEncodingName() == "UTF-8"_s);
    CHECK(restored.httpStatusCode() == 206 && restored.httpStatusText() == "Partial Content"_s && restored.httpVersion() == "HTTP/1.1"_s);
    CHECK(restored.httpHeaderField(HTTPHeaderName::ContentRange) == "bytes 0-122/1000"_s);
    CHECK(restored.source() == ResourceResponse::Source::Network && restored.type() == ResourceResponse::Type::Cors);
    CHECK(restored.tainting() == ResourceResponse::Tainting::Cors && restored.isRedirected() && restored.isRangeRequested());
    CHECK(restored.ipAddressSpace() == IPAddressSpace::Local);
    response.setHTTPHeaderField(HTTPHeaderName::ContentDisposition, "attachment; filename=changed.txt"_s);
    CHECK(restored.suggestedFilename() == "report.txt"_s);
    CHECK(restored.certificateInfo() && restored.certificateInfo()->isEmpty() && !restored.certificateInfo()->verificationWasReported());
    ++checks; return true;
}

static bool errors()
{
    ResourceError empty; CHECK(empty.isNull() && !empty.ipcData());
    ResourceError safe("CHOSNetwork"_s, 7, url("https://example.com/"_s), "Visible detail"_s, ResourceError::Type::AccessControl, ResourceError::IsSanitized::Yes);
    ResourceError privateError("CHOSNetwork"_s, 7, url("https://example.com/"_s), "Visible detail"_s, ResourceError::Type::AccessControl, ResourceError::IsSanitized::No);
    CHECK(safe != privateError && safe.sanitizedDescription() == "Visible detail"_s && privateError.sanitizedDescription() == "Load failed"_s);
    auto isolated = safe.isolatedCopy();
    CHECK(isolated == safe && isolated.isSanitized() && isolated.isAccessControl());
    auto restored = ResourceError::fromIPCData(safe.ipcData());
    CHECK(restored == safe && restored.isSanitized());
    CHECK(restored.errorRecoveryMethod() == ResourceError::ErrorRecoveryMethod::NoRecovery);
    CHECK(ResourceError::fromIPCData(std::nullopt).isNull());
    ++checks; return true;
}

static bool persistedCertificates()
{
    using namespace WTF::Persistence;
    using Record = Coder<CertificateInfo>;
    for (int flags : { -1, 0, 19 }) {
        CertificateInfo::CertificateChain chain { Vector<uint8_t> { 0, 255, 37 }, Vector<uint8_t> { 11, 6 } };
        CertificateInfo input { flags, WTF::move(chain), 1 };
        Encoder encoder;
        Record::encodeForPersistence(encoder, input);
        Decoder decoder(encoder.span());
        CHECK(Record::decodeForPersistence(decoder) == input);
        CHECK(decoder.currentOffset() == decoder.length());
        for (size_t i = 0; i < encoder.span().size(); ++i) {
            Decoder truncated(encoder.span().first(i));
            CHECK(!Record::decodeForPersistence(truncated));
        }
    }
    Encoder empty;
    Record::encodeForPersistence(empty, CertificateInfo());
    Decoder decoder(empty.span());
    CHECK(Record::decodeForPersistence(decoder) == CertificateInfo());
    auto reject = [](int32_t flags, uint32_t anchor, uint32_t count, uint32_t size) {
        Encoder data;
        data << uint8_t { 1 } << flags << anchor << count << size;
        Decoder decoder(data.span());
        return !Record::decodeForPersistence(decoder);
    };
    CHECK(reject(0, UINT32_MAX, 0, 0));
    CHECK(reject(-2, UINT32_MAX, 1, 0));
    CHECK(reject(0, 1, 1, 0));
    CHECK(reject(0, UINT32_MAX, 33, 0));
    CHECK(reject(0, UINT32_MAX, 1, UINT32_MAX));
    ++checks;
    return true;
}

static bool suffixes()
{
    auto& store = PublicSuffixStore::singleton();
    CHECK(store.isPublicSuffix("CO.UK"_s) && store.isPublicSuffix("github.io"_s));
    CHECK(!store.isPublicSuffix("user.github.io"_s));
    CHECK(store.topPrivatelyControlledDomain("www.a.github.io"_s) == "a.github.io"_s);
    CHECK(store.topPrivatelyControlledDomain(".WWW.Example.CO.UK"_s) == "example.co.uk"_s);
    CHECK(store.topPrivatelyControlledDomain("co.uk"_s).isEmpty());
    CHECK(store.topPrivatelyControlledDomain("localhost"_s) == "localhost"_s);
    ++checks;
    return true;
}

static bool run()
{
    CHECK(requests() && redirects() && responses() && errors() && persistedCertificates() && suffixes());
    return true;
}

struct Transfer {
    ResourceRequest request;
    std::optional<ResourceResponse::CrossThreadData> response;
    ResourceError error;
};

static void prepareTransfer(Transfer& transfer)
{
    auto source = populatedRequest();
    transfer.request = source.isolatedCopy();
    source.setHTTPHeaderField("X-Value"_s, "discarded source"_s);
    const uint8_t extra[] = {42};
    source.httpBody()->appendData(extra);

    ResourceResponse response(url("https://example.com/transfer"_s), "text/plain"_s, 5, "UTF-8"_s);
    response.setHTTPStatusCode(200);
    response.setHTTPHeaderField(HTTPHeaderName::ContentDisposition, "attachment; filename=transfer.txt"_s);
    transfer.response = response.crossThreadData();
    response.setHTTPHeaderField(HTTPHeaderName::ContentDisposition, "attachment; filename=discarded.txt"_s);

    ResourceError error("CHOSNetwork"_s, 19, url("https://example.com/transfer"_s),
        "Transferred detail"_s, ResourceError::Type::Timeout, ResourceError::IsSanitized::Yes);
    transfer.error = error.isolatedCopy();
    // Every source is destroyed here before the receiving pthread exists.
}

static void* receiveTransfer(void* data)
{
    auto& transfer = *static_cast<Transfer*>(data);
    if (!policy(transfer.request)
        || transfer.request.httpHeaderField("X-Value"_s) != "original"_s
        || transfer.request.httpBody()->flatten().size() != sizeof(bodyBytes))
        return reinterpret_cast<void*>(1);
    auto response = ResourceResponse::fromCrossThreadData(WTF::move(*transfer.response));
    if (response.url() != url("https://example.com/transfer"_s)
        || response.httpStatusCode() != 200
        || response.suggestedFilename() != "transfer.txt"_s
        || !transfer.error.isTimeout() || !transfer.error.isSanitized()
        || transfer.error.sanitizedDescription() != "Transferred detail"_s)
        return reinterpret_cast<void*>(2);
    ++checks;
    return nullptr;
}

static bool threads()
{
    Transfer transfers[3];
    pthread_t workers[3];
    pthread_attr_t attributes;
    CHECK(!pthread_attr_init(&attributes));
    CHECK(!pthread_attr_setstacksize(&attributes, 128 * 1024));
    for (auto& transfer : transfers)
        prepareTransfer(transfer);
    for (unsigned i = 0; i < 3; ++i)
        CHECK(!pthread_create(&workers[i], &attributes, receiveTransfer, &transfers[i]));
    for (unsigned i = 0; i < 3; ++i) {
        void* result = nullptr;
        CHECK(!pthread_join(workers[i], &result) && !result);
    }
    CHECK(!pthread_attr_destroy(&attributes));
    return true;
}

int main()
{
    WTF::initialize();
    chos_heap_stats_t baseline { };
    for (unsigned round = 0; round < 3; ++round) {
        if (!run() || !threads()) return 1;
        chos_heap_stats_t stats;
        if (chos_libc_heap_info(&stats)) return 1;
        std::printf("[VALUES HEAP] round=%u allocations=%zu mapped=%zu\n", round, stats.allocations, stats.mapped_bytes);
        if (!round) baseline = stats;
        else if (stats.allocations != baseline.allocations || stats.mapped_bytes != baseline.mapped_bytes) {
            std::puts("[FAIL] WebCore value warm ownership grew");
            return 1;
        }
    }
    std::printf("[ OK ] actual WebCore request response error values passed; checks=%u\n", checks.load());
    return 0;
}
