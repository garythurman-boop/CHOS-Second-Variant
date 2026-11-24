/* CHOS explicit-catalogue local-page font policy, 2026-11-16.
 * The application must install the real Fontconfig catalogue before creating
 * a Page. Families and weights come from actual matching; 16 CSS pixels is an
 * explicit diagnostic policy, not a claim of system UI-font discovery. */
#include "config.h"
#include "SystemFontDatabase.h"
#include <fontconfig/fontconfig.h>
#include <wtf/NeverDestroyed.h>

namespace WebCore {

SystemFontDatabase& SystemFontDatabase::singleton()
{
    static NeverDestroyed<SystemFontDatabase> database = SystemFontDatabase();
    return database.get();
}

auto SystemFontDatabase::platformSystemFontShorthandInfo(FontShorthand) -> SystemFontShorthandInfo
{
    auto* config = FcConfigGetCurrent();
    RELEASE_ASSERT_WITH_MESSAGE(config, "Install the explicit CHOS font catalogue before Page initialization");
    auto* request = FcPatternCreate();
    RELEASE_ASSERT(request);
    RELEASE_ASSERT(FcPatternAddString(request, FC_FAMILY, reinterpret_cast<const FcChar8*>("sans-serif")));
    RELEASE_ASSERT(FcPatternAddDouble(request, FC_PIXEL_SIZE, 16));
    RELEASE_ASSERT(FcPatternAddInteger(request, FC_WEIGHT, FC_WEIGHT_REGULAR));
    RELEASE_ASSERT(FcConfigSubstitute(config, request, FcMatchPattern));
    FcDefaultSubstitute(request);
    FcResult status;
    auto* match = FcFontMatch(config, request, &status);
    FcPatternDestroy(request);
    RELEASE_ASSERT_WITH_MESSAGE(match && status == FcResultMatch, "The CHOS diagnostic font catalogue has no matching font");
    FcChar8* family = nullptr;
    int fcWeight = FC_WEIGHT_REGULAR;
    RELEASE_ASSERT(FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch && family && *family);
    RELEASE_ASSERT(FcPatternGetInteger(match, FC_WEIGHT, 0, &fcWeight) == FcResultMatch);
    auto selectedFamily = AtomString(String::fromUTF8(reinterpret_cast<const char*>(family)));
    auto selectedWeight = FontSelectionValue::clampFloat(static_cast<float>(FcWeightToOpenType(fcWeight)));
    FcPatternDestroy(match);
    RELEASE_ASSERT(!selectedFamily.isEmpty());
    return { WTF::move(selectedFamily), 16, selectedWeight };
}

void SystemFontDatabase::platformInvalidate()
{
    // There is no platform-side cache: the maintained generic invalidate()
    // clears all derived shorthand values before the next real match.
}

} // namespace WebCore
