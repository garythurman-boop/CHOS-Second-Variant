#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "page_clear.h"
int main(void) {
    const size_t page=CHOS_PAGE_CLEAR_BYTES, bytes=3*CHOS_PAGE_CLEAR_BYTES;
    unsigned char *arena=aligned_alloc(page,bytes);assert(arena);
    for(unsigned seed=0; seed<64; ++seed) {
        for(size_t i=0;i<bytes;++i)arena[i]=(unsigned char)((i*37u+seed*17u)%255u+1u);
        assert(chos_clear_page(arena+page));
        for(size_t i=0;i<bytes;++i) {
            unsigned char expected=i>=page&&i<2*page?0:(unsigned char)((i*37u+seed*17u)%255u+1u);
            assert(arena[i]==expected);
        }
        assert(chos_clear_page(arena+page)); /* Idempotent, same exact extent. */
    }
    for(size_t i=0;i<bytes;++i)arena[i]=0xa5;
    assert(!chos_clear_page(NULL));
    for(size_t offset=1;offset<page;++offset)assert(!chos_clear_page(arena+offset));
    for(size_t i=0;i<bytes;++i)assert(arena[i]==0xa5);
    assert(chos_clear_page(arena));assert(chos_clear_page(arena+2*page));
    for(size_t i=0;i<bytes;++i)assert(arena[i]==(i>=page&&i<2*page?0xa5:0));
    free(arena);puts("page clear: exact4096-byte scrub,adjacent canaries,64 patterns,idempotence and all misaligned refusals passed");return 0;
}
