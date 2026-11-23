#include <source_location>
#include <stdio.h>
#include <string.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#include "line_cases.h"

#define CHECK(x) do { if (!(x)) { printf("[FAIL] source_location line %u: %s\n", __LINE__, #x); return 1; } } while (0)
#define CAPTURE_SOURCE_LOCATION() std::source_location::current()

static constexpr std::source_location capture(
    std::source_location where = std::source_location::current()) noexcept
{
    return where;
}

static constexpr std::source_location forward_location(
    std::source_location where = std::source_location::current()) noexcept
{
    return capture(where);
}

struct Recorded {
    std::source_location where;
    constexpr explicit Recorded(std::source_location value = std::source_location::current()) noexcept
        : where(value) { }
};

static int call_sites()
{
    constexpr std::source_location empty;
    static_assert(empty.line() == 0 && empty.column() == 0);
    CHECK(!strcmp(empty.file_name(), "") && !strcmp(empty.function_name(), ""));
    static_assert(__cpp_lib_source_location >= 201907L);

    constexpr unsigned direct_line = __LINE__ + 1;
    constexpr auto direct = std::source_location::current();
    static_assert(direct.line() == direct_line && direct.column() > 0);
    CHECK(!strcmp(direct.file_name(), __FILE__));
    CHECK(!strcmp(direct.function_name(), __PRETTY_FUNCTION__));

    constexpr unsigned default_line = __LINE__ + 1;
    constexpr auto default_argument = capture();
    static_assert(default_argument.line() == default_line);
    CHECK(!strcmp(default_argument.file_name(), __FILE__));
    CHECK(!strcmp(default_argument.function_name(), __PRETTY_FUNCTION__));

    constexpr unsigned forwarded_line = __LINE__ + 1;
    constexpr auto forwarded = forward_location();
    static_assert(forwarded.line() == forwarded_line);
    CHECK(!strcmp(forwarded.file_name(), __FILE__));
    CHECK(!strcmp(forwarded.function_name(), __PRETTY_FUNCTION__));

    constexpr auto first_column = capture(); constexpr auto second_column = capture();
    static_assert(first_column.line() == second_column.line());
    static_assert(first_column.column() < second_column.column());

    constexpr unsigned macro_line = __LINE__ + 1;
    constexpr auto macro = CAPTURE_SOURCE_LOCATION();
    static_assert(macro.line() == macro_line && macro.column() > 0);
    CHECK(!strcmp(macro.file_name(), __FILE__));
    CHECK(!strcmp(macro.function_name(), __PRETTY_FUNCTION__));

    constexpr unsigned constructor_line = __LINE__ + 1;
    constexpr Recorded constructed;
    static_assert(constructed.where.line() == constructor_line);
    CHECK(!strcmp(constructed.where.function_name(), __PRETTY_FUNCTION__));

    constexpr auto copied = capture(direct);
    static_assert(copied.line() == direct.line() && copied.column() == direct.column());
    CHECK(!strcmp(copied.file_name(), direct.file_name()));
    CHECK(!strcmp(copied.function_name(), direct.function_name()));

    constexpr auto mapped = mapped_location();
    static_assert(mapped.line() == 702 && mapped.column() == 1);
    CHECK(!strcmp(mapped.file_name(), "CHOS/generated-source-location.cpp"));
    CHECK(strstr(mapped.function_name(), "mapped_location()") != nullptr);
    return 0;
}

int main()
{
    CHECK(!call_sites());
#ifdef __CHOS__
    chos_heap_stats_t stats;
    CHECK(!chos_libc_heap_info(&stats) && !stats.allocations && !stats.mapped_bytes);
#endif
    puts("[ OK ] original libc++ source_location call sites,file,line,column,function and generated mappings passed");
    return 0;
}
