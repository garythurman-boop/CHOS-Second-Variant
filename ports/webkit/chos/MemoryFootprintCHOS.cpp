/* Exact owned user-frame charge. Privileged page tables, service-task memory
 * and kernel buffers are excluded from this per-process footprint definition. */
#include "config.h"
#include <wtf/MemoryFootprint.h>
#include <wtf/Assertions.h>
#include <chos/user.h>
#include <limits>
namespace WTF {
size_t memoryFootprint()
{
    chos_resource_info_t info { };
    RELEASE_ASSERT(!chos_resource_info(&info) && info.version == CHOS_RESOURCE_VERSION
        && info.size_bytes == sizeof(info) && (info.flags & CHOS_RESOURCE_MEMORY_VALID)
        && info.resident_bytes <= std::numeric_limits<size_t>::max());
    return static_cast<size_t>(info.resident_bytes);
}
}
