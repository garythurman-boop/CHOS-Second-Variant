#ifndef CHOS_EXCEPTION_H
#define CHOS_EXCEPTION_H

#include <stdint.h>
#include "chos/irq_context.h"

#if defined(__clang__) || defined(__GNUC__)
#define CHOS_NORETURN __attribute__((noreturn))
#else
#define CHOS_NORETURN
#endif

/* Called only by the AArch64 vector stubs; this function never returns. */
CHOS_NORETURN void exception_panic(uint64_t esr, uint64_t elr,
                                   uint64_t far, uint64_t spsr,
                                   uint64_t vector_kind);

/* AArch64 lower-EL synchronous exception entry; dispatches SVC or isolates EL0 faults. */
chos_irq_context_t *exception_lower_sync_dispatch(chos_irq_context_t *context);

#endif
