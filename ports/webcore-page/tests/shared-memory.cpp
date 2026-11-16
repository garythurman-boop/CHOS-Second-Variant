#include "config.h"
#include "SharedMemory.h"
#include "SharedBuffer.h"
#include <wtf/Threading.h>
#include <chos/libc.h>
#include <cstdio>
#include <cstring>
#include <pthread.h>

using namespace WebCore;
#define CHECK(x) do { if (!(x)) { std::printf("[FAIL] WebCore local memory line %u\n", __LINE__); std::fflush(stdout); return false; } } while (0)

static bool lifetime(size_t size)
{
    RefPtr memory = SharedMemory::allocate(size);
    CHECK(memory && memory->size() == size);
    for (auto byte : memory->span())
        CHECK(!byte);
    auto writable = memory->mutableSpan();
    for (size_t i = 0; i < size; ++i)
        writable[i] = (i * 131 + 17) & 255;
    CHECK(!memory->createHandle(SharedMemory::Protection::ReadOnly));
    auto handle = memory->createHandle(SharedMemory::Protection::ReadWrite);
    CHECK(handle);
    SharedMemoryHandle retained(*handle);
    CHECK(!SharedMemory::map(WTF::move(*handle), SharedMemory::Protection::ReadOnly));
    CHECK(!SharedMemory::map(WTF::move(*handle), SharedMemory::Protection::ReadWrite, SharedMemory::CopyOnWrite::Yes));
    auto alias = SharedMemory::map(WTF::move(*handle), SharedMemory::Protection::ReadWrite);
    CHECK(alias && alias->span().data() == memory->span().data());
    CHECK(!SharedMemory::map(WTF::move(*handle), SharedMemory::Protection::ReadWrite));
    alias->mutableSpan()[size - 1] = 93;
    CHECK(memory->span()[size - 1] == 93);
    auto view = memory->createSharedBuffer(size);
    memory = nullptr;
    alias = nullptr;
    auto mapped = SharedMemory::map(WTF::move(retained), SharedMemory::Protection::ReadWrite);
    CHECK(mapped && mapped->span().data() == view->span().data());
    mapped = nullptr;
    CHECK(view->size() == size && view->span()[size - 1] == 93);
    for (size_t i = 0; i + 1 < size; ++i)
        CHECK(view->span()[i] == ((i * 131 + 17) & 255));
    return true;
}

static bool failure_and_ipc()
{
    CHECK(!SharedMemory::allocate(0));
    volatile size_t oversized = SIZE_MAX;
    CHECK(!SharedMemory::allocate(oversized));
    auto backing = SharedMemoryCHOSBacking::allocate(31);
    CHECK(backing);
    SharedMemoryHandle forged(SharedMemoryHandle::Type(backing), 32);
    CHECK(!SharedMemory::map(WTF::move(forged), SharedMemory::Protection::ReadWrite));
    const uint8_t text[] { 1, 2, 3, 4, 5 };
    CHECK(!SharedMemoryHandle::createCopy(text, SharedMemory::Protection::ReadOnly));
    CHECK(!SharedMemoryHandle::createVMShare(text, SharedMemory::Protection::ReadWrite));
    CHECK(!SharedMemoryHandle::createVMCopy(text, SharedMemory::Protection::ReadWrite));
    auto copy = SharedMemoryHandle::createCopy(text, SharedMemory::Protection::ReadWrite);
    CHECK(copy);
    auto memory = SharedMemory::map(WTF::move(*copy), SharedMemory::Protection::ReadWrite);
    CHECK(memory && !std::memcmp(text, memory->span().data(), sizeof(text)));
    auto small = SharedBuffer::create(text);
    auto decoded = FragmentedSharedBuffer::fromIPCData(small->toIPCData());
    CHECK(decoded && (*decoded)->size() == sizeof(text));
    auto large = SharedMemory::allocate(8192);
    CHECK(large);
    auto input = large->createSharedBuffer(8192);
    // The original >page IPC caller requests a ReadOnly handle. Its real
    // failure is retained; there is no successful cross-process fallback.
    CHECK(!FragmentedSharedBuffer::fromIPCData(input->toIPCData()));
    return true;
}

struct Job { RefPtr<SharedMemory> memory; unsigned index; };
static void* worker(void* opaque)
{
    auto& job = *static_cast<Job*>(opaque);
    for (unsigned round = 0; round < 8; ++round) {
        auto handle = job.memory->createHandle(SharedMemory::Protection::ReadWrite);
        if (!handle)
            return reinterpret_cast<void*>(1);
        auto alias = SharedMemory::map(WTF::move(*handle), SharedMemory::Protection::ReadWrite);
        if (!alias)
            return reinterpret_cast<void*>(1);
        // Each worker owns a disjoint byte range; retained handle/refcount
        // operations are concurrent but the actual data accesses do not race.
        auto bytes = alias->mutableSpan().subspan(job.index * 16384, 16384);
        for (size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = (i + job.index * 17 + round) & 255;
    }
    return nullptr;
}

static bool threads()
{
    auto memory = SharedMemory::allocate(3 * 16384);
    CHECK(memory);
    Job jobs[3];
    pthread_t workers[3];
    pthread_attr_t attr;
    CHECK(!pthread_attr_init(&attr) && !pthread_attr_setstacksize(&attr, 128 * 1024));
    for (unsigned i = 0; i < 3; ++i) {
        jobs[i] = { memory, i };
        CHECK(!pthread_create(&workers[i], &attr, worker, &jobs[i]));
    }
    CHECK(!pthread_attr_destroy(&attr));
    for (unsigned i = 0; i < 3; ++i) {
        void* result;
        CHECK(!pthread_join(workers[i], &result) && !result);
        for (unsigned n = 0; n < 16384; ++n)
            CHECK(memory->span()[i * 16384 + n] == ((n + i * 17 + 7) & 255));
    }
    return true;
}

static bool run()
{
    for (size_t size : { 1u, 4095u, 4096u, 65539u, 1048593u })
        CHECK(lifetime(size));
    CHECK(failure_and_ipc() && threads());
    return true;
}

int main()
{
    WTF::initialize();
    if (!run())
        return 1;
    chos_heap_stats_t warm, after;
    if (chos_libc_heap_info(&warm))
        return 1;
    for (unsigned round = 0; round < 3; ++round)
        if (!run())
            return 1;
    if (chos_libc_heap_info(&after))
        return 1;
    std::printf("[WEBCORE LOCAL MEMORY HEAP] allocations=%lu mapped=%lu warm_allocations=%lu warm_mapped=%lu\n",
        (unsigned long)after.allocations, (unsigned long)after.mapped_bytes,
        (unsigned long)warm.allocations, (unsigned long)warm.mapped_bytes);
    if (after.allocations != warm.allocations || after.mapped_bytes != warm.mapped_bytes)
        return 1;
    std::puts("[ OK ] original WebCore local memory ownership,aliasing,refusal and threads passed");
    return 0;
}
