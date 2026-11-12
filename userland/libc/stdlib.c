#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "chos/libc.h"
#include "chos/memory_api.h"
#ifdef CHOS_STDLIB_HOST_TEST
extern long chos_map_memory(size_t);
extern long chos_reserve_memory(size_t,size_t);
extern long chos_unmap_memory(void *,size_t);
extern long chos_unmap_range(void *,size_t);
extern long chos_memory_control(void *,size_t,unsigned);
extern unsigned chos_native_resource_profile(void);
extern void chos_exit(long) __attribute__((noreturn));
#else
#include "chos/user.h"
#include "chos/tls_runtime.h"
#include "chos/resource_profile_user.h"
#endif
_Static_assert(_Alignof(max_align_t)<=CHOS_HEAP_ALIGNMENT,"malloc must meet target max alignment");

static chos_heap_t process_heap;
static chos_heap_region_t engine_heap_regions[CHOS_RESOURCE_ENGINE_REGIONS];
static unsigned initialized;
static pthread_mutex_t heap_lock=PTHREAD_MUTEX_INITIALIZER;
/* Only failed map attempts whose rollback became uncertain live here. A
 * successful mapping is tracked by process_heap instead. Never retry or reuse
 * these ranges; address-space reaping retains authoritative ownership. */
static struct {uintptr_t base;size_t bytes;} failed_maps[CHOS_RESOURCE_ENGINE_REGIONS];
static int release_reservation(uintptr_t *base,size_t *bytes){
    while(*bytes){
        size_t step=*bytes>CHOS_USER_MAP_MAX_BYTES?CHOS_USER_MAP_MAX_BYTES:*bytes;
        if(chos_unmap_range((void *)*base,step)<0)return -1;
        *base+=step;*bytes-=step;
    }
    return 0;
}
static void *map_pages(void *context,size_t bytes){
    chos_heap_t *h=context;
    if(bytes<=CHOS_USER_MAP_MAX_BYTES){
        long result=chos_map_memory(bytes);
        if(result<0){h->error=(int)-result;return 0;}
        return (void *)(uintptr_t)result;
    }
    unsigned slot;
    for(slot=0;slot<CHOS_RESOURCE_ENGINE_REGIONS&&failed_maps[slot].base;slot++){}
    if(slot==CHOS_RESOURCE_ENGINE_REGIONS){h->error=CHOS_HEAP_EIO;return 0;}
    long result=chos_reserve_memory(bytes,CHOS_HEAP_PAGE);
    if(result<0){h->error=(int)-result;return 0;}
    uintptr_t first=(uintptr_t)result,at=first;size_t remaining=bytes;
    failed_maps[slot].base=first;failed_maps[slot].bytes=bytes;
    while(remaining){
        size_t step=remaining>CHOS_USER_MAP_MAX_BYTES?CHOS_USER_MAP_MAX_BYTES:remaining;
        result=chos_memory_control((void *)at,step,CHOS_MEMORY_PROTECT_RW);
        if(result<0){
            int error=(int)-result;
            if(release_reservation(&failed_maps[slot].base,&failed_maps[slot].bytes))
                error=CHOS_HEAP_EIO;
            else failed_maps[slot].base=0;
            h->error=error;return 0;
        }
        at+=step;remaining-=step;
    }
    failed_maps[slot].base=0;failed_maps[slot].bytes=0;
    return (void *)first;
}
static int unmap_pages(void *context,void *base,size_t bytes){
    (void)context;
    if(bytes<=CHOS_USER_MAP_MAX_BYTES)return chos_unmap_memory(base,bytes)<0?-1:0;
    uintptr_t first=(uintptr_t)base;
    return release_reservation(&first,&bytes);
}
static chos_heap_t *heap(void){
    if(!initialized){
        if(chos_native_resource_profile()==CHOS_RESOURCE_ENGINE)
        {
            if(chos_heap_init_external(&process_heap,map_pages,unmap_pages,&process_heap,
                                         engine_heap_regions,CHOS_RESOURCE_ENGINE_REGIONS)||
               chos_heap_set_mapping_limit(&process_heap,CHOS_RESOURCE_ENGINE_RESERVE_MAX))abort();
        }
        else chos_heap_init(&process_heap,map_pages,unmap_pages,&process_heap);
        initialized=1;
    }
    return &process_heap;
}
static void *allocation_result(void *p){if(!p)errno=process_heap.error;return p;}
void *malloc(size_t bytes){int rc=pthread_mutex_lock(&heap_lock);if(rc){errno=rc;return 0;}void *p=allocation_result(chos_heap_alloc(heap(),bytes,CHOS_HEAP_ALIGNMENT));pthread_mutex_unlock(&heap_lock);return p;}
void free(void *p){int rc=pthread_mutex_lock(&heap_lock);if(rc){errno=rc;return;}if(chos_heap_free(heap(),p))errno=process_heap.error;pthread_mutex_unlock(&heap_lock);}
int __chos_libc_free_thread_storage(void *p){
 int rc=pthread_mutex_lock(&heap_lock);if(rc)return rc;
 rc=chos_heap_free(heap(),p)?process_heap.error:0;
 int unlocked=pthread_mutex_unlock(&heap_lock);return rc?rc:unlocked;
}
void *calloc(size_t n,size_t bytes){int rc=pthread_mutex_lock(&heap_lock);if(rc){errno=rc;return 0;}void *p=allocation_result(chos_heap_calloc(heap(),n,bytes));pthread_mutex_unlock(&heap_lock);return p;}
void *realloc(void *p,size_t bytes){
    int rc=pthread_mutex_lock(&heap_lock);if(rc){errno=rc;return 0;}
    void *next=chos_heap_realloc(heap(),p,bytes);
    if(!next && bytes)errno=process_heap.error;
    pthread_mutex_unlock(&heap_lock);return next;
}
static int alignment_valid(size_t alignment){return alignment && !(alignment&(alignment-1u));}
void *aligned_alloc(size_t alignment,size_t bytes){
    if(!alignment_valid(alignment)||bytes%alignment){errno=EINVAL;return 0;}
    if(alignment<CHOS_HEAP_ALIGNMENT)alignment=CHOS_HEAP_ALIGNMENT;
    int rc=pthread_mutex_lock(&heap_lock);if(rc){errno=rc;return 0;}void *p=allocation_result(chos_heap_alloc(heap(),bytes,alignment));pthread_mutex_unlock(&heap_lock);return p;
}
int posix_memalign(void **out,size_t alignment,size_t bytes){
    if(!out||!alignment_valid(alignment)||alignment%sizeof(void *))return EINVAL;
    if(alignment<CHOS_HEAP_ALIGNMENT)alignment=CHOS_HEAP_ALIGNMENT;
    int rc=pthread_mutex_lock(&heap_lock);if(rc)return rc;
    void *p=chos_heap_alloc(heap(),bytes,alignment);rc=p?0:process_heap.error;
    if(p)*out=p;pthread_mutex_unlock(&heap_lock);return rc;
}
int chos_libc_heap_info(chos_heap_stats_t *out){
    int rc=pthread_mutex_lock(&heap_lock);if(rc)return -1;
    rc=chos_heap_stats(heap(),out);
    if(out)for(unsigned i=0;i<CHOS_RESOURCE_ENGINE_REGIONS;i++)if(failed_maps[i].base){
        out->regions++;out->quarantined_regions++;out->mapped_bytes+=failed_maps[i].bytes;rc=-1;
    }
    pthread_mutex_unlock(&heap_lock);return rc;
}
void abort(void){chos_exit(134);}
