/* EL1 <-> EL0 trampoline. Per-invocation return state lives on the caller's
 * task stack so timer-preempted EL0 tasks never overwrite one another's state. */
.section .text.user_entry, "ax"
.balign 16
.global chos_enter_el0
.type chos_enter_el0, %function
chos_enter_el0:
    /* x0=entry, x1=user SP, x2=argc, x3=argv, x4=kernel faulted_out. */
    stp x19, x20, [sp, #-128]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    mrs x9, daif
    str x9, [sp, #96]
    str x4, [sp, #104]
    str x2, [sp, #112]
    str x3, [sp, #120]

    msr sp_el0, x1
    msr elr_el1, x0
    mov x9, #0x340               /* EL0t; IRQ enabled, D/A/F masked. */
    msr spsr_el1, x9
    dsb sy
    ic iallu
    dsb sy
    isb
    mov x0, xzr
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr
    mov x9, xzr
    mov x10, xzr
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    mov x16, xzr
    mov x17, xzr
    mov x18, xzr
    mov x19, xzr
    mov x20, xzr
    mov x21, xzr
    mov x22, xzr
    mov x23, xzr
    mov x24, xzr
    mov x25, xzr
    mov x26, xzr
    mov x27, xzr
    mov x28, xzr
    mov x29, xzr
    mov x30, xzr
    ldr x0, [sp, #112]
    ldr x1, [sp, #120]
    eret
.size chos_enter_el0, . - chos_enter_el0

.global chos_user_mode_return
.type chos_user_mode_return, %function
chos_user_mode_return:
    /* Exception restore has returned to the saved EL1 frame base. x0=status,
     * x1=faulted. The output pointer and DAIF snapshot are task-stack local. */
    ldr x9, [sp, #104]
    str x1, [x9]
    ldr x10, [sp, #96]
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #128
    msr daif, x10
    isb
    ret
.size chos_user_mode_return, . - chos_user_mode_return
