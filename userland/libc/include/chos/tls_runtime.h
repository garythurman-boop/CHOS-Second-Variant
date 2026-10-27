#ifndef CHOS_NATIVE_TLS_RUNTIME_H
#define CHOS_NATIVE_TLS_RUNTIME_H
#include <stdint.h>
#include <pthread.h>
#include <uchar.h>
#define CHOS_CXX_GUARD_DEPTH 16u
#define CHOS_TCB_MAGIC UINT64_C(0x43484f5354434231)
typedef struct { void *value;uint32_t generation; } chos_specific_t;
typedef struct {
 uint64_t magic;
 int error;unsigned main_thread;
 uint32_t tid,done,joining,once_depth,exiting;
 unsigned exit_counted,process_exiting,process_finalizer_depth;
 void *result;void *(*start)(void *);void *argument;
 uint64_t stack_base;size_t stack_bytes;
 uint64_t initial_fpcr,initial_fpsr;
 void *cxx_destructors;unsigned cxx_destructor_count;
 unsigned cxx_cleanup_started,cxx_finalize_depth,cxx_callbacks_left;
 mbstate_t unicode_states[4];
 pthread_once_t *active_once[8];
 uint64_t *active_guards[CHOS_CXX_GUARD_DEPTH];unsigned guard_depth;
 chos_specific_t specific[PTHREAD_KEYS_MAX];
} chos_libc_tcb_t;
#ifdef __cplusplus
extern "C" {
#endif
chos_libc_tcb_t *__chos_libc_tls(void);
void __chos_once_abandon(chos_libc_tcb_t *);
void __chos_cxx_guard_abandon(chos_libc_tcb_t *);
int __chos_pthread_key_create_late(pthread_key_t *,void (*)(void *));
void __chos_cxx_thread_finalize(chos_libc_tcb_t *);
#ifdef __cplusplus
}
#endif
#endif
