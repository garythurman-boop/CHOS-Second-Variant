// The clipboard belongs to the main thread of this browser process. Snapshots
// keep a synchronous reader valid if a callback changes the clipboard.
#include "config.h"
#include "Pasteboard.h"
#include "StaticPasteboard.h"
#include "ColorSerialization.h"
#include "Image.h"
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>

namespace WebCore {

struct Clipboard {
    Vector<PasteboardCustomData> items;
    bool smartReplace { false };
    int64_t revision { 0 };

    void didChange()
    {
        revision = revision == INT64_MAX ? 1 : revision + 1;
    }
};

static Clipboard& clipboard()
{
    ASSERT(isMainThread());
    static NeverDestroyed<Clipboard> state;
    return state;
}

static std::optional<PasteboardCustomData> snapshot(std::optional<size_t> index = std::nullopt)
{
    auto& items = clipboard().items;
    if (index.value_or(0) >= items.size())
        return std::nullopt;
    return items[index.value_or(0)];
}

static void replaceContents(PasteboardCustomData&& item, bool smartReplace = false)
{
    auto& state = clipboard();
    state.items.clear();
    if (item.hasData())
        state.items.append(WTF::move(item));
    state.smartReplace = smartReplace;
    state.didChange();
}

Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& context)
    : m_context(WTF::move(context))
{
}

std::unique_ptr<Pasteboard> Pasteboard::createForCopyAndPaste(std::unique_ptr<PasteboardContext>&& context)
{
    return makeUnique<Pasteboard>(WTF::move(context));
}

bool Pasteboard::hasData()
{
    return std::ranges::any_of(clipboard().items, [](auto& item) { return item.hasData(); });
}

Vector<String> Pasteboard::typesSafeForBindings(const String& origin)
{
    auto item = snapshot();
    if (!item)
        return { };
    auto types = item->orderedTypes();
    if (origin.isEmpty() || item->origin() != origin)
        types.removeAllMatching([&](auto& type) {
            return !isSafeTypeForDOMToReadAndWrite(type) || item->readString(type).isNull();
        });
    return types;
}

Vector<String> Pasteboard::typesForLegacyUnsafeBindings()
{
    auto item = snapshot();
    return item ? item->orderedTypes() : Vector<String> { };
}

String Pasteboard::readOrigin()
{
    auto item = snapshot();
    return item ? item->origin() : String { };
}

String Pasteboard::readString(const String& type)
{
    auto item = snapshot();
    return item ? item->readString(type) : String { };
}

String Pasteboard::readStringInCustomData(const String& type)
{
    auto item = snapshot();
    return item ? item->readStringInCustomData(type) : String { };
}

void Pasteboard::writeString(const String& type, const String& data)
{
    // A direct platform write has no origin. Keep only public string formats
    // from a previous item so it cannot inherit another writer's custom values.
    PasteboardCustomData updated;
    if (auto old = snapshot()) {
        old->forEachPlatformString([&](const String& name, const String& value) {
            if (isSafeTypeForDOMToReadAndWrite(name))
                updated.writeString(name, value);
        });
    }
    updated.writeString(type, data);
    replaceContents(WTF::move(updated));
}

void Pasteboard::clear()
{
    replaceContents({ });
}

void Pasteboard::clear(const String& type)
{
    auto& state = clipboard();
    for (auto& item : state.items)
        item.clear(type);
    state.items.removeAllMatching([](auto& item) { return !item.hasData(); });
    state.smartReplace = false;
    state.didChange();
}

void Pasteboard::read(PasteboardPlainText& result, PlainTextURLReadingPolicy policy, std::optional<size_t> index)
{
    result.text = { };
    if (auto item = snapshot(index)) {
        result.text = item->readString("text/plain"_s);
        if (result.text.isNull() && policy == PlainTextURLReadingPolicy::AllowURL)
            result.text = item->readString("text/uri-list"_s);
    }
}

void Pasteboard::read(PasteboardWebContentReader& reader, WebContentReadingPolicy policy, std::optional<size_t> index)
{
    auto item = snapshot(index);
    if (!item)
        return;
    reader.setContentOrigin(item->origin());
    auto html = item->readString("text/html"_s);
    if (!html.isNull() && reader.readHTML(html))
        return;
    if (policy == WebContentReadingPolicy::OnlyRichTextTypes)
        return;
    for (auto& entry : item->data()) {
        if (!entry.type.startsWith("image/"_s))
            continue;
        if (auto buffer = item->readBuffer(entry.type); buffer && reader.readImage(buffer.releaseNonNull(), entry.type))
            return;
    }
    auto text = item->readString("text/plain"_s);
    if (!text.isNull() && reader.readPlainText(text))
        return;
    URL url { item->readString("text/uri-list"_s) };
    if (url.isValid())
        reader.readURL(url, { });
}

