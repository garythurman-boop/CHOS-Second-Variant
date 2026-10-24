/* Position-independent CPU-bound EL0 workload. Its loop runs long enough for the
 * 100 Hz timer to preempt it at least once under QEMU, then it reports and exits. */
.section .text.user_preempt_good, "ax"
.balign 16
.global chos_user_preempt_good_start
.global chos_user_preempt_good_end
.type chos_user_preempt_good_start, %function
chos_user_preempt_good_start:
    movz x9, #0xC380
    movk x9, #0x01C9, lsl #16   /* 30,000,000 iterations */
1:
    subs x9, x9, #1
    b.ne 1b
    mov x8, #1                  /* CHOS_SYS_WRITE */
    adr x0, 2f
    mov x1, #(.Lgood_end - .Lgood_message)
    svc #0
    cmp x0, x1
    b.ne 3f
    mov x0, #0
    b 4f
3:
    mov x0, #3
4:
    mov x8, #2                  /* CHOS_SYS_EXIT */
    svc #0
5:
    b 5b
2:
.Lgood_message:
    .ascii "[EL0 GOOD] resumed after timer preemption and exited\n"
.Lgood_end:
chos_user_preempt_good_end:
.size chos_user_preempt_good_start, chos_user_preempt_good_end - chos_user_preempt_good_start
