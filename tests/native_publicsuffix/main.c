#include <libpsl.h>
#include <stdio.h>
#include <string.h>

struct suffix_case { const char *input, *expected; };
static const struct suffix_case cases[] = {
#include "cases.inc"
};

int main(void)
{
    const psl_ctx_t *psl = psl_builtin();
    if (!psl || sizeof(cases) / sizeof(cases[0]) < 75)
        return 1;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const char *input = cases[i].input;
        char lower[256];
        if (strlen(input) >= sizeof(lower))
            return 1;
        for (size_t j = 0; j <= strlen(input); ++j) {
            unsigned char ch = input[j];
            lower[j] = ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
        }
        const char *domain = strcmp(input, "null") ? lower : NULL;
        const char *actual = psl_registrable_domain(psl, domain);
        if (strcmp(actual ? actual : "null", cases[i].expected)) {
            printf("[FAIL] suffix %s expected %s got %s\n", input, cases[i].expected, actual ? actual : "null");
            return 1;
        }
    }
    if (!psl_is_public_suffix2(psl, "github.io", PSL_TYPE_PRIVATE)
        || psl_is_public_suffix2(psl, "github.io", PSL_TYPE_ICANN)
        || psl_is_cookie_domain_acceptable(psl, "a.github.io", ".github.io")
        || !psl_is_cookie_domain_acceptable(psl, "b.a.github.io", ".a.github.io"))
        return 1;
    puts("[ OK ] public suffix rules and cookie-domain boundaries passed");
    return 0;
}
