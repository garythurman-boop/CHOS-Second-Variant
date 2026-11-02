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
    puts("resource admission: exact profiles, unknown/forged refusal, reservation/physical-floor boundaries and overflow passed");
}
