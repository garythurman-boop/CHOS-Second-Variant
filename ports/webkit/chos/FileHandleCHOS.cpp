/* Native WTF file handles. CHFS uses owned working copies with explicit durable
 * commits; read handles retain immutable snapshots. No POSIX fd translation. */
#include "config.h"
#include <wtf/FileHandle.h>
#include <wtf/FileSystem.h>
#include <wtf/MappedFileData.h>
#include <chos/user.h>
#include <cerrno>
#include <algorithm>
#include <limits>

namespace WTF::FileSystemImpl {
static bool metadata(int fd, chos_file_metadata_t& out)
{
    auto result = chos_file_metadata(fd, &out);
    if (result < 0) {
        errno = -result;
        return false;
    }
    if (out.version != CHOS_FILE_METADATA_VERSION || out.size_bytes != sizeof(out)) {
        errno = EIO;
        return false;
    }
    return true;
}

std::optional<uint64_t> FileHandle::read(std::span<uint8_t> bytes)
{
    if (!m_handle || m_writeOnly) { errno = EBADF; return { }; }
    size_t done = 0;
    // Generic appendFileContents treats a short read as EOF. Hide the native
    // transfer limit, not actual EOF/errors, from these engine callers.
    do {
        size_t amount = std::min<size_t>(256, bytes.size() - done);
        auto result = chos_read(*m_handle, done ? bytes.data() + done : bytes.data(), amount);
        if (result < 0) { errno = -result; return done ? std::optional<uint64_t>(done) : std::nullopt; }
        if (static_cast<size_t>(result) > amount) { errno = EIO; return { }; }
        done += result;
        if (!result)
            break;
    } while (done < bytes.size());
    return done;
}

std::optional<uint64_t> FileHandle::write(std::span<const uint8_t> bytes)
{
    if (!m_handle) { errno = EBADF; return { }; }
    size_t done = 0;
    do {
        size_t amount = std::min<size_t>(256, bytes.size() - done);
        auto result = chos_file_write(*m_handle, done ? bytes.data() + done : bytes.data(), amount);
        if (result < 0) { errno = -result; return done ? std::optional<uint64_t>(done) : std::nullopt; }
        if (static_cast<size_t>(result) > amount) { errno = EIO; return { }; }
        done += result;
        if (!result)
            break;
    } while (done < bytes.size());
    return done;
}

bool FileHandle::truncate(int64_t length)
{
    if (!m_handle) { errno = EBADF; return false; }
    if (length < 0) { errno = EINVAL; return false; }
    auto result = chos_file_truncate(*m_handle, length);
    if (result < 0) errno = -result;
    return !result;
}
bool FileHandle::flush()
{
    if (!m_handle) { errno = EBADF; return false; }
    auto result = chos_file_flush(*m_handle);
    if (result < 0) errno = -result;
    return !result;
}
std::optional<uint64_t> FileHandle::seek(int64_t offset, FileSeekOrigin origin)
{
    if (!m_handle) { errno = EBADF; return { }; }
    unsigned nativeOrigin;
    switch (origin) {
    case FileSeekOrigin::Beginning: nativeOrigin = 0; break;
    case FileSeekOrigin::Current: nativeOrigin = 1; break;
    case FileSeekOrigin::End: nativeOrigin = 2; break;
    default: errno = EINVAL; return { };
    }
    auto result = chos_seek(*m_handle, offset, nativeOrigin);
    if (result < 0) { errno = -result; return { }; }
    return result;
}
std::optional<PlatformFileID> FileHandle::id()
{
    if (!m_handle) { errno = EBADF; return { }; }
    chos_file_metadata_t info;
    if (!metadata(*m_handle, info) || !(info.flags & CHOS_FILE_META_ID_VALID))
        return { };
    return info.identity;
}
std::optional<uint64_t> FileHandle::size()
{
    if (!m_handle) { errno = EBADF; return { }; }
    chos_file_metadata_t info;
    if (!metadata(*m_handle, info) || !(info.flags & CHOS_FILE_META_SIZE_VALID))
        return { };
    return info.length;
}
void FileHandle::close()
{
    if (auto handle = std::exchange(m_handle, std::nullopt)) {
        auto result = chos_close(*handle);
        if (result < 0) {
            // Native close retains dirty data on failure. A destructor cannot
            // retain the handle: report the loss, then explicitly discard it.
            static constexpr char message[] = "[WTF FILE ERROR] close failed; attempting explicit discard of retained handle\n";
            chos_write(message, sizeof(message) - 1);
            chos_file_abort(*handle);
            errno = -result;
        }
    }
}
std::optional<MappedFileData> FileHandle::map(MappedFileMode mode, FileOpenMode access)
{
    if (!m_handle) { errno = EBADF; return { }; }
    if (m_writeOnly) { errno = EBADF; return { }; }
    // Native mappings are private anonymous pages. A copied private file view
    // has real private semantics; never advertise shared writeback/coherence.
    if (mode != MappedFileMode::Private || (access != FileOpenMode::Read && access != FileOpenMode::ReadWrite)) {
        errno = ENOTSUP;
        return { };
    }
    auto length = size();
    if (!length)
        return { };
    if (!*length)
        return MappedFileData::emptyFile();
    if (*length > std::numeric_limits<size_t>::max()) { errno = EOVERFLOW; return { }; }
    auto data = MmapSpan<uint8_t>::tryAlloc(*length);
    if (!data)
        return { };
    size_t done = 0;
    auto span = data.mutableSpan();
    while (done < span.size()) {
        size_t amount = std::min<size_t>(256, span.size() - done);
        auto result = chos_read_at(*m_handle, span.data() + done, amount, done);
        if (result <= 0 || static_cast<size_t>(result) > amount) {
            errno = result < 0 ? -result : EIO;
            return { };
        }
        done += result;
    }
    if (access == FileOpenMode::Read && mprotect(span.data(), span.size(), PROT_READ))
        return { };
    return MappedFileData { WTF::move(data) };
}
}
