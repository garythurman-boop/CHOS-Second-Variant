/* CHOS local extension metadata, 2026-11-16. MIME knowledge does not assert
 * support for decoding, executing, downloading or displaying any file format. */
#include "config.h"
#include "MIMETypeRegistry.h"

namespace WebCore {

static constexpr TypeExtensionPair types[] {
    { "text/html"_s, "html"_s },
    { "text/html"_s, "htm"_s },
    { "text/css"_s, "css"_s },
    { "text/javascript"_s, "js"_s },
    { "text/javascript"_s, "mjs"_s },
    { "application/json"_s, "json"_s },
    { "text/plain"_s, "txt"_s },
    { "text/plain"_s, "text"_s },
    { "application/xml"_s, "xml"_s },
    { "application/xhtml+xml"_s, "xhtml"_s },
    { "image/svg+xml"_s, "svg"_s },
    { "image/png"_s, "png"_s },
    { "image/jpeg"_s, "jpg"_s },
    { "image/jpeg"_s, "jpeg"_s },
    { "image/gif"_s, "gif"_s },
    { "image/bmp"_s, "bmp"_s },
    { "image/x-icon"_s, "ico"_s },
    { "image/webp"_s, "webp"_s },
    { "image/avif"_s, "avif"_s },
    { "image/jxl"_s, "jxl"_s },
    { "application/pdf"_s, "pdf"_s },
    { "application/wasm"_s, "wasm"_s },
    { "font/ttf"_s, "ttf"_s },
    { "font/otf"_s, "otf"_s },
    { "font/woff"_s, "woff"_s },
    { "font/woff2"_s, "woff2"_s },
};

String MIMETypeRegistry::mimeTypeForExtension(StringView extension)
{
    for (const auto& entry : types) {
        if (equalIgnoringASCIICase(extension, entry.extension))
            return entry.type;
    }
    return emptyString();
}

String MIMETypeRegistry::preferredExtensionForMIMEType(const String& type)
{
    for (const auto& entry : types) {
        if (equalIgnoringASCIICase(type, entry.type))
            return entry.extension;
    }
    return emptyString();
}

Vector<String> MIMETypeRegistry::extensionsForMIMEType(const String& type)
{
    Vector<String> result;
    for (const auto& entry : types) {
        if (equalIgnoringASCIICase(type, entry.type))
            result.append(entry.extension);
    }
    return result;
}

bool MIMETypeRegistry::isApplicationPluginMIMEType(const String&)
{
    // This explicit diagnostic profile has no application plug-in provider.
    return false;
}

} // namespace WebCore
