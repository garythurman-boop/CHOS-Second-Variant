#ifndef CHOS_NATIVE_TLS_RUNTIME_H
#define CHOS_NATIVE_TLS_RUNTIME_H
#include <stdint.h>
#include <pthread.h>
#define CHOS_TCB_MAGIC UINT64_C(0x43484f5354434231)
typedef struct { void *value;uint32_t generation; } chos_specific_t;
typedef struct {
 uint64_t magic;
 int error;unsigned main_thread;
 uint32_t tid,done,joining,once_depth,exiting;
 void *result;void *(*start)(void *);void *argument;
 uint64_t stack_base;size_t stack_bytes;
 pthread_once_t *active_once[8];
 chos_specific_t specific[PTHREAD_KEYS_MAX];
} chos_libc_tcb_t;
#ifdef __cplusplus
extern "C" {
#endif
chos_libc_tcb_t *__chos_libc_tls(void);
void __chos_once_abandon(chos_libc_tcb_t *);
#ifdef __cplusplus
}
#endif
#endif
