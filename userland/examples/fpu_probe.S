/* Actual EL0 FP/SIMD probe. Kernel C remains general-register-only. */
.section .text.boot,"ax"
.global _start
_start:
    umov x9, v0.d[0]
    cbnz x9, .Ldirty
    umov x9, v0.d[1]
    cbnz x9, .Ldirty
    umov x9, v1.d[0]
    cbnz x9, .Ldirty
    umov x9, v1.d[1]
    cbnz x9, .Ldirty
    umov x9, v2.d[0]
    cbnz x9, .Ldirty
    umov x9, v2.d[1]
    cbnz x9, .Ldirty
    umov x9, v3.d[0]
    cbnz x9, .Ldirty
    umov x9, v3.d[1]
    cbnz x9, .Ldirty
    umov x9, v4.d[0]
    cbnz x9, .Ldirty
    umov x9, v4.d[1]
    cbnz x9, .Ldirty
    umov x9, v5.d[0]
    cbnz x9, .Ldirty
    umov x9, v5.d[1]
    cbnz x9, .Ldirty
    umov x9, v6.d[0]
    cbnz x9, .Ldirty
    umov x9, v6.d[1]
    cbnz x9, .Ldirty
    umov x9, v7.d[0]
    cbnz x9, .Ldirty
    umov x9, v7.d[1]
    cbnz x9, .Ldirty
    umov x9, v8.d[0]
    cbnz x9, .Ldirty
    umov x9, v8.d[1]
    cbnz x9, .Ldirty
    umov x9, v9.d[0]
    cbnz x9, .Ldirty
    umov x9, v9.d[1]
    cbnz x9, .Ldirty
    umov x9, v10.d[0]
    cbnz x9, .Ldirty
    umov x9, v10.d[1]
    cbnz x9, .Ldirty
    umov x9, v11.d[0]
    cbnz x9, .Ldirty
    umov x9, v11.d[1]
    cbnz x9, .Ldirty
    umov x9, v12.d[0]
    cbnz x9, .Ldirty
    umov x9, v12.d[1]
    cbnz x9, .Ldirty
    umov x9, v13.d[0]
    cbnz x9, .Ldirty
    umov x9, v13.d[1]
    cbnz x9, .Ldirty
    umov x9, v14.d[0]
    cbnz x9, .Ldirty
    umov x9, v14.d[1]
    cbnz x9, .Ldirty
    umov x9, v15.d[0]
    cbnz x9, .Ldirty
    umov x9, v15.d[1]
    cbnz x9, .Ldirty
    umov x9, v16.d[0]
    cbnz x9, .Ldirty
    umov x9, v16.d[1]
    cbnz x9, .Ldirty
    umov x9, v17.d[0]
    cbnz x9, .Ldirty
    umov x9, v17.d[1]
    cbnz x9, .Ldirty
    umov x9, v18.d[0]
    cbnz x9, .Ldirty
    umov x9, v18.d[1]
    cbnz x9, .Ldirty
    umov x9, v19.d[0]
    cbnz x9, .Ldirty
    umov x9, v19.d[1]
    cbnz x9, .Ldirty
    umov x9, v20.d[0]
    cbnz x9, .Ldirty
    umov x9, v20.d[1]
    cbnz x9, .Ldirty
    umov x9, v21.d[0]
    cbnz x9, .Ldirty
    umov x9, v21.d[1]
    cbnz x9, .Ldirty
    umov x9, v22.d[0]
    cbnz x9, .Ldirty
    umov x9, v22.d[1]
    cbnz x9, .Ldirty
    umov x9, v23.d[0]
    cbnz x9, .Ldirty
    umov x9, v23.d[1]
    cbnz x9, .Ldirty
    umov x9, v24.d[0]
    cbnz x9, .Ldirty
    umov x9, v24.d[1]
    cbnz x9, .Ldirty
    umov x9, v25.d[0]
    cbnz x9, .Ldirty
    umov x9, v25.d[1]
    cbnz x9, .Ldirty
    umov x9, v26.d[0]
    cbnz x9, .Ldirty
    umov x9, v26.d[1]
    cbnz x9, .Ldirty
    umov x9, v27.d[0]
    cbnz x9, .Ldirty
    umov x9, v27.d[1]
    cbnz x9, .Ldirty
    umov x9, v28.d[0]
    cbnz x9, .Ldirty
    umov x9, v28.d[1]
    cbnz x9, .Ldirty
    umov x9, v29.d[0]
    cbnz x9, .Ldirty
    umov x9, v29.d[1]
    cbnz x9, .Ldirty
    umov x9, v30.d[0]
    cbnz x9, .Ldirty
    umov x9, v30.d[1]
    cbnz x9, .Ldirty
    umov x9, v31.d[0]
    cbnz x9, .Ldirty
    umov x9, v31.d[1]
    cbnz x9, .Ldirty
    mrs x9, fpcr
    cbnz x9, .Ldirty
    mrs x9, fpsr
    cbnz x9, .Ldirty
    bl main
    mov x8, #2
    svc #0
