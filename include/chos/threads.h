#ifndef CHOS_THREADS_H
#define CHOS_THREADS_H
#include "chos/thread_api.h"
#include "chos/address_space.h"
#include "chos/irq_context.h"
/* Kernel-owned records, never user TCB pointers. Single CPU, IRQ-safe locks. */
int64_t chos_thread_enqueue(address_space_handle_t,const chos_thread_request_t *);
/* 0 pending, 1 published, negative errno on failure. Failed records are released. */
void chos_thread_cancel_pending(address_space_handle_t,uint32_t);
int chos_thread_create_result(address_space_handle_t,uint32_t tid);
int64_t chos_thread_self(address_space_handle_t);
int chos_thread_info(address_space_handle_t,uint32_t,chos_thread_info_t *);
int chos_thread_reap(address_space_handle_t,uint32_t);
int chos_thread_is_secondary(address_space_handle_t);
void chos_threads_service(void);
void chos_threads_stop(address_space_handle_t,int64_t status,int faulted);
int chos_threads_stopping(address_space_handle_t);
void chos_threads_finish(address_space_handle_t,int64_t *,int *);
int chos_threads_drop(address_space_handle_t);
chos_irq_context_t *chos_threads_cancel_return(chos_irq_context_t *);
#endif
