#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/resource_profile.h"
int main(void) {
    chos_resource_profile_t legacy, engine;
    assert(chos_resource_profile_get(CHOS_RESOURCE_LEGACY, &legacy));
    assert(chos_resource_profile_get(CHOS_RESOURCE_ENGINE, &engine));
    assert(legacy.frame_limit == 2048 && legacy.buckets == 4096);
    assert(engine.frame_limit == 49152 && engine.buckets == 131072);
    assert(!chos_resource_profile_get(0, NULL));
    chos_resource_profile_t old = engine;
    assert(!chos_resource_profile_get(UINT32_MAX, &engine));
    assert(!memcmp(&old, &engine, sizeof(old)));
    const chos_resource_profile_t profiles[] = {legacy, engine};
    for (unsigned i = 0; i < 2; ++i) {
        const chos_resource_profile_t *p = &profiles[i];
        assert(!(p->buckets & (p->buckets - 1u)) && p->frame_limit <= p->buckets / 2u);
        uint64_t threshold = (uint64_t)CHOS_RESOURCE_SYSTEM_RESERVE_FRAMES + p->frame_limit + 5u;
        assert(!chos_resource_admit(0, threshold - 1u, p));
        assert(chos_resource_admit(0, threshold, p));
        assert(chos_resource_admit(CHOS_RESOURCE_COMMITTED_FRAMES - p->frame_limit, UINT64_MAX, p));
        assert(!chos_resource_admit(CHOS_RESOURCE_COMMITTED_FRAMES - p->frame_limit + 1u, UINT64_MAX, p));
        assert(!chos_resource_admit(UINT32_MAX, UINT64_MAX, p));
        chos_resource_profile_t forged = *p;
        forged.frame_limit--;
        assert(!chos_resource_admit(0, UINT64_MAX, &forged));
        forged = *p; forged.buckets /= 2u;
        assert(!chos_resource_admit(0, UINT64_MAX, &forged));
    }
    assert(!chos_resource_admit(0, UINT64_MAX, NULL));
    chos_execution_profile_t x,y;
    assert(chos_execution_profile_get(CHOS_RESOURCE_LEGACY,&x));
    assert(chos_execution_profile_get(CHOS_RESOURCE_ENGINE,&y));
    assert(x.image_span==4u*1024u*1024u&&x.root_stack_pages==8&&x.map_regions==16&&x.default_worker_stack_bytes==32768);
    assert(y.image_span==64u*1024u*1024u&&y.root_stack_pages==256&&y.map_regions==512&&y.default_worker_stack_bytes==128u*1024u);
    assert(UINT64_C(0x20000000)+x.image_span<x.map_base&&UINT64_C(0x20000000)+y.image_span<y.map_base);
    assert(x.map_base+x.map_bytes<UINT64_C(0x30000000)-x.root_stack_pages*4096u);
    assert(y.map_base+y.map_bytes<UINT64_C(0x30000000)-y.root_stack_pages*4096u);
    assert(!chos_execution_stack_mask(0)&&!chos_execution_stack_mask(33));
    for(unsigned n=1;n<=32;n++)assert(chos_execution_stack_mask(n)==(uint32_t)((UINT64_C(1)<<n)-1u));
    for(unsigned n=1;n<=256;n++)for(unsigned word=0;word<4;word++){
        uint64_t expected=0;
        for(unsigned bit=0;bit<64;bit++)if(word*64u+bit<n)expected|=UINT64_C(1)<<bit;
        assert(chos_execution_stack_word_mask(n,word)==expected);
    }
    assert(!chos_execution_stack_word_mask(0,0)&&!chos_execution_stack_word_mask(257,0));
    assert(!chos_execution_stack_word_mask(256,4)&&!chos_execution_stack_word_mask(UINT32_MAX,UINT32_MAX));
    chos_execution_profile_t saved;memcpy(&saved,&y,sizeof(y));assert(!chos_execution_profile_get(UINT32_MAX,&y));
    assert(!memcmp(&saved,&y,sizeof(y)));
    puts("resource admission: exact profiles, unknown/forged refusal, reservation/physical-floor boundaries and overflow passed");
}
