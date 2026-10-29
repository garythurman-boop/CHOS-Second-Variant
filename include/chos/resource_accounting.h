#ifndef CHOS_RESOURCE_ACCOUNTING_H
#define CHOS_RESOURCE_ACCOUNTING_H
#include <stdint.h>
#define CHOS_CPU_STOPPED 0u
#define CHOS_CPU_USER 1u
#define CHOS_CPU_KERNEL 2u
/* Owned by the single-CPU scheduler under its local IRQ mask. No allocation,
 * address-space walks, locks, floating point, or scheduler policy in this model. */
typedef struct {
 uint64_t user_ticks,system_ticks,counter;
 uint32_t mode,valid;
} chos_cpu_account_t;
void chos_cpu_account_init(chos_cpu_account_t *);
/* Flush the previous interval then enter mode at now. Invalid counter/order or
 * overflow invalidates both measurements permanently until owner reinitializes. */
void chos_cpu_account_transition(chos_cpu_account_t *thread,chos_cpu_account_t *process,uint64_t now,unsigned mode);
#endif
