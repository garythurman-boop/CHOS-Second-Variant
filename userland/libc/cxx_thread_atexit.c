/* Static-link Itanium C++ thread-local destructor ABI, without DSO unloading. */
#include <stdlib.h>
#include "chos/tls_runtime.h"
typedef struct cleanup {struct cleanup *next;void (*destroy)(void *);void *object;} cleanup_t;
void *__dso_handle __attribute__((weak))=&__dso_handle;
int __cxa_thread_atexit_impl(void (*destroy)(void *),void *object,void *dso){
 (void)dso;if(!destroy)return -1;
 chos_libc_tcb_t *t=__chos_libc_tls();if(t->cxx_destructor_count>=64)abort();
 cleanup_t *entry=malloc(sizeof(*entry));if(!entry)abort();
 *entry=(cleanup_t){t->cxx_destructors,destroy,object};t->cxx_destructors=entry;++t->cxx_destructor_count;return 0;
}
int __cxa_thread_atexit(void (*destroy)(void *),void *object,void *dso){return __cxa_thread_atexit_impl(destroy,object,dso);}
void __chos_cxx_thread_finalize(chos_libc_tcb_t *t){
 unsigned invoked=0;
 while(t->cxx_destructors){
  if(++invoked>256)abort();
  cleanup_t *entry=t->cxx_destructors;void (*destroy)(void *)=entry->destroy;void *object=entry->object;
  t->cxx_destructors=entry->next;--t->cxx_destructor_count;free(entry);destroy(object);
 }
}
/* Normal main return runs C++ thread-local destructors. Ordinary pthread key
 * destructors are for pthread_exit, not process exit. The OS reaps other storage. */
void __chos_libc_process_fini(void){__chos_cxx_thread_finalize(__chos_libc_tls());}
