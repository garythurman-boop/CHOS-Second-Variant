/*
 * CHOS AArch64 exception vectors.
 *
 * Each architectural vector slot is exactly 128 bytes; the complete table is
 * 2048-byte aligned as required for VBAR_EL1. Synchronous, FIQ and SError
 * exceptions remain fatal; IRQs save context, dispatch the source and return.
 */
.extern interrupt_irq_dispatch
.extern exception_lower_sync_dispatch
.section .text.vectors, "ax"
.balign 2048
.global exception_vectors
.type exception_vectors, %function
exception_vectors:
.macro VECTOR_ENTRY target
    b \target
    .space 124
.endm
    /* Current EL using SP0: Sync, IRQ, FIQ, SError. */
    VECTOR_ENTRY exception_sync_entry
    VECTOR_ENTRY exception_irq_entry
    VECTOR_ENTRY exception_fiq_entry
    VECTOR_ENTRY exception_serror_entry
    /* Current EL using SPx: Sync, IRQ, FIQ, SError. */
    VECTOR_ENTRY exception_sync_entry
    VECTOR_ENTRY exception_irq_entry
    VECTOR_ENTRY exception_fiq_entry
    VECTOR_ENTRY exception_serror_entry
    /* Lower EL using AArch64: Sync uses the syscall/fault dispatcher. */
    VECTOR_ENTRY exception_lower_sync_entry
    VECTOR_ENTRY exception_irq_entry
    VECTOR_ENTRY exception_fiq_entry
    VECTOR_ENTRY exception_serror_entry
    /* Lower EL using AArch32: Sync, IRQ, FIQ, SError. */
    VECTOR_ENTRY exception_sync_entry
    VECTOR_ENTRY exception_irq_entry
    VECTOR_ENTRY exception_fiq_entry
    VECTOR_ENTRY exception_serror_entry
.size exception_vectors, . - exception_vectors

/* Sync/FIQ/SError report panic details; IRQs save context and return normally. */
.type exception_sync_entry, %function
exception_sync_entry:
    mov x4, #0
    b exception_common
.type exception_irq_entry, %function
exception_irq_entry:
    /*
     * Full, preemption-ready exception frame. Frame layout in
     * include/chos/irq_context.h: x0..x30, ELR_EL1, SPSR_EL1, ESR_EL1, SP_EL0.
     * 288 bytes preserves 16-byte SP alignment. Saving SP_EL0 is required for
     * correct restoration when preempting and switching between EL0 processes.
     * the development scheduler may return a different saved task frame; normal
     * builds stay cooperative by default.
     */
    sub sp, sp, #288
    stp x0,  x1,  [sp, #0]
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x0, elr_el1
    str x0, [sp, #248]
    mrs x0, spsr_el1
    str x0, [sp, #256]
    mrs x0, esr_el1
    str x0, [sp, #264]
    mrs x0, sp_el0
    str x0, [sp, #272]
    str xzr, [sp, #280]

    mov x0, sp
    bl interrupt_irq_dispatch
    /* x0 is the context frame selected for exception return. */
    mov sp, x0
    b exception_irq_restore

/* A synchronous exception from AArch64 EL0 (SVC, instruction/data abort, etc.).
 * Use the same full GPR/SPSR/ELR frame as IRQ entry so syscall arguments and
 * the user return frame are preserved. The C dispatcher may redirect ELR/SPSR
 * to the EL1 return trampoline for SYS_EXIT or an EL0 fault. */
.type exception_lower_sync_entry, %function
exception_lower_sync_entry:
    sub sp, sp, #288
    stp x0,  x1,  [sp, #0]
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x0, elr_el1
    str x0, [sp, #248]
    mrs x0, spsr_el1
    str x0, [sp, #256]
    mrs x0, esr_el1
    str x0, [sp, #264]
    mrs x0, sp_el0
    str x0, [sp, #272]
    str xzr, [sp, #280]
    mov x0, sp
    bl exception_lower_sync_dispatch
    mov sp, x0
    b exception_irq_restore

/* Enter a synthetic/previously saved EL1 IRQ frame using the same ERET ABI. */
.global chos_irq_resume
.type chos_irq_resume, %function
chos_irq_resume:
    mov sp, x0
    b exception_irq_restore

.type exception_irq_restore, %function
exception_irq_restore:
    ldr x16, [sp, #248]
    msr elr_el1, x16
    ldr x16, [sp, #256]
    msr spsr_el1, x16
    /* Restore the selected task's user stack before ERET. */
    ldr x16, [sp, #272]
    msr sp_el0, x16
    ldp x0,  x1,  [sp, #0]
    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #288
    eret
.type exception_fiq_entry, %function
exception_fiq_entry:
    mov x4, #2
    b exception_common
.type exception_serror_entry, %function
exception_serror_entry:
    mov x4, #3
    b exception_common

.type exception_common, %function
exception_common:
    /* DAIF cannot mask synchronous aborts. Latch BEFORE touching the interrupted
     * stack or calling C, so a reporting fault cannot recursively overflow it.
     * Plain aligned accesses are intentional: MMU-off Device memory need not
     * support exclusive operations. Secondary CPUs remain parked. */
    msr daifset, #0xf
    adrp x5, panic_vector_entered
    add x5, x5, :lo12:panic_vector_entered
    ldr w6, [x5]
    cbnz w6, 1f
    mov w6, #1
    str w6, [x5]
    mrs x0, esr_el1
    mrs x1, elr_el1
    mrs x2, far_el1
    mrs x3, spsr_el1
    /* A damaged/overflowed interrupted stack must not prevent diagnostics. */
    adrp x5, panic_stack_top
    add x5, x5, :lo12:panic_stack_top
    mov sp, x5
    bl exception_panic
1:
    wfe
    b 1b
.size exception_common, . - exception_common

.section .bss.panic, "aw", %nobits
.balign 16
panic_vector_entered:
    .skip 4
.balign 16
    .skip 8192
panic_stack_top:
