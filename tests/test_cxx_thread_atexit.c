#include <assert.h>
#include <stdio.h>
#include "chos/tls_runtime.h"
extern int __cxa_thread_atexit_impl(void (*)(void *),void *,void *);
extern int __cxa_thread_atexit(void (*)(void *),void *,void *);
extern void __chos_libc_process_fini(void);
static chos_libc_tcb_t threads[2];
static unsigned selected,next;
chos_libc_tcb_t *__chos_libc_tls(void){return &threads[selected];}
static void descending(void *object){assert((uintptr_t)object==next--);}
static unsigned callback_count;
static void add_during_cleanup(void *object){
    (void)object;++callback_count;
    if(callback_count<3)assert(!__cxa_thread_atexit(add_during_cleanup,0,0));
}
int main(void){
    assert(__cxa_thread_atexit(0,0,0)==-1&&!threads[0].cxx_destructors);
    for(unsigned i=1;i<=64;i++)assert(!__cxa_thread_atexit_impl(descending,(void *)(uintptr_t)i,0));
    assert(threads[0].cxx_destructor_count==64);
    selected=1;assert(!__cxa_thread_atexit(add_during_cleanup,0,0));
    __chos_libc_process_fini();assert(callback_count==3&&!threads[1].cxx_destructors&&!threads[1].cxx_destructor_count);
    selected=0;next=64;__chos_cxx_thread_finalize(&threads[0]);
    assert(!next&&!threads[0].cxx_destructors&&!threads[0].cxx_destructor_count);
    __chos_cxx_thread_finalize(&threads[0]);
    puts("native C++ TLS cleanup: per-TCB ownership,LIFO64,registration during cleanup and normal-main hook passed");
    return 0;
}
