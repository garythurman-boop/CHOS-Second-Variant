#include "chos/allocator.h"

typedef struct { size_t span,requested; uint64_t magic; uint32_t used,reserved; } block_t;
#define HEADER sizeof(block_t)
#define MIN_SPAN (HEADER+CHOS_HEAP_ALIGNMENT)
#define DEFAULT_MAP (64u*1024u)
#define MAGIC UINT64_C(0x43484f5348454150)
_Static_assert(sizeof(block_t)==32,"heap headers must preserve max alignment");
static uint64_t stamp(const block_t *b){return MAGIC^(uintptr_t)b^b->span^b->requested^b->used;}
static void block_set(block_t *b,size_t span,size_t requested,unsigned used){
    b->span=span;b->requested=requested;b->used=used;b->reserved=0;b->magic=stamp(b);
}
static int valid(const block_t *b,size_t remaining){
    return remaining>=MIN_SPAN && b->span>=MIN_SPAN && b->span<=remaining &&
        !(b->span&(CHOS_HEAP_ALIGNMENT-1u)) && b->used<=1u && !b->reserved &&
        (b->used ? b->requested<=b->span-HEADER : !b->requested) && b->magic==stamp(b);
}
static void copy_bytes(void *dst,const void *src,size_t n){
    unsigned char *d=dst;const unsigned char *s=src;while(n--)*d++=*s++;
}
static void zero_bytes(void *dst,size_t n){unsigned char *p=dst;while(n--)*p++=0;}
void chos_heap_init(chos_heap_t *h,chos_heap_map_fn map,chos_heap_unmap_fn unmap,void *context){
    if(!h)return;zero_bytes(h,sizeof(*h));h->map=map;h->unmap=unmap;h->context=context;
}
static int corrupt(chos_heap_t *h,chos_heap_region_t *r){r->quarantined=1;h->error=CHOS_HEAP_EIO;return -1;}
static int region_valid(chos_heap_t *h,chos_heap_region_t *r){
    size_t off=0;
    while(off<r->bytes){block_t *b=(block_t *)((unsigned char *)r->base+off);
        if(!valid(b,r->bytes-off))return corrupt(h,r);off+=b->span;}
    return 0;
}
static void *from_region(chos_heap_t *h,chos_heap_region_t *r,size_t need,size_t alignment){
    if(region_valid(h,r))return 0;
    for(size_t off=0;off<r->bytes;){
        block_t *b=(block_t *)((unsigned char *)r->base+off);size_t span=b->span;
        if(!b->used){
            uintptr_t start=(uintptr_t)b,payload=start+HEADER;
            uintptr_t aligned=(payload+alignment-1u)&~(uintptr_t)(alignment-1u);
            size_t prefix=(size_t)(aligned-payload);
            if(prefix && prefix<MIN_SPAN){aligned+=alignment;prefix+=alignment;}
            if(prefix && prefix<MIN_SPAN){aligned+=alignment;prefix+=alignment;}
            if(prefix<=span && HEADER+need<=span-prefix){
                size_t alloc_span=HEADER+need,tail=span-prefix-alloc_span;
                if(tail<MIN_SPAN){alloc_span+=tail;tail=0;}
                block_t *allocated=(block_t *)(aligned-HEADER);
                if(prefix)block_set(b,prefix,0,0);
                block_set(allocated,alloc_span,need,1);
                if(tail)block_set((block_t *)((unsigned char *)allocated+alloc_span),tail,0,0);
                return (void *)aligned;
            }
        }
        off+=span;
    }
    return 0;
}
void *chos_heap_alloc(chos_heap_t *h,size_t size,size_t alignment){
    if(!h)return 0;h->error=0;
    if(!h->map||!h->unmap||alignment<CHOS_HEAP_ALIGNMENT||
       (alignment&(alignment-1u))){h->error=CHOS_HEAP_EINVAL;return 0;}
    if(alignment>CHOS_HEAP_MAP_MAX){h->error=CHOS_HEAP_ENOMEM;return 0;}
    size_t requested=size; if(!size)size=1;
    if(size>CHOS_HEAP_MAP_MAX-HEADER){h->error=CHOS_HEAP_ENOMEM;return 0;}
    size_t need=(size+CHOS_HEAP_ALIGNMENT-1u)&~(size_t)(CHOS_HEAP_ALIGNMENT-1u);
    for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++){
        chos_heap_region_t *r=&h->regions[i];
        if(r->base&&!r->quarantined){void *p=from_region(h,r,need,alignment);
            if(p){block_t *b=(block_t *)((unsigned char *)p-HEADER);b->requested=requested;b->magic=stamp(b);return p;}}
    }
    unsigned slot=CHOS_HEAP_REGIONS;
    for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++)if(!h->regions[i].base){slot=i;break;}
    if(slot==CHOS_HEAP_REGIONS){h->error=CHOS_HEAP_ENOMEM;return 0;}
    size_t extra=alignment>CHOS_HEAP_ALIGNMENT ? alignment+MIN_SPAN : 0;
    if(need>CHOS_HEAP_MAP_MAX-HEADER || extra>CHOS_HEAP_MAP_MAX-HEADER-need){h->error=CHOS_HEAP_ENOMEM;return 0;}
    size_t bytes=need+HEADER+extra;
    bytes=(bytes+CHOS_HEAP_PAGE-1u)&~(size_t)(CHOS_HEAP_PAGE-1u);
    if(bytes<DEFAULT_MAP)bytes=DEFAULT_MAP;
    void *base=h->map(h->context,bytes);
    if(!base){h->error=CHOS_HEAP_ENOMEM;return 0;}
    chos_heap_region_t *r=&h->regions[slot];r->base=base;r->bytes=bytes;r->quarantined=0;
    if(((uintptr_t)base&(CHOS_HEAP_PAGE-1u)) || (uintptr_t)base>UINTPTR_MAX-bytes){
        /* A broken mapping backend cannot be used for aligned metadata. */
        r->quarantined=1;h->error=CHOS_HEAP_EIO;return 0;
    }
    block_set(base,bytes,0,0);
    void *p=from_region(h,r,need,alignment);
    if(!p){corrupt(h,r);return 0;}
    block_t *b=(block_t *)((unsigned char *)p-HEADER);b->requested=requested;b->magic=stamp(b);return p;
}
static block_t *lookup(chos_heap_t *h,void *p,chos_heap_region_t **region){
    uintptr_t addr=(uintptr_t)p;
    for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++){
        chos_heap_region_t *r=&h->regions[i];uintptr_t base=(uintptr_t)r->base;
        if(!r->base||r->quarantined||addr<base||addr-base>=r->bytes)continue;
        if(region_valid(h,r))return 0;
        for(size_t off=0;off<r->bytes;){block_t *b=(block_t *)((unsigned char *)r->base+off);
            if((void *)((unsigned char *)b+HEADER)==p && b->used){*region=r;return b;}off+=b->span;}
        break;
    }
    if(!h->error)h->error=CHOS_HEAP_EINVAL;return 0;
}
int chos_heap_free(chos_heap_t *h,void *p){
    if(!h)return -1;h->error=0;if(!p)return 0;
    chos_heap_region_t *r=0;block_t *b=lookup(h,p,&r);if(!b)return -1;
    block_set(b,b->span,0,0);
    block_t *previous=0;
    for(size_t off=0;off<r->bytes;){
        block_t *current=(block_t *)((unsigned char *)r->base+off);size_t step=current->span;
        if(previous && !previous->used && !current->used)block_set(previous,previous->span+step,0,0);
        else previous=current;off+=step;
    }
    b=r->base;
    if(!b->used && b->span==r->bytes){
        if(h->unmap(h->context,r->base,r->bytes)){return corrupt(h,r);}
        r->base=0;r->bytes=0;r->quarantined=0;
    }
    return 0;
}
void *chos_heap_calloc(chos_heap_t *h,size_t n,size_t size){
    if(size && n>SIZE_MAX/size){if(h)h->error=CHOS_HEAP_ENOMEM;return 0;}
    size_t bytes=n*size;void *p=chos_heap_alloc(h,bytes,CHOS_HEAP_ALIGNMENT);if(p)zero_bytes(p,bytes);return p;
}
void *chos_heap_realloc(chos_heap_t *h,void *p,size_t size){
    if(!p)return chos_heap_alloc(h,size,CHOS_HEAP_ALIGNMENT);
    if(!h)return 0;h->error=0;chos_heap_region_t *r=0;block_t *b=lookup(h,p,&r);if(!b)return 0;
    if(!size){chos_heap_free(h,p);return 0;}
    if(size<=b->span-HEADER){b->requested=size;b->magic=stamp(b);return p;}
    size_t old=b->requested;
    void *next=chos_heap_alloc(h,size,CHOS_HEAP_ALIGNMENT);if(!next)return 0;
    copy_bytes(next,p,old);(void)chos_heap_free(h,p);return next;
}
int chos_heap_destroy(chos_heap_t *h){
    if(!h)return -1;h->error=0;int failed=0;
    for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++){
        chos_heap_region_t *r=&h->regions[i];if(!r->base)continue;
        if(r->quarantined||h->unmap(h->context,r->base,r->bytes)){corrupt(h,r);failed=1;continue;}
        r->base=0;r->bytes=0;r->quarantined=0;
    }
    return failed?-1:0;
}
int chos_heap_stats(chos_heap_t *h,chos_heap_stats_t *out){
    if(!h||!out)return -1;h->error=0;zero_bytes(out,sizeof(*out));int failed=0;
    for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++){
        chos_heap_region_t *r=&h->regions[i];if(!r->base)continue;
        out->regions++;out->mapped_bytes+=r->bytes;
        if(r->quarantined||region_valid(h,r)){out->quarantined_regions++;failed=1;continue;}
        for(size_t off=0;off<r->bytes;){block_t *b=(block_t *)((unsigned char *)r->base+off);
            if(b->used){out->allocations++;out->requested_bytes+=b->requested;}off+=b->span;}
    }
    if(failed)h->error=CHOS_HEAP_EIO;return failed?-1:0;
}
