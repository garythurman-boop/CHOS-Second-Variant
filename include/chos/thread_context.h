#ifndef CHOS_THREAD_CONTEXT_H
#define CHOS_THREAD_CONTEXT_H
#include "chos/thread_context_api.h"
#include "chos/irq_context.h"
/* Pure checked conversion. Failure leaves output unchanged. Caller supplies
 * independently owned/pinned stack bounds and the current thread's identity. */
int chos_thread_context_capture(const chos_irq_context_t *,uint32_t,uint64_t,uint64_t,chos_thread_context_t *);
#endif
