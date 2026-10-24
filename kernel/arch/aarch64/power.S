.section .text, "ax"
.align 4
.global chos_arch_halt
.type chos_arch_halt, %function
chos_arch_halt:
    msr daifset, #0xf
    dsb sy
1:
    wfi
    b 1b
.size chos_arch_halt, . - chos_arch_halt

.align 4
.global chos_arch_restart
.type chos_arch_restart, %function
chos_arch_restart:
    /* Stop exceptions before returning to the reset entry path. */
    msr daifset, #0xf
    dsb sy
    /* Keep instruction/data caches intact but disable translation before .bss
     * (which contains the active page tables) is cleared at _start. */
    mrs x9, sctlr_el1
    bic x9, x9, #1
    msr sctlr_el1, x9
    dsb sy
    isb
    tlbi vmalle1
    dsb sy
    isb
    ldr x9, =_start
    br x9
.size chos_arch_restart, . - chos_arch_restart
