/* Native CHFS/ROMFS filesystem binding. No host POSIX structures or libc file
 * descriptor emulation. Format/service limitations fail explicitly below. */
#include "config.h"
#include <wtf/FileSystem.h>
#include <wtf/FileHandle.h>
#include <wtf/Function.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/WTFString.h>
#include <chos/user.h>
#include <cerrno>
#include <cstring>
#include <limits>

namespace WTF::FileSystemImpl {
static bool succeeded(long result)
{
    if (result < 0) { errno = -result; return false; }
    return true;
}
static bool pathBytes(const String& path, CString& bytes)
{
    bytes = path.utf8();
    if (bytes.isNull() || !bytes.length() || bytes.length() >= CHOS_FILE_PATH_CAPACITY
        || strlen(bytes.data()) != bytes.length() || bytes.data()[0] != '/') {
        errno = EINVAL;
        return false;
    }
    return true;
}
static std::optional<chos_file_metadata_t> pathInfo(const String& path)
{
    CString bytes;
    if (!pathBytes(path, bytes))
        return { };
    chos_file_metadata_t info { };
    if (!succeeded(chos_path_metadata(bytes.data(), &info)))
        return { };
    return info;
}
CString fileSystemRepresentation(const String& path) { return path.utf8(); }
String stringFromFileSystemRepresentation(const char* path) { return path ? String::fromUTF8(path) : String(); }
FileHandle openFile(const String& path, FileOpenMode mode, FileAccessPermission permission, OptionSet<FileLockMode> lockMode, bool exclusive)
{
    // Reject unsupported locks before create/truncate can alter the namespace.
    if (lockMode) { errno = ENOTSUP; return { }; }
    if (permission != FileAccessPermission::User && permission != FileAccessPermission::All) { errno = EINVAL; return { }; }
    CString bytes;
    if (!pathBytes(path, bytes))
        return { };
    long fd;
    switch (mode) {
    case FileOpenMode::Read:
        if (exclusive) { errno = ENOTSUP; return { }; }
        fd = chos_open(bytes.data());
        break;
    case FileOpenMode::ReadWrite:
    case FileOpenMode::Truncate: {
        unsigned flags = CHOS_FILE_WRITE_CREATE;
        if (exclusive) flags |= CHOS_FILE_WRITE_EXCLUSIVE;
        if (mode == FileOpenMode::Truncate) flags |= CHOS_FILE_WRITE_TRUNCATE;
        fd = chos_open_write(bytes.data(), flags, CHOS_FILE_WRITE_MAX_BYTES);
        break;
    }
    default: errno = EINVAL; return { };
    }
    if (!succeeded(fd))
        return { };
    return mode == FileOpenMode::Truncate ? FileHandle::adoptWriteOnly(fd) : FileHandle::adopt(fd);
}
bool fileExists(const String& path) { return !!pathInfo(path); }
std::optional<uint64_t> fileSize(const String& path)
{
    auto info = pathInfo(path);
    if (!info || !(info->flags & CHOS_FILE_META_SIZE_VALID))
        return { };
    return info->length;
}
std::optional<FileType> fileType(const String& path)
{
    auto info = pathInfo(path);
    if (!info)
        return { };
    return info->flags & CHOS_FILE_META_DIRECTORY ? FileType::Directory : FileType::Regular;
}
std::optional<FileType> fileTypeFollowingSymlinks(const String& path) { return fileType(path); }
bool fileIDsAreEqual(std::optional<PlatformFileID> a, std::optional<PlatformFileID> b) { return a == b; }
bool deleteFile(const String& path)
{
    CString bytes;
    return pathBytes(path, bytes) && succeeded(chos_unlink(bytes.data()));
}
bool deleteEmptyDirectory(const String& path)
{
    CString bytes;
    return pathBytes(path, bytes) && succeeded(chos_rmdir(bytes.data()));
}
bool moveFile(const String& from, const String& to)
{
    CString a, b;
    return pathBytes(from, a) && pathBytes(to, b) && succeeded(chos_rename(a.data(), b.data()));
}
std::optional<int32_t> getFileDeviceId(const String& path)
{
    auto info = pathInfo(path);
    if (!info)
        return { };
    if (info->identity.source != CHOS_FILE_SOURCE_CHFS || !(info->flags & CHOS_FILE_META_ID_VALID)) { errno = ENOTSUP; return { }; }
    if (info->identity.mount > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) { errno = EOVERFLOW; return { }; }
    return static_cast<int32_t>(info->identity.mount);
}
std::optional<uint32_t> volumeFileBlockSize(const String& path)
{
    auto info = pathInfo(path);
    if (!info)
        return { };
    if (info->identity.source != CHOS_FILE_SOURCE_CHFS) { errno = ENOTSUP; return { }; }
    return 512; // Actual CHFS allocation/extent unit, including NVMe-backed CHFS.
}
// CHFS has no stored timestamps, symbolic links or hard links. Capacity/free
// statistics are not exported by the current native user ABI. Do not invent
// these values from uptime, revisions, RAM or host filesystem information.
std::optional<WallTime> fileCreationTime(const String&) { errno = ENOTSUP; return { }; }
std::optional<WallTime> fileModificationTime(const String&) { errno = ENOTSUP; return { }; }
bool updateFileModificationTime(const String&) { errno = ENOTSUP; return false; }
std::optional<uint64_t> volumeFreeSpace(const String&) { errno = ENOTSUP; return { }; }
std::optional<uint64_t> volumeCapacity(const String&) { errno = ENOTSUP; return { }; }
bool createSymbolicLink(const String&, const String&) { errno = ENOTSUP; return false; }
bool hardLink(const String&, const String&) { errno = ENOTSUP; return false; }
std::optional<uint64_t> hardLinkCount(const String& path)
{
    auto info = pathInfo(path);
    if (!info)
        return { };
    if (!(info->flags & CHOS_FILE_META_REGULAR)) { errno = ENOTSUP; return { }; }
    return 1; // Each native regular-file object has one namespace link.
}

String pathByAppendingComponent(StringView base, StringView component)
{
    if (component.startsWith('/'))
        return component.toString();
    if (base.isEmpty() || base.endsWith('/'))
        return makeString(base, component);
    return makeString(base, '/', component);
}
String pathByAppendingComponents(StringView base, std::span<const StringView> components)
{
    String result = base.toString();
    for (auto component : components)
        result = pathByAppendingComponent(result, component);
    return result;
}
String pathFileName(const String& path)
{
    unsigned first = path.length();
    while (first && path[first - 1] != '/') --first;
    return path.substring(first);
}
String parentPath(const String& path)
{
    if (path.isEmpty())
        return { };
    unsigned end = path.length();
    while (end && path[end - 1] != '/') --end;
    while (end > 1 && path[end - 1] == '/') --end;
    return path.left(end);
}
String lexicallyNormal(const String& path)
{
    bool absolute = path.startsWith('/');
    Vector<StringView> parts;
    StringView view(path);
    unsigned first = 0;
    while (first < view.length()) {
        while (first < view.length() && view[first] == '/') ++first;
        unsigned end = first;
        while (end < view.length() && view[end] != '/') ++end;
        auto part = view.substring(first, end - first);
        first = end;
        if (part.isEmpty() || part == "."_s)
            continue;
        if (part == ".."_s) {
            if (!parts.isEmpty() && parts.last() != ".."_s) { parts.removeLast(); continue; }
            if (absolute) continue;
        }
        parts.append(part);
    }
    StringBuilder result;
    if (absolute) result.append('/');
    for (unsigned i = 0; i < parts.size(); ++i) {
        if (i) result.append('/');
        result.append(parts[i]);
    }
    if (!absolute && parts.isEmpty() && !path.isEmpty()) result.append('.');
    return result.toString();
}
bool isAncestor(const String& ancestor, const String& child)
{
    auto a = lexicallyNormal(ancestor);
    auto c = lexicallyNormal(child);
    if (a.isEmpty() || c.length() <= a.length())
        return false;
    for (unsigned i = 0; i < a.length(); ++i) {
        auto x = a[i], y = c[i];
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return a == "/"_s || c[a.length()] == '/';
}
bool isHiddenFile(const String& path) { return pathFileName(path).startsWith('.'); }
String realPath(const String& path)
{
    auto normalized = lexicallyNormal(path);
    return fileExists(normalized) ? normalized : path;
}
bool makeAllDirectories(const String& path)
{
    CString bytes;
    if (!pathBytes(path, bytes))
        return false;
    // Validate the complete path before creating any prefix.
    auto length = bytes.length();
    if (length == 1)
        return true;
    if (length >= 48) { errno = ENAMETOOLONG; return false; }
    unsigned component = 0;
    for (unsigned i = 1; i <= length; ++i) {
        unsigned char c = i == length ? '/' : bytes.data()[i];
        if (c == '/') {
            if (!component || (component == 1 && bytes.data()[i - 1] == '.')
                || (component == 2 && bytes.data()[i - 1] == '.' && bytes.data()[i - 2] == '.')) { errno = EINVAL; return false; }
            component = 0;
        } else {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) { errno = EINVAL; return false; }
            ++component;
        }
    }
    for (unsigned end = 1; end <= length; ++end) {
        if (end != length && bytes.data()[end] != '/')
            continue;
        auto prefix = path.left(end);
        auto info = pathInfo(prefix);
        if (info) {
            if (!(info->flags & CHOS_FILE_META_DIRECTORY)) { errno = ENOTDIR; return false; }
            continue;
        }
        if (errno != ENOENT)
            return false;
        CString prefixBytes;
        if (!pathBytes(prefix, prefixBytes))
            return false;
        auto result = chos_mkdir(prefixBytes.data());
        if (result == -CHOS_ERRNO_EEXIST) {
            info = pathInfo(prefix);
            if (info && info->flags & CHOS_FILE_META_DIRECTORY)
                continue;
        }
        if (!succeeded(result))
            return false;
    }
    return true;
}

struct DirectoryEntry { String path; uint64_t bytes; FileType type; };
static std::optional<Vector<DirectoryEntry>> directoryEntries(const String& path)
{
    CString bytes;
    if (!pathBytes(path, bytes))
        return { };
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        Vector<DirectoryEntry> result;
        chos_directory_page_t page { };
        page.version = CHOS_DIRECTORY_VERSION;
        page.size_bytes = sizeof(page);
        page.flags = CHOS_DIRECTORY_START;
        bool restart = false;
        do {
            auto error = chos_directory_page(bytes.data(), &page);
            if (error == -CHOS_ERRNO_ESTALE) { restart = true; break; }
            if (!succeeded(error))
                return { };
            if (page.version != CHOS_DIRECTORY_VERSION || page.size_bytes != sizeof(page) || page.count > CHOS_DIRECTORY_PAGE_ENTRIES
                || page.index != result.size() + page.count || page.index > page.total) { errno = EIO; return { }; }
            if (!result.tryReserveCapacity(page.total)) { errno = ENOMEM; return { }; }
            for (unsigned i = 0; i < page.count; ++i) {
                auto& entry = page.entries[i];
                auto child = String::fromUTF8(entry.path);
                if (child.isNull()) { errno = EILSEQ; return { }; }
                result.append(DirectoryEntry { WTF::move(child), entry.size_bytes,
                    entry.flags & CHOS_FILE_FLAG_DIRECTORY ? FileType::Directory : FileType::Regular });
            }
        } while (!(page.flags & CHOS_DIRECTORY_END));
        if (!restart)
            return result;
    }
    errno = EAGAIN;
    return { };
}
Vector<String> listDirectory(const String& path)
{
    Vector<String> result;
    auto entries = directoryEntries(path);
    if (!entries)
        return result;
    for (auto& entry : *entries)
        result.append(pathFileName(entry.path));
    return result;
}
void traverseDirectory(const String& path, NOESCAPE const Function<void(const String&, FileType)>& function)
{
    auto entries = directoryEntries(path);
    if (!entries)
        return;
    for (auto& entry : *entries)
        function(pathFileName(entry.path), entry.type);
}
static std::optional<uint64_t> directoryBytes(const String& path, unsigned depth)
{
    if (depth > 32) { errno = EOVERFLOW; return { }; }
    auto entries = directoryEntries(path);
    if (!entries)
        return { };
    uint64_t total = 0;
    for (auto& entry : *entries) {
        auto amount = entry.type == FileType::Directory ? directoryBytes(entry.path, depth + 1) : std::optional<uint64_t>(entry.bytes);
        if (!amount)
            return { };
        if (*amount > UINT64_MAX - total) { errno = EOVERFLOW; return { }; }
        total += *amount;
    }
    return total;
}
std::optional<uint64_t> directorySize(const String& path) { return directoryBytes(path, 0); }
static bool removeDirectory(const String& path, unsigned depth)
{
    if (depth > 32 || path == "/"_s) { errno = EINVAL; return false; }
    auto entries = directoryEntries(path);
    if (!entries)
        return false;
    for (auto& entry : *entries) {
        if (entry.type == FileType::Directory ? !removeDirectory(entry.path, depth + 1) : !deleteFile(entry.path))
            return false;
    }
    return deleteEmptyDirectory(path);
}
bool deleteNonEmptyDirectory(const String& path) { return removeDirectory(path, 0); }
static bool copyFileImpl(const String& destination, const String& source, bool exclusive)
{
    auto input = openFile(source, FileOpenMode::Read);
    if (!input)
        return false;
    auto info = pathInfo(destination);
    if (exclusive && info) { errno = EEXIST; return false; }
    if (info && info->flags & CHOS_FILE_META_ID_VALID && input.id() == info->identity) { errno = EINVAL; return false; }
    if (!info && errno != ENOENT)
        return false;
    auto output = openFile(destination, FileOpenMode::Truncate, FileAccessPermission::User, { }, exclusive);
    auto length = input.size();
    if (!output || !length)
        return false;
    uint64_t done = 0;
    std::array<uint8_t, 4096> buffer;
    while (done < *length) {
        size_t amount = std::min<uint64_t>(buffer.size(), *length - done);
        auto read = input.read(std::span<uint8_t>(buffer).first(amount));
        if (!read || *read != amount || output.write(std::span<const uint8_t>(buffer).first(amount)) != read)
            return false;
        done += *read;
    }
    return output.flush();
}
bool copyFile(const String& destination, const String& source) { return copyFileImpl(destination, source, false); }
bool hardLinkOrCopyFile(const String& source, const String& destination) { return copyFileImpl(destination, source, true); }

