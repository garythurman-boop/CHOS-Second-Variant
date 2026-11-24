// Software scrollbars for the native WebCore port.
#include "config.h"
#include "GraphicsContext.h"
#include "ScrollableArea.h"
#include "ScrollbarInlines.h"
#include "ScrollbarThemeComposite.h"
#include <wtf/NeverDestroyed.h>

namespace WebCore {

class ScrollbarThemeCHOS final : public ScrollbarThemeComposite {
public:
    int scrollbarThickness(ScrollbarWidth width, OverlayScrollbarSizeRelevancy) override
    {
        switch (width) {
        case ScrollbarWidth::Auto: return 12;
        case ScrollbarWidth::Thin: return 6;
        case ScrollbarWidth::None: return 0;
        }
        RELEASE_ASSERT_NOT_REACHED();
    }

    ScrollbarButtonsPlacement buttonsPlacement() const override { return ScrollbarButtonsNone; }
    bool hasButtons(Scrollbar&) override { return false; }
    bool hasThumb(Scrollbar& bar) override { return bar.enabled() && bar.maximum() > 0 && thumbLength(bar) > 0; }
    IntRect backButtonRect(Scrollbar&, ScrollbarPart, bool) override { return { }; }
    IntRect forwardButtonRect(Scrollbar&, ScrollbarPart, bool) override { return { }; }
    IntRect trackRect(Scrollbar& bar, bool) override { return bar.frameRect(); }

    void paintTrackBackground(GraphicsContext& context, Scrollbar& bar, const IntRect& rect) override
    {
        auto color = bar.scrollableArea().scrollbarTrackColorStyle();
        context.fillRect(rect, color.isValid() ? color : Color { SRGBA<uint8_t> { 231, 234, 237 } });
    }

    void paintThumb(GraphicsContext& context, Scrollbar& bar, const IntRect& rect) override
    {
        auto color = bar.scrollableArea().scrollbarThumbColorStyle();
        if (!color.isValid()) {
            if (bar.pressedPart() == ThumbPart)
                color = SRGBA<uint8_t> { 66, 73, 81 };
            else if (bar.hoveredPart() == ThumbPart)
                color = SRGBA<uint8_t> { 87, 97, 107 };
            else
                color = SRGBA<uint8_t> { 111, 121, 131 };
        }
        context.fillRect(rect, color);
    }
};

ScrollbarTheme& ScrollbarTheme::nativeTheme()
{
    static NeverDestroyed<ScrollbarThemeCHOS> theme;
    return theme;
}

} // namespace WebCore
