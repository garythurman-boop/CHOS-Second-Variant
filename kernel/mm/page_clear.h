#ifndef CHOS_PAGE_CLEAR_H
#define CHOS_PAGE_CLEAR_H
#include <stddef.h>
#include <stdint.h>
#define CHOS_PAGE_CLEAR_BYTES 4096u
/* Owned normal pages only, never MMIO. Alias-safe, aligned integer stores avoid
 * byte-at-a-time teardown without SIMD or DC ZVA assumptions. Volatile keeps
 * the scrub before page release even under whole-program optimization. */
typedef uint64_t chos_clear_word_t __attribute__((may_alias));
static inline int chos_clear_page(void *page) {
    if (!page || ((uintptr_t)page & (CHOS_PAGE_CLEAR_BYTES-1u))) return 0;
    volatile chos_clear_word_t *words = (volatile chos_clear_word_t *)page;
    for (size_t i=0; i<CHOS_PAGE_CLEAR_BYTES/sizeof(*words); ++i) words[i]=0;
    return 1;
}
#endif
