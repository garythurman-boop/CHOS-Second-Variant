#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "chos/libc.h"
#include "chos/user.h"
_Static_assert(_Alignof(max_align_t)<=CHOS_HEAP_ALIGNMENT,"malloc must meet target max alignment");

static chos_heap_t process_heap;
static unsigned initialized;
static pthread_mutex_t heap_lock=PTHREAD_MUTEX_INITIALIZER;
static void *map_pages(void *context,size_t bytes){
    (void)context;long result=chos_map_memory(bytes);return result<0?0:(void *)(uintptr_t)result;
}
static int unmap_pages(void *context,void *base,size_t bytes){
    (void)context;return chos_unmap_memory(base,bytes)<0?-1:0;
}
static chos_heap_t *heap(void){
    if(!initialized){chos_heap_init(&process_heap,map_pages,unmap_pages,0);initialized=1;}
    return &process_heap;
}
static void *allocation_result(void *p){if(!p)errno=process_heap.error;return p;}
void *malloc(size_t bytes){int rc=pthread_mutex_lock(&heap_lock);if(rc){errno=rc;return 0;}void *p=allocation_result(chos_heap_alloc(heap(),bytes,CHOS_HEAP_ALIGNMENT));pthread_mutex_unlock(&heap_lock);return p;}
void free(void *p){int rc=pthread_mutex_lock(&heap_lock);if(rc){errno=rc;return;}if(chos_heap_free(heap(),p))errno=process_heap.error;pthread_mutex_unlock(&heap_lock);}
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
int chos_libc_heap_info(chos_heap_stats_t *out){int rc=pthread_mutex_lock(&heap_lock);if(rc)return -1;rc=chos_heap_stats(heap(),out);pthread_mutex_unlock(&heap_lock);return rc;}
