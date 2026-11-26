#include "config.h"
#include "DestinationColorSpace.h"
#include "GraphicsContextStateSaver.h"
#include "GraphicsLayer.h"
#include "GraphicsLayerClient.h"
#include "GraphicsLayerFactory.h"
#include "ImageBuffer.h"
#include "PixelFormat.h"
#include "RenderingMode.h"

namespace WebCore {

class SoftwareGraphicsLayer final : public GraphicsLayer {
public:
    SoftwareGraphicsLayer(Type type, GraphicsLayerClient& client)
        : GraphicsLayer(type, client) { }
    ~SoftwareGraphicsLayer() final { willBeDestroyed(); }

private:
    void setNeedsDisplay() final { setNeedsDisplayInRect({ { }, size() }, ShouldClipToLayer::Clip); }

    void setNeedsDisplayInRect(const FloatRect& rect, ShouldClipToLayer) final
    {
        auto clipped = intersection(rect, FloatRect { { }, size() });
        if (clipped.isEmpty())
            return;
        bool notify = m_dirty.isEmpty();
        m_dirty.unite(clipped);
        if (notify)
            client().notifyFlushRequired(this);
    }

    void setDrawsContent(bool enabled) final
    {
        if (enabled == drawsContent())
            return;
        GraphicsLayer::setDrawsContent(enabled);
        if (enabled)
            setNeedsDisplay();
        else {
            m_dirty = { };
            m_backing = nullptr;
        }
    }

    void setSize(const FloatSize& value) final
    {
        if (size() != value) {
            m_backing = nullptr;
            m_dirty = { };
        }
        GraphicsLayer::setSize(value);
    }

    void flushCompositingStateForThisLayerOnly() final
    {
        if (!drawsContent() || !contentsAreVisible() || m_dirty.isEmpty())
            return;
        if (!m_backing) {
            m_backing = ImageBuffer::create(size(), RenderingMode::Unaccelerated, RenderingPurpose::LayerBacking,
                1, DestinationColorSpace::SRGB(), PixelFormat::BGRA8);
            if (!m_backing)
                return;
        }
        // A paint callback may invalidate more content or replace this backing.
        Ref backing = *m_backing;
        auto dirty = std::exchange(m_dirty, { });
        auto& context = backing->context();
        GraphicsContextStateSaver saver(context);
        context.clip(dirty);
        context.clearRect(dirty);
        paintGraphicsLayerContents(context, dirty);
        incrementRepaintCount();
    }

    void flushCompositingState(const FloatRect& visibleRect) final
    {
        flushCompositingStateForThisLayerOnly();
        auto snapshot = children();
        for (auto& child : snapshot)
            child->flushCompositingState(visibleRect);
    }

    double backingStoreMemoryEstimate() const final { return m_backing ? m_backing->memoryCost() : 0; }

    FloatRect m_dirty;
    RefPtr<ImageBuffer> m_backing;
};

Ref<GraphicsLayer> GraphicsLayer::create(GraphicsLayerFactory* factory, GraphicsLayerClient& client, Type type)
{
    if (factory)
        return factory->createGraphicsLayer(type, client);
    return adoptRef(*new SoftwareGraphicsLayer(type, client));
}

} // namespace WebCore
