/* Minimal position-independent EL0 payload for process-slot reuse tests. */
.section .text.user_exit, "ax"
.balign 16
.global chos_user_exit_start
.global chos_user_exit_end
.type chos_user_exit_start, %function
chos_user_exit_start:
    mov x0, #0                  /* exit status */
    mov x8, #2                  /* CHOS_SYS_EXIT */
    svc #0
1:
    b 1b                        /* SYS_EXIT must never return to EL0 */
chos_user_exit_end:
.size chos_user_exit_start, chos_user_exit_end - chos_user_exit_start
