#ifndef CHOS_USER_MODE_H
#define CHOS_USER_MODE_H

#include <stdint.h>
#include "chos/address_space.h"
#include "chos/irq_context.h"

/* Runs a tiny position-independent EL0 test payload using the CHOS SVC ABI. */
int user_mode_self_test(void);
/* Synchronously run a mapped, executable EL0 entry and return its SYS_EXIT status.
 * The caller owns the address space and must clean it up after this returns. */
int user_mode_execute_with_tls(address_space_handle_t,uint64_t,uint64_t,uint64_t,uint64_t,int64_t *,int *,uint64_t);
int user_mode_execute(address_space_handle_t space, uint64_t entry,
                      uint64_t user_stack_top, int64_t *exit_status_out,
                      int *faulted_out);
int user_mode_execute_with_args(address_space_handle_t space, uint64_t entry,
                      uint64_t user_stack_pointer, uint64_t argc, uint64_t argv,
                      int64_t *exit_status_out, int *faulted_out);
int user_mode_is_active(void);
int user_mode_get_address_space(address_space_handle_t *out);
void user_mode_request_exit(chos_irq_context_t *frame, int64_t status, int faulted);
void user_mode_force_return(chos_irq_context_t *frame, int64_t status);
uint64_t user_mode_write_syscalls(void);
uint64_t user_mode_rejected_writes(void);

#endif
