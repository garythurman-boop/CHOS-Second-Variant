#pragma once
#include <stdint.h>
#include <stddef.h>
struct alignas(16) PauseSeed {
 uint32_t ready,stop,mode,word;
 volatile uint64_t counter;uint64_t tid,fpcr,fpsr;
 uint64_t roots[10],lanes[32][2],observedRoots[10],observedLanes[32][2],observedFpcr,observedFpsr;
};
static_assert(offsetof(PauseSeed,roots)==48 && offsetof(PauseSeed,lanes)==128);
static_assert(offsetof(PauseSeed,observedRoots)==640 && offsetof(PauseSeed,observedLanes)==720);
static_assert(offsetof(PauseSeed,observedFpcr)==1232 && sizeof(PauseSeed)==1248);
extern "C" void pause_seed_run(PauseSeed*);
extern "C" char pause_seed_active[],pause_seed_end[],pause_seed_wait_return[];
