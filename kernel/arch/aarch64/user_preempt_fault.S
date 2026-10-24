/* A second CPU-bound EL0 payload that deliberately reads unmapped address 0x1000.
 * The kernel must mark this process failed without panicking or killing its peer. */
.section .text.user_preempt_fault, "ax"
.balign 16
.global chos_user_preempt_fault_start
.global chos_user_preempt_fault_end
.type chos_user_preempt_fault_start, %function
chos_user_preempt_fault_start:
    movz x9, #0xE360
    movk x9, #0x00E4, lsl #16   /* 15,000,000 iterations */
1:
    subs x9, x9, #1
    b.ne 1b
    mov x10, #0x1000
    ldr x11, [x10]              /* Deliberate lower-EL translation fault. */
    mov x0, #0x7f
    mov x8, #2
    svc #0
2:
    b 2b
chos_user_preempt_fault_end:
.size chos_user_preempt_fault_start, chos_user_preempt_fault_end - chos_user_preempt_fault_start
