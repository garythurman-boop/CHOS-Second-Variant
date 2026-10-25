#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chos/allocator.h"

typedef struct {
    struct { void *p; size_t n; } maps[CHOS_HEAP_REGIONS];
    unsigned maps_total,unmaps_total,fail_map,fail_unmap;
} backend_t;
static void *mapping(void *context,size_t bytes){
    backend_t *b=context;
    assert(bytes && !(bytes%CHOS_HEAP_PAGE) && bytes<=CHOS_HEAP_MAP_MAX);
    if(b->fail_map)return 0;
    for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++)if(!b->maps[i].p){
        void *p=aligned_alloc(CHOS_HEAP_PAGE,bytes);assert(p);memset(p,0xa5,bytes);
        b->maps[i].p=p;b->maps[i].n=bytes;b->maps_total++;return p;
    }
    assert(!"allocator exceeded backing mapping table");return 0;
}
static int unmapping(void *context,void *p,size_t bytes){
    backend_t *b=context;b->unmaps_total++;
    for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++)if(b->maps[i].p==p){
        assert(b->maps[i].n==bytes);if(b->fail_unmap)return -1;
        memset(p,0,bytes);free(p);b->maps[i].p=0;b->maps[i].n=0;return 0;
    }
    assert(!"foreign or repeated unmap");return -1;
}
static void cleanup(backend_t *b){for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++)free(b->maps[i].p);memset(b,0,sizeof(*b));}
static void begin(chos_heap_t *h,backend_t *b){memset(b,0,sizeof(*b));chos_heap_init(h,mapping,unmapping,b);}
static void empty(chos_heap_t *h){chos_heap_stats_t s;assert(!chos_heap_stats(h,&s));assert(!s.allocations&&!s.regions&&!s.mapped_bytes);}
static void basics(void){
    chos_heap_t h;backend_t b;begin(&h,&b);
    assert(!chos_heap_alloc(&h,10,3)&&h.error==CHOS_HEAP_EINVAL);
    assert(!chos_heap_alloc(&h,10,CHOS_HEAP_MAP_MAX*2u)&&h.error==CHOS_HEAP_ENOMEM);
    assert(!chos_heap_alloc(&h,SIZE_MAX,16)&&h.error==CHOS_HEAP_ENOMEM);
    assert(!chos_heap_calloc(&h,SIZE_MAX,2)&&h.error==CHOS_HEAP_ENOMEM);
    assert(!chos_heap_free(&h,0));int foreign;assert(chos_heap_free(&h,&foreign)&&h.error==CHOS_HEAP_EINVAL);
    unsigned char *a=chos_heap_alloc(&h,47,16);assert(a&&!((uintptr_t)a%16));
    memset(a,0x51,47);assert(chos_heap_free(&h,a+1)&&h.error==CHOS_HEAP_EINVAL);
    unsigned char *zero=chos_heap_calloc(&h,55,7);assert(zero);for(size_t i=0;i<385;i++)assert(!zero[i]);
    unsigned char *large=chos_heap_realloc(&h,a,100000);assert(large);for(size_t i=0;i<47;i++)assert(large[i]==0x51);
    b.fail_map=1;assert(!chos_heap_realloc(&h,large,1200000)&&h.error==CHOS_HEAP_ENOMEM);
    for(size_t i=0;i<47;i++)assert(large[i]==0x51);
    assert(chos_heap_realloc(&h,large,7)==large);assert(!chos_heap_realloc(&h,large,0));
    assert(!chos_heap_free(&h,zero));empty(&h);
    assert(chos_heap_free(&h,zero)&&h.error==CHOS_HEAP_EINVAL);
    assert(!chos_heap_destroy(&h));assert(b.maps_total==b.unmaps_total);cleanup(&b);
}
static void alignment_and_coalescing(void){
    chos_heap_t h;backend_t b;begin(&h,&b);
    void *ptrs[13];
    for(unsigned i=0;i<13;i++){size_t alignment=(size_t)16<<i;ptrs[i]=chos_heap_alloc(&h,129,alignment);
        assert(ptrs[i]&&!((uintptr_t)ptrs[i]%alignment));memset(ptrs[i],i,129);}
    for(unsigned i=0;i<13;i++){for(unsigned n=0;n<129;n++)assert(((unsigned char *)ptrs[i])[n]==i);assert(!chos_heap_free(&h,ptrs[i]));}
    empty(&h);
    void *a=chos_heap_alloc(&h,20000,16),*c=chos_heap_alloc(&h,20000,16),*d=chos_heap_alloc(&h,20000,16);assert(a&&c&&d);
    unsigned mapped=b.maps_total;assert(!chos_heap_free(&h,a));assert(!chos_heap_free(&h,c));
    void *combined=chos_heap_alloc(&h,35000,16);assert(combined==a&&b.maps_total==mapped);
    assert(!chos_heap_free(&h,d));assert(!chos_heap_free(&h,combined));empty(&h);
    void *max=chos_heap_alloc(&h,CHOS_HEAP_MAP_MAX-32u,16);assert(max);memset(max,0x43,CHOS_HEAP_MAP_MAX-32u);assert(!chos_heap_free(&h,max));
    assert(!chos_heap_destroy(&h));cleanup(&b);
}
static void failure_and_quarantine(void){
    chos_heap_t h;backend_t b;begin(&h,&b);b.fail_map=1;
    assert(!chos_heap_alloc(&h,10,16)&&h.error==CHOS_HEAP_ENOMEM);empty(&h);b.fail_map=0;
    void *p[CHOS_HEAP_REGIONS];for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++){p[i]=chos_heap_alloc(&h,100000,16);assert(p[i]);}
    assert(!chos_heap_alloc(&h,100000,16)&&h.error==CHOS_HEAP_ENOMEM);
    assert(!chos_heap_destroy(&h));empty(&h);cleanup(&b);
    begin(&h,&b);void *one=chos_heap_alloc(&h,123,16);assert(one);b.fail_unmap=1;
    assert(chos_heap_free(&h,one)&&h.error==CHOS_HEAP_EIO);unsigned calls=b.unmaps_total;
    assert(chos_heap_destroy(&h)&&b.unmaps_total==calls); /* Uncertain mappings are never reused/retried. */
    chos_heap_stats_t s;assert(chos_heap_stats(&h,&s)&&s.quarantined_regions==1);cleanup(&b);
    begin(&h,&b);one=chos_heap_alloc(&h,123,16);assert(one);
    ((unsigned char *)one)[-8]^=0x40; /* Corrupt the owned in-mapping header's used/reserved fields. */
    assert(chos_heap_free(&h,one)&&h.error==CHOS_HEAP_EIO);
    assert(chos_heap_stats(&h,&s)&&s.quarantined_regions==1);assert(chos_heap_destroy(&h)&&!b.unmaps_total);cleanup(&b);
}
static uint32_t random_state=0x9182e537u;
static uint32_t random_value(void){random_state^=random_state<<13;random_state^=random_state>>17;return random_state^=random_state<<5;}
static void stress(void){
    enum { SLOTS=128 };void *p[SLOTS]={0};size_t sizes[SLOTS]={0};unsigned char values[SLOTS]={0};
    chos_heap_t h;backend_t b;begin(&h,&b);
    for(unsigned round=0;round<12000;round++){
        unsigned slot=random_value()%SLOTS;
        if(p[slot])for(size_t n=0;n<sizes[slot];n++)assert(((unsigned char *)p[slot])[n]==values[slot]);
        if(p[slot]&&(random_value()&1u)){assert(!chos_heap_free(&h,p[slot]));p[slot]=0;sizes[slot]=0;}
        else {
            size_t size=1+random_value()%4096;void *next=chos_heap_realloc(&h,p[slot],size);
            if(next){size_t preserved=sizes[slot]<size?sizes[slot]:size;
                for(size_t n=0;n<preserved;n++)assert(((unsigned char *)next)[n]==values[slot]);
                p[slot]=next;sizes[slot]=size;values[slot]=(unsigned char)random_value();memset(next,values[slot],size);}
            else assert(h.error==CHOS_HEAP_ENOMEM);
        }
        for(unsigned i=0;i<SLOTS;i++)if(p[i])for(unsigned j=i+1;j<SLOTS;j++)if(p[j]){
            uintptr_t ai=(uintptr_t)p[i],aj=(uintptr_t)p[j];assert(ai+sizes[i]<=aj||aj+sizes[j]<=ai);}
        if(!(round%97)){chos_heap_stats_t stats;assert(!chos_heap_stats(&h,&stats));
            size_t live=0,total=0;for(unsigned i=0;i<SLOTS;i++)if(p[i]){live++;total+=sizes[i];}
            assert(stats.allocations==live&&stats.requested_bytes==total);}
    }
    for(unsigned i=0;i<SLOTS;i++)assert(!chos_heap_free(&h,p[i]));empty(&h);assert(!chos_heap_destroy(&h));cleanup(&b);
}
int main(void){basics();alignment_and_coalescing();failure_and_quarantine();stress();puts("user allocator: PASS (actual core, alignment, overflow, coalescing, rollback, quarantine, 12000 mixed operations)");return 0;}
