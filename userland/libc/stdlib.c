#include <errno.h>
#include <stdlib.h>
#include "chos/libc.h"
#include "chos/user.h"
_Static_assert(_Alignof(max_align_t)<=CHOS_HEAP_ALIGNMENT,"malloc must meet target max alignment");

static int process_errno;
static chos_heap_t process_heap;
static unsigned initialized;
int *__chos_errno_location(void){return &process_errno;}
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
void *malloc(size_t bytes){return allocation_result(chos_heap_alloc(heap(),bytes,CHOS_HEAP_ALIGNMENT));}
void free(void *p){if(chos_heap_free(heap(),p))errno=process_heap.error;}
void *calloc(size_t n,size_t bytes){return allocation_result(chos_heap_calloc(heap(),n,bytes));}
void *realloc(void *p,size_t bytes){
    void *next=chos_heap_realloc(heap(),p,bytes);
    if(!next && bytes)errno=process_heap.error;
    return next;
}
static int alignment_valid(size_t alignment){return alignment && !(alignment&(alignment-1u));}
void *aligned_alloc(size_t alignment,size_t bytes){
    if(!alignment_valid(alignment)||bytes%alignment){errno=EINVAL;return 0;}
    if(alignment<CHOS_HEAP_ALIGNMENT)alignment=CHOS_HEAP_ALIGNMENT;
    return allocation_result(chos_heap_alloc(heap(),bytes,alignment));
}
int posix_memalign(void **out,size_t alignment,size_t bytes){
    if(!out||!alignment_valid(alignment)||alignment%sizeof(void *))return EINVAL;
    if(alignment<CHOS_HEAP_ALIGNMENT)alignment=CHOS_HEAP_ALIGNMENT;
    void *p=chos_heap_alloc(heap(),bytes,alignment);
    if(!p)return process_heap.error;
    *out=p;return 0;
}
int chos_libc_heap_info(chos_heap_stats_t *out){return chos_heap_stats(heap(),out);}
