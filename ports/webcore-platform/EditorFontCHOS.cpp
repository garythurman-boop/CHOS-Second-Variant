#include "config.h"
#include "CSSPropertyNames.h"
#include "Document.h"
#include "EditingStyle.h"
#include "Editor.h"
#include "FrameSelection.h"
#include "MutableStyleProperties.h"
#include <wtf/NeverDestroyed.h>

namespace WebCore {

static RefPtr<MutableStyleProperties>& copiedFont()
{
    static NeverDestroyed<RefPtr<MutableStyleProperties>> properties;
    return properties.get();
}

void Editor::platformCopyFont()
{
    auto style = EditingStyle::create(document().selection().selection().start());
    static constexpr CSSPropertyID fontProperties[] = {
        CSSPropertyFontFamily, CSSPropertyFontSize, CSSPropertyFontStyle,
        CSSPropertyFontWeight, CSSPropertyFontVariantCaps
    };
    auto* properties = style->style();
    copiedFont() = properties ? properties->copyProperties(fontProperties).ptr() : nullptr;
}

void Editor::platformPasteFont()
{
    if (auto font = copiedFont())
        applyStyle(font.get(), EditAction::PasteFont);
}

} // namespace WebCore
