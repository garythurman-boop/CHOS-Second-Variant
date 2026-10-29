#ifndef CHOS_THREAD_CONTEXT_API_H
#define CHOS_THREAD_CONTEXT_API_H
#include <stdint.h>
#include <stddef.h>
#define CHOS_THREAD_CONTEXT_VERSION 1u
#define CHOS_THREAD_CONTEXT_CURRENT 1u
#define CHOS_THREAD_CONTEXT_REGISTERS_VALID 2u
#define CHOS_THREAD_CONTEXT_STACK_VALID 4u
/* Read-only snapshot of the calling EL0 thread at SVC entry. No foreign thread,
 * kernel context, signal frame, register modification or suspension is exposed. */
typedef struct {
 uint32_t version,size_bytes,tid,flags;
 uint64_t stack_base,stack_bytes;
 uint64_t x[31];
 uint64_t pc,sp,fpcr,fpsr,reserved;
 uint64_t q[32][2];
} __attribute__((aligned(16))) chos_thread_context_t;
_Static_assert(sizeof(chos_thread_context_t)==832u,"thread context ABI size");
_Static_assert(offsetof(chos_thread_context_t,x)==32u,"thread context GPR offset");
_Static_assert(offsetof(chos_thread_context_t,q)==320u,"thread context SIMD offset");
#endif
