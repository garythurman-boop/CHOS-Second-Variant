/* Actual allocator+native wrapper with bounded syscall fault models. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "chos/allocator.h"
static int native_errno;
int *__chos_errno_location(void){return &native_errno;}
int test_mutex_lock(pthread_mutex_t *);
int test_mutex_unlock(pthread_mutex_t *);
void test_abort(void) __attribute__((noreturn));
#define CHOS_STDLIB_HOST_TEST 1
#define malloc test_malloc
#define free test_free
#define calloc test_calloc
#define realloc test_realloc
#define aligned_alloc test_aligned_alloc
#define posix_memalign test_posix_memalign
#define abort test_abort
#define pthread_mutex_lock test_mutex_lock
#define pthread_mutex_unlock test_mutex_unlock
#include "../userland/libc/stdlib.c"
#undef malloc
#undef free
#undef calloc
#undef realloc
#undef aligned_alloc
#undef posix_memalign
#undef abort
#undef pthread_mutex_lock
#undef pthread_mutex_unlock

typedef struct {unsigned char *base;size_t bytes,removed;unsigned char pages[8192];} Area;
static Area areas[512];
static unsigned profile,small_calls,reserve_calls,commit_calls,unmap_calls;
static unsigned reserve_fail,commit_fail,unmap_fail,locked;
int test_mutex_lock(pthread_mutex_t *m){(void)m;assert(!locked);locked=1;return 0;}
int test_mutex_unlock(pthread_mutex_t *m){(void)m;assert(locked);locked=0;return 0;}
unsigned chos_native_resource_profile(void){return profile;}
void chos_exit(long code){fprintf(stderr,"unexpected native process exit %ld\n",code);abort();}
static long area(size_t bytes,int eager){
    assert(bytes&&bytes<=CHOS_RESOURCE_ENGINE_RESERVE_MAX&&!(bytes%4096));
    for(unsigned i=0;i<512;i++)if(!areas[i].base){
        unsigned char *p=aligned_alloc(4096,bytes);assert(p);memset(p,eager?0:0xa5,bytes);
        areas[i].base=p;areas[i].bytes=bytes;areas[i].removed=0;
        memset(areas[i].pages,eager,bytes/4096);return (long)(uintptr_t)p;
    }
    assert(!"mock area capacity");return -ENOMEM;
}
long chos_map_memory(size_t bytes){assert(locked&&bytes<=CHOS_USER_MAP_MAX_BYTES);small_calls++;return area(bytes,1);}
long chos_reserve_memory(size_t bytes,size_t alignment){
    assert(locked&&alignment==4096);reserve_calls++;assert(profile==CHOS_RESOURCE_ENGINE);
    if(reserve_calls==reserve_fail)return -ENOMEM;return area(bytes,0);
}
static Area *find(void *p,size_t bytes){
    uintptr_t at=(uintptr_t)p;
    for(unsigned i=0;i<512;i++)if(areas[i].base&&at>=(uintptr_t)areas[i].base&&
       at-(uintptr_t)areas[i].base<=areas[i].bytes&&bytes<=areas[i].bytes-(at-(uintptr_t)areas[i].base))return &areas[i];
    assert(!"foreign mock range");return NULL;
}
long chos_memory_control(void *p,size_t bytes,unsigned operation){
    assert(locked&&bytes&&!(bytes%4096)&&bytes<=CHOS_USER_MAP_MAX_BYTES&&operation==CHOS_MEMORY_PROTECT_RW);
    Area *a=find(p,bytes);size_t first=((unsigned char *)p-a->base)/4096;
    commit_calls++;if(commit_calls==commit_fail)return -ENOMEM;
    for(size_t i=first;i<first+bytes/4096;i++){assert(!a->pages[i]);a->pages[i]=1;}
    memset(p,0,bytes);return 0;
}
long chos_unmap_range(void *p,size_t bytes){
    assert(locked&&bytes&&!(bytes%4096)&&bytes<=CHOS_USER_MAP_MAX_BYTES);
    Area *a=find(p,bytes);unmap_calls++;if(unmap_calls==unmap_fail)return -EIO;
    assert((unsigned char *)p==a->base+a->removed);
    size_t first=a->removed/4096;
    for(size_t i=first;i<first+bytes/4096;i++)a->pages[i]=0;
    memset(p,0xdd,bytes);a->removed+=bytes;
    if(a->removed==a->bytes){free(a->base);memset(a,0,sizeof(*a));}return 0;
}
long chos_unmap_memory(void *p,size_t bytes){
    Area *a=find(p,bytes);assert(a->base==p&&a->bytes==bytes&&bytes<=CHOS_USER_MAP_MAX_BYTES);
    return chos_unmap_range(p,bytes);
}
static unsigned active(void){unsigned n=0;for(unsigned i=0;i<512;i++)n+=!!areas[i].base;return n;}
static void reset(unsigned next_profile){
    for(unsigned i=0;i<512;i++)free(areas[i].base);
    memset(areas,0,sizeof(areas));memset(failed_maps,0,sizeof(failed_maps));memset(&process_heap,0,sizeof(process_heap));
    memset(engine_heap_regions,0,sizeof(engine_heap_regions));initialized=0;
    profile=next_profile;small_calls=reserve_calls=commit_calls=unmap_calls=0;
    reserve_fail=commit_fail=unmap_fail=locked=0;native_errno=0;
}
static void clean(void){chos_heap_stats_t s;assert(!chos_libc_heap_info(&s));assert(!s.allocations&&!s.mapped_bytes&&!s.regions&&!active());}
static void large_and_alignment(void){
    reset(CHOS_RESOURCE_ENGINE);
    const size_t sizes[]={CHOS_USER_MAP_MAX_BYTES-32u,CHOS_USER_MAP_MAX_BYTES,8u*1024u*1024u,32u*1024u*1024u-32u};
    for(unsigned k=0;k<4;k++){
        size_t bytes=sizes[k];unsigned char *p=test_malloc(bytes);assert(p&&!((uintptr_t)p%16));
        for(size_t n=0;n<bytes;n++)assert(!p[n]);
        for(size_t n=0;n<bytes;n++)p[n]=(unsigned char)(n*31u+k);
        for(size_t n=0;n<bytes;n++)assert(p[n]==(unsigned char)(n*31u+k));
        chos_heap_stats_t s;assert(!chos_libc_heap_info(&s)&&s.allocations==1&&s.requested_bytes==bytes);
        test_free(p);clean();
    }
    assert(!test_malloc(CHOS_RESOURCE_ENGINE_RESERVE_MAX)&&errno==ENOMEM);
    assert(!test_malloc(SIZE_MAX)&&errno==ENOMEM);assert(!test_calloc(SIZE_MAX,2)&&errno==ENOMEM);
    void *p=test_aligned_alloc(1024u*1024u,2u*1024u*1024u);assert(p&&!((uintptr_t)p%(1024u*1024u)));test_free(p);clean();
    p=(void *)0x1234;assert(test_posix_memalign(&p,3,8)==EINVAL&&p==(void *)0x1234);
    assert(!test_posix_memalign(&p,65536,4u*1024u*1024u)&&!((uintptr_t)p%65536));test_free(p);clean();
}
static void reallocations(void){
    reset(CHOS_RESOURCE_ENGINE);unsigned char *p=test_malloc(39);assert(p);memset(p,0x51,39);
    p=test_realloc(p,8u*1024u*1024u);assert(p);for(unsigned i=0;i<39;i++)assert(p[i]==0x51);
    memset(p,0x34,8u*1024u*1024u);unsigned char *old=p;
    reserve_fail=reserve_calls+1;assert(!test_realloc(p,16u*1024u*1024u)&&errno==ENOMEM);
    for(size_t i=0;i<8u*1024u*1024u;i++)assert(old[i]==0x34);
    reserve_fail=0;p=test_realloc(old,16u*1024u*1024u);assert(p);
    for(size_t i=0;i<8u*1024u*1024u;i++)assert(p[i]==0x34);
    assert(test_realloc(p,20)==p);assert(!test_realloc(p,0));clean();
    p=test_calloc(3u*1024u,2048);assert(p);for(size_t i=0;i<6u*1024u*1024u;i++)assert(!p[i]);test_free(p);clean();
}
static void rollback(void){
    for(unsigned failure=1;failure<=9;failure++){
        reset(CHOS_RESOURCE_ENGINE);commit_fail=failure;
        assert(!test_malloc(8u*1024u*1024u)&&errno==ENOMEM);clean();
        assert(commit_calls==failure&&reserve_calls==1);
    }
    reset(CHOS_RESOURCE_ENGINE);reserve_fail=1;assert(!test_malloc(8u*1024u*1024u));clean();
    reset(CHOS_RESOURCE_ENGINE);commit_fail=3;unmap_fail=2;
    assert(!test_malloc(8u*1024u*1024u)&&errno==EIO);
    chos_heap_stats_t s;assert(chos_libc_heap_info(&s)==-1&&s.quarantined_regions==1&&s.allocations==0&&active()==1);
    unsigned calls=unmap_calls;assert(chos_libc_heap_info(&s)==-1&&unmap_calls==calls);
    reset(CHOS_RESOURCE_ENGINE);unsigned char *p=test_malloc(8u*1024u*1024u);assert(p);unmap_fail=2;
    test_free(p);assert(errno==EIO);assert(chos_libc_heap_info(&s)==-1&&s.quarantined_regions==1&&s.allocations==0);
    calls=unmap_calls;test_free(p);assert(unmap_calls==calls); /* Header prefix was disposed: no read/reuse. */
    reset(CHOS_RESOURCE_ENGINE);clean();
}
static void legacy(void){
    reset(CHOS_RESOURCE_LEGACY);assert(!test_malloc(CHOS_USER_MAP_MAX_BYTES)&&errno==ENOMEM&&!reserve_calls);
    unsigned char *p=test_malloc(CHOS_USER_MAP_MAX_BYTES-32u);assert(p);test_free(p);clean();
    assert(!reserve_calls&&!commit_calls);
}
int main(void){large_and_alignment();reallocations();rollback();legacy();reset(CHOS_RESOURCE_LEGACY);
    puts("large heap: actual core/wrapper,32MiB boundary,alignment,realloc preservation,all commit failures,quarantine and unchanged legacy cap passed");return 0;}
