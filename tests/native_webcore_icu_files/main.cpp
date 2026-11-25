#include "BrowserICUData.h"
#include <chos/libc.h>
#include <cstdio>
#include <cstring>
#include <unicode/ubrk.h>
#include <unicode/ucol.h>
#include <unicode/ulocdata.h>
#include <unicode/unorm2.h>
#include <unicode/unum.h>
#include <unicode/ures.h>

#define CHECK(test) do { if (!(test)) { std::printf("[FAIL] browser ICU line %u: %s\n", __LINE__, #test); return 1; } } while (0)

int main(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "refuse")) {
        chos_heap_stats_t before, after;
        CHECK(!chos_libc_heap_info(&before));
        CHECK(chos_browser_icu_init() < 0 && chos_browser_icu_init() < 0);
        CHECK(!chos_libc_heap_info(&after));
        CHECK(before.allocations == after.allocations && before.mapped_bytes == after.mapped_bytes);
        std::puts("[ OK ] incomplete or altered ICU packages refused and released");
        return 0;
    }
    CHECK(!chos_browser_icu_init() && !chos_browser_icu_init());
    UErrorCode status = U_ZERO_ERROR;
    UVersionInfo cldr;
    ulocdata_getCLDRVersion(cldr, &status);
    CHECK(U_SUCCESS(status) && cldr[0] != 0);
    UChar decomposed[] = { 'e', 0x301 };
    UChar result[64];
    auto* normalizer = unorm2_getNFCInstance(&status);
    CHECK(U_SUCCESS(status) && unorm2_normalize(normalizer, decomposed, 2, result, 64, &status) == 1 && result[0] == 0xe9);
    auto* swedish = ucol_open("sv", &status);
    UChar z = 'z', aRing = 0xe5;
    CHECK(U_SUCCESS(status) && swedish && ucol_strcoll(swedish, &z, 1, &aRing, 1) == UCOL_LESS);
    ucol_close(swedish);
    auto* arabic = unum_open(UNUM_DECIMAL, nullptr, 0, "ar_EG@numbers=arab", nullptr, &status);
    CHECK(U_SUCCESS(status) && arabic && unum_format( arabic, 123, result, 64, nullptr, &status) == 3);
    CHECK(result[0] == 0x661 && result[1] == 0x662 && result[2] == 0x663);
    unum_close(arabic);
    const UChar text[] = { 'a', 0x301, 'b' };
    auto* breaks = ubrk_open(UBRK_CHARACTER, "th", text, 3, &status);
    CHECK(U_SUCCESS(status) && breaks && ubrk_first(breaks) == 0 && ubrk_next(breaks) == 2 && ubrk_next(breaks) == 3);
    ubrk_close(breaks);
    const char* locales[] = { "fr", "ru", "ja", "zh_Hant", "fa", "bn", "pt", "ta" };
    for (auto* locale : locales) {
        status = U_ZERO_ERROR;
        auto* bundle = ures_openDirect(nullptr, locale, &status);
        CHECK(U_SUCCESS(status) && bundle && ures_getSize(bundle) > 0);
        ures_close(bundle);
    }
    std::puts("[ OK ] external ICU data normalization collation numbers breaks locales passed");
    return 0;
}
