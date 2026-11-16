/* CHOS local WebCore diagnostic backend, 2026-11-16.
 * Retained ReadWrite handles share actual process-owned bytes. ReadOnly,
 * copy-on-write and cross-process alias mappings are unavailable and fail.
 * These are ordinary engine heap allocations, never exported kernel handles. */
#include "config.h"
#include "SharedMemory.h"
#include "SharedMemoryCHOSBacking.h"
#include <cstdlib>

namespace WebCore {

RefPtr<SharedMemoryCHOSBacking> SharedMemoryCHOSBacking::allocate(size_t size)
{
    if (!size)
        return nullptr;
    // The selected native heap enforces its actual mapping/quota limits and
    // overflow handling. calloc also supplies the zero-initialized contract.
    void* data = std::calloc(size, 1);
    if (!data)
        return nullptr;
    return adoptRef(new SharedMemoryCHOSBacking(data, size));
}

SharedMemoryCHOSBacking::~SharedMemoryCHOSBacking()
{
    std::free(m_data);
}

RefPtr<SharedMemory> SharedMemory::allocate(size_t size)
{
    auto backing = SharedMemoryCHOSBacking::allocate(size);
    if (!backing)
        return nullptr;
    RefPtr memory = adoptRef(new SharedMemory());
    memory->m_data = backing->data();
    memory->m_size = size;
    memory->m_chosBacking = WTF::move(backing);
    return memory;
}

RefPtr<SharedMemory> SharedMemory::map(Handle&& handle, Protection protection, CopyOnWrite copyOnWrite)
{
    // CHOS currently has no distinct read-only alias of a writable allocation.
    // Refuse that capability rather than silently retaining write access or
    // changing shared updates into an undocumented private snapshot.
    if (protection != Protection::ReadWrite || copyOnWrite != CopyOnWrite::No)
        return nullptr;
    if (!handle.m_handle || !handle.m_size || handle.m_size != handle.m_handle->size())
        return nullptr;
    RefPtr memory = adoptRef(new SharedMemory());
    memory->m_size = handle.m_size;
    memory->m_chosBacking = WTF::move(handle.m_handle);
    memory->m_data = memory->m_chosBacking->data();
    return memory;
}

SharedMemory::~SharedMemory() = default;

auto SharedMemory::createHandle(Protection protection) -> std::optional<Handle>
{
    if (protection != Protection::ReadWrite)
        return std::nullopt;
    return Handle(Handle::Type(m_chosBacking), m_size);
}

} // namespace WebCore