static String randomName(StringView prefix, StringView suffix, const String& directory)
{
    std::array<uint8_t, 8> entropy;
    auto received = chos_random_bytes(entropy.data(), entropy.size());
    if (received != static_cast<long>(entropy.size())) { errno = received < 0 ? -received : EAGAIN; return { }; }
    constexpr char digits[] = "0123456789ABCDEF";
    std::array<char, 16> text;
    for (unsigned i = 0; i < entropy.size(); ++i) { text[2*i] = digits[entropy[i] >> 4]; text[2*i+1] = digits[entropy[i] & 15]; }
    return pathByAppendingComponent(directory, makeString(prefix, std::span<const char>(text), suffix));
}
static bool temporaryPart(StringView part)
{
    for (auto c : part.codeUnits()) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) {
            errno = EINVAL;
            return false;
        }
    }
    return true;
}
std::pair<String, FileHandle> openTemporaryFile(StringView prefix, StringView suffix, const String& temporaryDirectory)
{
    if (!temporaryPart(prefix) || !temporaryPart(suffix))
        return { String(), FileHandle() };
    String directory = temporaryDirectory.isEmpty() ? "/TMP"_s : temporaryDirectory;
    if (directory.length() + 1 + prefix.length() + 16 + suffix.length() >= 48) { errno = ENAMETOOLONG; return { String(), FileHandle() }; }
    if (!makeAllDirectories(directory))
        return { String(), FileHandle() };
    for (unsigned i = 0; i < 32; ++i) {
        auto name = randomName(prefix, suffix, directory);
        if (name.isNull())
            break;
        auto handle = openFile(name, FileOpenMode::ReadWrite, FileAccessPermission::User, { }, true);
        if (handle)
            return { WTF::move(name), WTF::move(handle) };
        if (errno != EEXIST)
            break;
    }
    return { String(), FileHandle() };
}
String createTemporaryFile(StringView prefix, StringView suffix)
{
    auto [path, handle] = openTemporaryFile(prefix, suffix);
    return path;
}
String createTemporaryDirectory()
{
    if (!makeAllDirectories("/TMP"_s))
        return { };
    for (unsigned i = 0; i < 32; ++i) {
        auto path = randomName({ }, { }, "/TMP"_s);
        CString bytes;
        if (path.isNull() || !pathBytes(path, bytes))
            return { };
        auto result = chos_mkdir(bytes.data());
        if (succeeded(result))
            return path;
        if (result != -CHOS_ERRNO_EEXIST)
            return { };
    }
    errno = EEXIST;
    return { };
}
FileHandle createDumpFile(StringView filename, StringView extension, StringView path)
{
    if (!temporaryPart(filename) || !temporaryPart(extension))
        return { };
    if (path.isEmpty()) {
        auto [unused, handle] = openTemporaryFile(filename, extension);
        return WTF::move(handle);
    }
    return openFile(pathByAppendingComponent(path, makeString(filename, extension)), FileOpenMode::ReadWrite);
}
}
