#include "config.h"
#include "CommonAtomStrings.h"
#include "Pasteboard.h"
#include "PlatformKeyboardEvent.h"
#include "StaticPasteboard.h"
#include <wtf/MainThread.h>
#include <chos/libc.h>
#include <cstdio>

using namespace WebCore;

#define CHECK(test) do { if (!(test)) { std::printf("[FAIL] WebCore clipboard line %u: %s\n", __LINE__, #test); return false; } } while (0)

class Reader final : public PasteboardWebContentReader {
public:
    explicit Reader(Pasteboard& board) : m_board(board) { }
    bool readHTML(const String& value) final
    {
        html = value;
        // Re-enter before choosing fallback. Data for this read must stay valid.
        m_board.writePlainText("replacement"_s, Pasteboard::CannotSmartReplace);
        return false;
    }
    bool readPlainText(const String& value) final { text = value; return true; }
    bool readFilePath(const String&, PresentationSize, const String&) final { return false; }
    bool readFilePaths(const Vector<String>&) final { return false; }
    bool readImage(Ref<FragmentedSharedBuffer>&&, const String&, PresentationSize) final { return false; }
    bool readURL(const URL&, const String&) final { return false; }
    String html;
    String text;
private:
    Pasteboard& m_board;
};

class FileReader final : public PasteboardFileReader {
public:
    void readFilename(const String&) final { pathSeen = true; }
    bool shouldReadBuffer(const String& type) const final { return type == "image/png"_s; }
    void readBuffer(const String&, const String&, Ref<SharedBuffer>&& value) final { buffer = WTF::move(value); }
    bool pathSeen { false };
    RefPtr<SharedBuffer> buffer;
};

static bool exercise()
{
    auto writer = Pasteboard::createForCopyAndPaste({ });
    auto reader = Pasteboard::createForCopyAndPaste({ });
    CHECK(!reader->hasData());
    auto revision = reader->changeCount();
    writer->writePlainText("one"_s, Pasteboard::CanSmartReplace);
    CHECK(reader->changeCount() != revision && reader->changeCount() == writer->changeCount());
    CHECK(reader->readString("text/plain"_s) == "one"_s && reader->canSmartReplace());

    PasteboardCustomData item;
    item.setOrigin("site-A"_s);
    item.writeString("text/plain"_s, "fallback"_s);
    item.writeString("text/html"_s, "<b>rich</b>"_s);
    item.writeStringInCustomData("application/x-example"_s, "custom"_s);
    PasteboardCustomData second;
    second.writeString("text/uri-list"_s, "https://example.org/"_s);
    writer->writeCustomData({ item, second });
    CHECK(reader->readOrigin() == "site-A"_s && !reader->canSmartReplace());
    CHECK(reader->typesSafeForBindings("site-A"_s).size() == 3);
    CHECK(reader->typesSafeForBindings("site-B"_s).size() == 2);
    CHECK(!reader->typesSafeForBindings("site-B"_s).contains("application/x-example"_s));
    CHECK(reader->readStringInCustomData("application/x-example"_s) == "custom"_s);
    CHECK(reader->readString("application/x-example"_s).isNull());
    PasteboardPlainText plain;
    reader->read(plain, PlainTextURLReadingPolicy::AllowURL, 1);
    CHECK(plain.text == "https://example.org/"_s);
    reader->read(plain, PlainTextURLReadingPolicy::IgnoreURL, 1);
    CHECK(plain.text.isNull());
    reader->read(plain, PlainTextURLReadingPolicy::AllowURL, 2);
    CHECK(plain.text.isNull());

    Reader reentrant(*writer);
    reader->read(reentrant);
    CHECK(reentrant.html == "<b>rich</b>"_s && reentrant.text == "fallback"_s);
    CHECK(reentrant.contentOrigin() == "site-A"_s && reader->readString("text/plain"_s) == "replacement"_s);
    writer->writeCustomData({ item });
    Reader richOnly(*writer);
    reader->read(richOnly, WebContentReadingPolicy::OnlyRichTextTypes);
    CHECK(richOnly.html == "<b>rich</b>"_s && richOnly.text.isNull());
    writer->writeCustomData({ item });
    writer->writeString("text/uri-list"_s, "https://example.com/"_s);
    CHECK(reader->readOrigin().isNull() && reader->readStringInCustomData("application/x-example"_s).isNull());
    writer->clear("text/html"_s);
    CHECK(reader->readString("text/html"_s).isNull() && reader->readString("text/plain"_s) == "fallback"_s);

    const uint8_t bytes[] = { 0, 45, 255, 0 };
    writer->write(PasteboardBuffer { "image/png"_s, SharedBuffer::create(std::span<const uint8_t>(bytes)) });
    CHECK(reader->fileContentState() == Pasteboard::FileContentState::InMemoryImage);
    FileReader files;
    reader->read(files);
    StaticPasteboard staged;
    FileReader stagedRead;
    Pasteboard& stagedBase = staged;
    stagedBase.read(stagedRead);
    CHECK(!stagedRead.buffer);
    writer->clear();
    CHECK(!files.pathSeen && files.buffer && equalSpans(files.buffer->span(), std::span(bytes)));
    CHECK(reader->fileContentState() == Pasteboard::FileContentState::NoFileOrImageData && !reader->hasData());

    stagedBase.write(PasteboardBuffer { "image/png"_s, files.buffer });
    stagedBase.read(stagedRead);
    CHECK(stagedRead.buffer && equalSpans(stagedRead.buffer->span(), std::span(bytes)) && !reader->hasData());
    staged.writePlainText("private staging"_s, Pasteboard::CannotSmartReplace);
    CHECK(!reader->hasData() && staged.readString("text/plain"_s) == "private staging"_s);

    using Modifier = PlatformEvent::Modifier;
    PlatformKeyboardEvent::setCurrentModifierState({ Modifier::ControlKey, Modifier::ShiftKey });
    CHECK(PlatformKeyboardEvent::currentStateOfModifierKeys().contains(Modifier::ControlKey));
    CHECK(!PlatformKeyboardEvent::currentCapsLockState());
    PlatformKeyboardEvent::setCurrentModifierState({ Modifier::CapsLockKey });
    CHECK(PlatformKeyboardEvent::currentCapsLockState());
    PlatformKeyboardEvent::setCurrentModifierState({ });
    CHECK(PlatformKeyboardEvent::currentStateOfModifierKeys().isEmpty());
    return true;
}

int main()
{
    WTF::initializeMainThread();
    initializeCommonAtomStrings();
    chos_heap_stats_t baseline { };
    for (unsigned round = 0; round < 3; ++round) {
        if (!exercise())
            return 1;
        chos_heap_stats_t stats;
        if (chos_libc_heap_info(&stats))
            return 1;
        std::printf("[VALUES HEAP] round=%u allocations=%zu mapped=%zu\n", round, stats.allocations, stats.mapped_bytes);
        if (round && (stats.allocations != baseline.allocations || stats.mapped_bytes != baseline.mapped_bytes))
            return 1;
        baseline = stats;
    }
    std::puts("[ OK ] native WebCore clipboard origin, reentry, buffers and modifiers passed");
    return 0;
}
