/* Explicit CHOS clocks; no foreign OS/POSIX feature identity is claimed. */
#include <__chrono/system_clock.h>
#include <__chrono/steady_clock.h>
#include <__system_error/system_error.h>
#include <cerrno>
#include <time.h>
_LIBCPP_BEGIN_NAMESPACE_STD
namespace chrono {
system_clock::time_point system_clock::now() noexcept{
 timespec t;if(::clock_gettime(CLOCK_REALTIME,&t))__throw_system_error(errno,"native real-time clock unavailable");
 return time_point(seconds(t.tv_sec)+microseconds(t.tv_nsec/1000));
}
time_t system_clock::to_time_t(const time_point& t) noexcept{return time_t(duration_cast<seconds>(t.time_since_epoch()).count());}
system_clock::time_point system_clock::from_time_t(time_t t) noexcept{return time_point(seconds(t));}
steady_clock::time_point steady_clock::now() noexcept{
 timespec t;if(::clock_gettime(CLOCK_MONOTONIC,&t))__throw_system_error(errno,"native monotonic clock unavailable");
 return time_point(seconds(t.tv_sec)+nanoseconds(t.tv_nsec));
}
}
_LIBCPP_END_NAMESPACE_STD
