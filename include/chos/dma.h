#ifndef CHOS_DMA_H
#define CHOS_DMA_H
#include <stddef.h>
#include <stdint.h>
/* Cache ownership helpers for identity DMA on the current single-core target.
 * Callers must use dedicated, cache-line-isolated buffers; these do not map an
 * IOMMU or translate arbitrary virtual addresses. */
static inline void chos_dma_cache(const void *p,size_t n,int invalidate) {
#ifdef CHOS_HOST_TEST
    (void)p;(void)n;(void)invalidate;
#else
    uint64_t ctr; __asm__ volatile("mrs %0,ctr_el0":"=r"(ctr));
    uintptr_t line=(uintptr_t)4<<((ctr>>16)&15),a=(uintptr_t)p;
    if(!n || a>UINTPTR_MAX-n) return;
    uintptr_t end=a+n;a&=~(line-1);
    for(;a<end;a+=line) {
        if(invalidate) __asm__ volatile("dc ivac,%0"::"r"(a):"memory");
        else __asm__ volatile("dc cvac,%0"::"r"(a):"memory");
    }
    __asm__ volatile("dsb sy":::"memory");
#endif
}
#endif