static void readFiles(PasteboardFileReader& reader, const PasteboardCustomData& item)
{
    for (auto& entry : item.data()) {
        if (auto buffer = item.readBuffer(entry.type); buffer && reader.shouldReadBuffer(entry.type))
            reader.readBuffer({ }, entry.type, buffer.releaseNonNull());
    }
}

void Pasteboard::read(PasteboardFileReader& reader, std::optional<size_t> index)
{
    if (auto item = snapshot(index))
        readFiles(reader, *item);
}

void StaticPasteboard::read(PasteboardFileReader& reader, std::optional<size_t> index)
{
    if (!index.value_or(0))
        readFiles(reader, PasteboardCustomData { m_customData });
}

void StaticPasteboard::write(const PasteboardBuffer& buffer)
{
    if (buffer.data && !buffer.type.isEmpty())
        writeData(buffer.type, Ref { *buffer.data });
}

void Pasteboard::write(const Color& color)
{
    writePlainText(serializationForCSS(color), CannotSmartReplace);
}

void Pasteboard::write(const PasteboardURL& value)
{
    PasteboardCustomData item;
    if (value.url.isValid()) {
        item.writeString("text/uri-list"_s, value.url.string());
        item.writeString("text/plain"_s, value.url.string());
        if (!value.markup.isEmpty())
            item.writeString("text/html"_s, value.markup);
    }
    replaceContents(WTF::move(item));
}

void Pasteboard::writeTrustworthyWebURLsPboardType(const PasteboardURL& value)
{
    // This port only offers the ordinary URI representation.
    if (value.url.protocolIsInHTTPFamily())
        write(value);
}

void Pasteboard::write(const PasteboardImage& image)
{
    PasteboardCustomData item;
    if (image.resourceData && !image.resourceMIMEType.isEmpty())
        item.writeData(image.resourceMIMEType, Ref { *image.resourceData });
    else if (image.image && image.image->data() && !image.image->mimeType().isEmpty())
        item.writeData(image.image->mimeType(), image.image->data()->makeContiguous());
    if (canExposeURLToDOMWhenPasteboardContainsFiles(image.url.url.string()))
        item.writeString("text/uri-list"_s, image.url.url.string());
    replaceContents(WTF::move(item));
}

void Pasteboard::write(const PasteboardBuffer& buffer)
{
    PasteboardCustomData item;
    if (buffer.data && !buffer.type.isEmpty())
        item.writeData(buffer.type, Ref { *buffer.data });
    replaceContents(WTF::move(item));
}

void Pasteboard::write(const PasteboardWebContent& content)
{
    PasteboardCustomData item;
    item.setOrigin(content.contentOrigin);
    if (!content.markup.isNull())
        item.writeString("text/html"_s, content.markup);
    if (!content.text.isNull())
        item.writeString("text/plain"_s, content.text);
    replaceContents(WTF::move(item), content.canSmartCopyOrDelete);
}

void Pasteboard::writeCustomData(const Vector<PasteboardCustomData>& items)
{
    clipboard().items = items;
    clipboard().smartReplace = false;
    clipboard().didChange();
}

Pasteboard::FileContentState Pasteboard::fileContentState()
{
    for (auto& item : clipboard().items) {
        for (auto& entry : item.data()) {
            if (entry.type.startsWith("image/"_s) && item.readBuffer(entry.type))
                return FileContentState::InMemoryImage;
        }
    }
    return FileContentState::NoFileOrImageData;
}

int64_t Pasteboard::changeCount() const
{
    return isStatic() ? 0 : clipboard().revision;
}

bool Pasteboard::canSmartReplace()
{
    return clipboard().smartReplace;
}

void Pasteboard::writeMarkup(const String& markup)
{
    PasteboardCustomData item;
    item.writeString("text/html"_s, markup);
    replaceContents(WTF::move(item));
}

void Pasteboard::writePlainText(const String& text, SmartReplaceOption option)
{
    PasteboardCustomData item;
    item.writeString("text/plain"_s, text);
    replaceContents(WTF::move(item), option == CanSmartReplace);
}

} // namespace WebCore
