.section .text, "ax"
.align 4
.global chos_arch_halt
.type chos_arch_halt, %function
chos_arch_halt:
    msr daifset, #0xf
    dsb sy
1:  wfi
    b 1b
.size chos_arch_halt, . - chos_arch_halt

/* C ABI (conduit, PSCI function). No platform guesses or firmware callbacks. */
.align 4
.global chos_arch_psci
.type chos_arch_psci, %function
chos_arch_psci:
    mov w4, w0
    mov w0, w1
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    dsb sy
    cmp w4, #2
    b.eq 2f
    smc #0
    ret
2:  hvc #0
    ret
.size chos_arch_psci, . - chos_arch_psci
