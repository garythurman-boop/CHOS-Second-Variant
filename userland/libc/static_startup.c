/* Opt-in dense static native ELF profile. Legacy CHCX/sparse profiles reject
 * constructor arrays rather than discarding them without executing them. */
#include <stdint.h>
#include <stdlib.h>
typedef void (*initializer_t)(void);
extern initializer_t __preinit_array_start[],__preinit_array_end[];
extern initializer_t __init_array_start[],__init_array_end[];
extern initializer_t __fini_array_start[],__fini_array_end[];
static uint32_t initialized,finalized;
void __chos_libc_run_fini_array(void);
static void run_forward(initializer_t *first,initializer_t *last){
 for(;first!=last;++first){if(!*first)abort();(*first)();}
}
void __chos_libc_process_init(void){
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&initialized,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  if(expected==2)return;abort();
 }
 /* Register first so arrays run after ordinary statics, while the callback
  * registry is still open to cleanup registered by a fini function. */
 if(atexit(__chos_libc_run_fini_array))abort();
 run_forward(__preinit_array_start,__preinit_array_end);
 run_forward(__init_array_start,__init_array_end);
 __atomic_store_n(&initialized,2,__ATOMIC_RELEASE);
}
void __chos_libc_run_fini_array(void){
 uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&finalized,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  if(expected==2)return;abort();
 }
 initializer_t *last=__fini_array_end;
 while(last!=__fini_array_start){--last;if(!*last)abort();(*last)();}
 __atomic_store_n(&finalized,2,__ATOMIC_RELEASE);
}
