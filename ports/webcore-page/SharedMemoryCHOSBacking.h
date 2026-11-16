/* CHOS local WebCore diagnostic backend, 2026-11-16.
 * Owned in-process bytes only. This is not an IPC handle or an alias mapping. */
#pragma once

#include <wtf/RefPtr.h>
#include <wtf/ThreadSafeRefCounted.h>

namespace WebCore {

class SharedMemoryCHOSBacking final : public ThreadSafeRefCounted<SharedMemoryCHOSBacking> {
public:
    static RefPtr<SharedMemoryCHOSBacking> allocate(size_t);
    ~SharedMemoryCHOSBacking();
    size_t size() const { return m_size; }
    void* data() const { return m_data; }

private:
    SharedMemoryCHOSBacking(void* data, size_t size)
        : m_data(data), m_size(size) { }
    void* m_data;
    size_t m_size;
};

} // namespace WebCore
