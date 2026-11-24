/* CHOS software local-page diagnostic policy, 2026-11-16.
 * The maintained generic Theme/RenderTheme implementations supply CSS control
 * fallback painting. This does not invent a native widget or preference API. */
#include "config.h"
#include "RenderTheme.h"
#include "Theme.h"
#include <wtf/NeverDestroyed.h>

namespace WebCore {

class ThemeCHOS final : public Theme {
public:
    ThemeCHOS() = default;
};

class RenderThemeCHOS final : public RenderTheme {
public:
    RenderThemeCHOS() = default;
};

Theme& Theme::singleton()
{
    static NeverDestroyed<ThemeCHOS> theme;
    return theme;
}

RenderTheme& RenderTheme::singleton()
{
    static NeverDestroyed<RenderThemeCHOS> theme;
    return theme;
}

} // namespace WebCore
