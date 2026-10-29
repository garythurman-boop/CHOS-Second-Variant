/* CHOS native port of WTF CPUTime. Measurements come from the dispatched task
 * and its generation-owned process aggregate, never elapsed wall time. */
#include "config.h"
#include <wtf/CPUTime.h>
#include <chos/user.h>
namespace WTF {
static bool cpuSnapshot(chos_resource_info_t& info)
{
    return !chos_resource_info(&info) && info.version == CHOS_RESOURCE_VERSION
        && info.size_bytes == sizeof(info) && (info.flags & CHOS_RESOURCE_CPU_VALID)
        && info.counter_frequency;
}
std::optional<CPUTime> CPUTime::get()
{
    chos_resource_info_t info { };
    if (!cpuSnapshot(info))
        return std::nullopt;
    double frequency = static_cast<double>(info.counter_frequency);
    return CPUTime { MonotonicTime::now(), Seconds(static_cast<double>(info.process_user_ticks) / frequency),
        Seconds(static_cast<double>(info.process_system_ticks) / frequency) };
}
Seconds CPUTime::forCurrentThread()
{
    chos_resource_info_t info { };
    RELEASE_ASSERT(cpuSnapshot(info));
    // Convert separately to avoid integer addition overflow.
    return Seconds(static_cast<double>(info.thread_user_ticks) / static_cast<double>(info.counter_frequency)
        + static_cast<double>(info.thread_system_ticks) / static_cast<double>(info.counter_frequency));
}
}