.Ldirty:
    adr x0, dirty_message
    mov x1, #dirty_end-dirty_message
    mov x8, #1
    svc #0
    mov x0, #99
    mov x8, #2
    svc #0
.Lhang:
    b .Lhang
.section .rodata
 dirty_message: .ascii "[FAIL] fresh FP state is not zero\n"
 dirty_end:
.section .text,"ax"
.global fpu_probe
.type fpu_probe,%function
fpu_probe:
    sub sp, sp, #112
    stp x19, x20, [sp]
    stp d8, d9, [sp, #16]
    stp d10, d11, [sp, #32]
    stp d12, d13, [sp, #48]
    stp d14, d15, [sp, #64]
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [sp, #80]
    mov x19, x0
    mov x20, x1
    mov x9, x19
    dup v0.2d, x9
    add x9, x9, #1
    dup v1.2d, x9
    add x9, x9, #1
    dup v2.2d, x9
    add x9, x9, #1
    dup v3.2d, x9
    add x9, x9, #1
    dup v4.2d, x9
    add x9, x9, #1
    dup v5.2d, x9
    add x9, x9, #1
    dup v6.2d, x9
    add x9, x9, #1
    dup v7.2d, x9
    add x9, x9, #1
    dup v8.2d, x9
    add x9, x9, #1
    dup v9.2d, x9
    add x9, x9, #1
    dup v10.2d, x9
    add x9, x9, #1
    dup v11.2d, x9
    add x9, x9, #1
    dup v12.2d, x9
    add x9, x9, #1
    dup v13.2d, x9
    add x9, x9, #1
    dup v14.2d, x9
    add x9, x9, #1
    dup v15.2d, x9
    add x9, x9, #1
    dup v16.2d, x9
    add x9, x9, #1
    dup v17.2d, x9
    add x9, x9, #1
    dup v18.2d, x9
    add x9, x9, #1
    dup v19.2d, x9
    add x9, x9, #1
    dup v20.2d, x9
    add x9, x9, #1
    dup v21.2d, x9
    add x9, x9, #1
    dup v22.2d, x9
    add x9, x9, #1
    dup v23.2d, x9
    add x9, x9, #1
    dup v24.2d, x9
    add x9, x9, #1
    dup v25.2d, x9
    add x9, x9, #1
    dup v26.2d, x9
    add x9, x9, #1
    dup v27.2d, x9
    add x9, x9, #1
    dup v28.2d, x9
    add x9, x9, #1
    dup v29.2d, x9
    add x9, x9, #1
    dup v30.2d, x9
    add x9, x9, #1
    dup v31.2d, x9
    add x9, x9, #1
    msr fpcr, x20
    mov x9, #0x11
    msr fpsr, x9
    /* No system call for long enough to expose timer preemption. */
    movz x10, #0x5a00
    movk x10, #0x262, lsl #16
.Lbusy:
    subs x10, x10, #1
    b.ne .Lbusy
    mov x8, #4
    svc #0
    mov x8, #5
    mov x0, #2
    svc #0
    mov x9, x19
    umov x10, v0.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v0.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v1.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v1.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v2.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v2.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v3.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v3.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v4.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v4.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v5.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v5.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v6.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v6.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v7.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v7.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v8.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v8.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v9.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v9.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v10.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v10.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v11.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v11.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v12.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v12.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v13.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v13.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v14.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v14.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v15.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v15.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v16.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v16.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v17.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v17.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v18.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v18.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v19.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v19.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v20.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v20.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v21.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v21.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v22.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v22.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v23.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v23.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v24.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v24.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v25.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v25.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v26.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v26.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v27.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v27.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v28.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v28.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v29.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v29.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v30.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v30.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    umov x10, v31.d[0]
    cmp x10, x9
    b.ne .Lbad
    umov x10, v31.d[1]
    cmp x10, x9
    b.ne .Lbad
    add x9, x9, #1
    mrs x9, fpcr
    cmp x9, x20
    b.ne .Lbad
    mrs x9, fpsr
    cmp x9, #0x11
    b.ne .Lbad
    mov x0, #0
    b .Lrestore
.Lbad:
    mov x0, #1
.Lrestore:
    ldp x9, x10, [sp, #80]
    msr fpcr, x9
    msr fpsr, x10
    ldp d8, d9, [sp, #16]
    ldp d10, d11, [sp, #32]
    ldp d12, d13, [sp, #48]
    ldp d14, d15, [sp, #64]
    ldp x19, x20, [sp]
    add sp, sp, #112
    ret
.size fpu_probe, .-fpu_probe
