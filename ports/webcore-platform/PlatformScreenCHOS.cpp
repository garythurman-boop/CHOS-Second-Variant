/* CHOS software-compositor screen backend, 2026-11-19.
 * Dimensions/work area/colour depth/desktop scale come from the actual owned
 * read-only GUI service. It exposes one virtual desktop, not monitor identity,
 * physical DPI, an ICC profile or an independently scaled Retina raster. */
#include "config.h"
#include "PlatformScreen.h"
#include "ContentsFormat.h"
#include "DestinationColorSpace.h"
#include "FloatRect.h"
#include <chos/user.h>
#include <optional>

namespace WebCore {

static std::optional<chos_gui_screen_info_t> screenInfo()
{
    chos_gui_screen_info_t info { };
    if (chos_gui_screen_info(&info) || info.size_bytes != sizeof(info)
        || info.version != CHOS_GUI_SCREEN_VERSION || info.reserved
        || info.pixel_format != CHOS_GUI_SCREEN_XRGB8888
        || !info.width || !info.height || !info.scale_numerator || !info.scale_denominator
        || info.bits_per_pixel != 32 || info.color_bits != 24)
        return std::nullopt;
    return info;
}

static FloatRect logicalRect(int32_t x, int32_t y, uint32_t width, uint32_t height,
    const chos_gui_screen_info_t& info)
{
    double ratio = static_cast<double>(info.scale_denominator) / info.scale_numerator;
    return { static_cast<float>(x * ratio), static_cast<float>(y * ratio),
        static_cast<float>(width * ratio), static_cast<float>(height * ratio) };
}

int screenDepth(Widget*)
{
    auto info = screenInfo();
    return info ? info->color_bits : 0;
}

int screenDepthPerComponent(Widget*)
{
    auto info = screenInfo();
    return info ? info->color_bits / 3 : 0;
}

bool screenIsMonochrome(Widget*) { return false; }
bool screenHasInvertedColors() { return false; }
bool screenSupportsExtendedColor(Widget*) { return false; }

DestinationColorSpace screenColorSpace(Widget*)
{
    // The current 8-bit software rendering domain is explicitly sRGB. The
    // service does not supply a monitor calibration or HDR capability.
    return DestinationColorSpace::SRGB();
}

FloatRect screenRect(Widget*)
{
    auto info = screenInfo();
    return info ? logicalRect(info->x, info->y, info->width, info->height, *info) : FloatRect { };
}

FloatRect screenAvailableRect(Widget*)
{
    auto info = screenInfo();
    return info ? logicalRect(info->available_x, info->available_y,
        info->available_width, info->available_height, *info) : FloatRect { };
}

OptionSet<ContentsFormat> screenContentsFormats(Widget*)
{
    return screenInfo() ? OptionSet<ContentsFormat> { ContentsFormat::RGBA8 } : OptionSet<ContentsFormat> { };
}

ScreenProperties collectScreenProperties()
{
    ScreenProperties properties;
    auto info = screenInfo();
    if (!info)
        return properties;
    ScreenData data;
    data.screenRect = logicalRect(info->x, info->y, info->width, info->height, *info);
    data.screenAvailableRect = logicalRect(info->available_x, info->available_y,
        info->available_width, info->available_height, *info);
    data.screenDepth = info->color_bits;
    data.screenDepthPerComponent = info->color_bits / 3;
    // CHOS currently owns one software desktop. This is a local stable key,
    // not a physical connector, monitor serial, GPU or firmware identifier.
    properties.primaryDisplayID = 1;
    properties.screenDataMap.add(properties.primaryDisplayID, WTF::move(data));
    return properties;
}

} // namespace WebCore
