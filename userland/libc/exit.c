#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "chos/tls_runtime.h"
#include "chos/user.h"
#include "internal/exit_registry.h"
static pthread_mutex_t table_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t final_lock={0,PTHREAD_MUTEX_RECURSIVE,0,0,CHOS_MUTEX_MAGIC};
static int lock_registry(void *p,unsigned which){(void)p;return pthread_mutex_lock(which?&final_lock:&table_lock);}
static int unlock_registry(void *p,unsigned which){(void)p;return pthread_mutex_unlock(which?&final_lock:&table_lock);}
static chos_exit_registry_t registry={.lock=lock_registry,.unlock=unlock_registry};
static uint32_t process_cleanup;
int atexit(void (*fn)(void)){return chos_exit_register_c(&registry,fn)?-1:0;}
int __cxa_atexit(void (*fn)(void *),void *object,void *dso){
 if(!fn)return -1;
 /* Clang does not check registration failures. Never silently drop a destructor. */
 if(chos_exit_register_cxx(&registry,fn,object,dso))abort();
 return 0;
}
static void finalize(void *dso,int close_registry){
 chos_libc_tcb_t *t=__chos_libc_tls();++t->process_finalizer_depth;
 int result=chos_exit_finalize(&registry,dso,close_registry);
 --t->process_finalizer_depth;if(result)abort();
}
void __cxa_finalize(void *dso){finalize(dso,0);}
void __chos_libc_process_fini(void){
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&process_cleanup,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  if(expected==2)return;
  abort(); /* recursive/concurrent exit cannot leave a live finalizer owner */
 }
 chos_libc_tcb_t *t=__chos_libc_tls();t->process_exiting=1;
 __chos_cxx_thread_finalize(t);finalize(0,1);
 __atomic_store_n(&process_cleanup,2,__ATOMIC_RELEASE);
}
void _Exit(int status){chos_exit(status);}
void _exit(int status){_Exit(status);}
void exit(int status){__chos_libc_process_fini();_Exit(status);}
void __cxa_pure_virtual(void){abort();}
void __cxa_deleted_virtual(void){abort();}
