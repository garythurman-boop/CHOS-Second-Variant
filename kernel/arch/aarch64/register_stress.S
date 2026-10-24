/*
 * Register-preservation probe for opt-in timer-preemption testing.
 *
 * x19-x30 are saved according to AAPCS64. Then x0-x27 and x29-x30 are loaded
 * with distinct values while x28 is used as a countdown register. The loop is
 * deliberately long enough for timer IRQs to interrupt it. After the loop,
 * x28/x29 are restored to their test values and every GPR plus SP is checked.
 *
 * Stack locals: saved x19-x30 [0..95], original iteration count [96],
 * expected SP [104], result [112]. Total frame 128 bytes and 16-byte aligned.
 */
.section .text.register_stress, "ax"
.balign 16
.global chos_register_preservation_probe
.type chos_register_preservation_probe, %function
chos_register_preservation_probe:
    sub sp, sp, #128
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    str x0, [sp, #96]
    mov x1, sp
    str x1, [sp, #104]
    str xzr, [sp, #112]

    ldr x28, [sp, #96]
    cbz x28, .Lprobe_fail
    mov x0,  #0x200
    mov x1,  #0x201
    mov x2,  #0x202
    mov x3,  #0x203
    mov x4,  #0x204
    mov x5,  #0x205
    mov x6,  #0x206
    mov x7,  #0x207
    mov x8,  #0x208
    mov x9,  #0x209
    mov x10, #0x20a
    mov x11, #0x20b
    mov x12, #0x20c
    mov x13, #0x20d
    mov x14, #0x20e
    mov x15, #0x20f
    mov x16, #0x210
    mov x17, #0x211
    mov x18, #0x212
    mov x19, #0x213
    mov x20, #0x214
    mov x21, #0x215
    mov x22, #0x216
    mov x23, #0x217
    mov x24, #0x218
    mov x25, #0x219
    mov x26, #0x21a
    mov x27, #0x21b
    mov x29, #0x21d
    mov x30, #0x21e
.Lprobe_loop:
    subs x28, x28, #1
    b.ne .Lprobe_loop

    /* Verify SP separately, temporarily using x28/x29 as scratch. */
    mov x28, sp
    ldr x29, [sp, #104]
    cmp x28, x29
    b.ne .Lprobe_fail
    mov x28, #0x21c
    mov x29, #0x21d

    cmp x0,  #0x200
    b.ne .Lprobe_fail
    cmp x1,  #0x201
    b.ne .Lprobe_fail
    cmp x2,  #0x202
    b.ne .Lprobe_fail
    cmp x3,  #0x203
    b.ne .Lprobe_fail
    cmp x4,  #0x204
    b.ne .Lprobe_fail
    cmp x5,  #0x205
    b.ne .Lprobe_fail
    cmp x6,  #0x206
    b.ne .Lprobe_fail
    cmp x7,  #0x207
    b.ne .Lprobe_fail
    cmp x8,  #0x208
    b.ne .Lprobe_fail
    cmp x9,  #0x209
    b.ne .Lprobe_fail
    cmp x10, #0x20a
    b.ne .Lprobe_fail
    cmp x11, #0x20b
    b.ne .Lprobe_fail
    cmp x12, #0x20c
    b.ne .Lprobe_fail
    cmp x13, #0x20d
    b.ne .Lprobe_fail
    cmp x14, #0x20e
    b.ne .Lprobe_fail
    cmp x15, #0x20f
    b.ne .Lprobe_fail
    cmp x16, #0x210
    b.ne .Lprobe_fail
    cmp x17, #0x211
    b.ne .Lprobe_fail
    cmp x18, #0x212
    b.ne .Lprobe_fail
    cmp x19, #0x213
    b.ne .Lprobe_fail
    cmp x20, #0x214
    b.ne .Lprobe_fail
    cmp x21, #0x215
    b.ne .Lprobe_fail
    cmp x22, #0x216
    b.ne .Lprobe_fail
    cmp x23, #0x217
    b.ne .Lprobe_fail
    cmp x24, #0x218
    b.ne .Lprobe_fail
    cmp x25, #0x219
    b.ne .Lprobe_fail
    cmp x26, #0x21a
    b.ne .Lprobe_fail
    cmp x27, #0x21b
    b.ne .Lprobe_fail
    cmp x28, #0x21c
    b.ne .Lprobe_fail
    cmp x29, #0x21d
    b.ne .Lprobe_fail
    cmp x30, #0x21e
    b.ne .Lprobe_fail
    b .Lprobe_done

.Lprobe_fail:
    mov x28, #1
    str x28, [sp, #112]
.Lprobe_done:
    ldr x0, [sp, #112]
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #128
    ret
.size chos_register_preservation_probe, . - chos_register_preservation_probe
