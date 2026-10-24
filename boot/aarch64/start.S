/* CHOS ARM64 bootstrap. Entry may be EL2 (QEMU) or EL1 (UEFI handoff after EBS). */
.section .text.boot, "ax"
.global _start
.type _start, %function
_start:
    mov x20, x0                    // Optional direct-boot FDT hint (legacy path).
    mov x21, xzr                   // Direct QEMU loader: no UEFI boot-info handoff.
    b boot_common

/* The UEFI PIE advertises this entry in its ELF header. Keep the ordinary
 * _start entry for QEMU's generic loader, which expects a static ET_EXEC. */
.global chos_uefi_start
.type chos_uefi_start, %function
chos_uefi_start:
    mov x20, x0                    // EFI loader passes chos_boot_info_t * in x0.
    movz x21, #0x4631
    movk x21, #0x5545, lsl #16
    movk x21, #0x4f53, lsl #32
    movk x21, #0x4348, lsl #48  // CHOS_UEFI_ENTRY_MAGIC
    b boot_common
.size chos_uefi_start, . - chos_uefi_start

.type boot_common, %function
boot_common:
    msr daifset, #0xf             // Mask exceptions during early setup.
    mrs x9, CurrentEL
    lsr x9, x9, #2
    and x9, x9, #3
    cmp x9, #2
    b.eq enter_el1
    cmp x9, #1
    b.eq el1_entry
    b hang

enter_el1:
    // Configure a minimal EL2 -> EL1 transition, using EL1h and masked IRQs.
    mov x9, #(1 << 31)            // HCR_EL2.RW = AArch64 at EL1.
    msr hcr_el2, x9
    mov x9, #3
    msr cnthctl_el2, x9           // Permit EL1 access to physical counter/timer.
    msr cntvoff_el2, xzr
    adr x9, el1_entry
    msr elr_el2, x9
    mov x9, #0x3c5                // EL1h, mask D/A/I/F.
    msr spsr_el2, x9
    eret

el1_entry:
    /*
     * Early-boot control registers can be implementation/entry-path dependent.
     * Clear SCTLR_EL1.A explicitly before any C code. The FDT is a byte stream,
     * and optimized parsers may use naturally valid unaligned accesses to it.
     * We also build C with -mstrict-align as a second line of defence.
     */
    mrs x9, sctlr_el1
    bic x9, x9, #2              // SCTLR_EL1.A = 0: disable alignment checking.
    msr sctlr_el1, x9
    isb

    ldr x9, =__stack_top
    mov sp, x9
    // Clear .bss (word-aligned linker symbols).
    ldr x0, =__bss_start
    ldr x1, =__bss_end
1:
    cmp x0, x1
    b.hs 2f
    str xzr, [x0], #8
    b 1b
2:
    /* Firmware services are exited before the kernel is entered. Always
     * install CHOS-owned exception vectors; never retain firmware IRQ routing. */
    ldr x0, =exception_vectors
    msr vbar_el1, x0
    isb
    mov x0, x20                    // FDT hint on direct boot, boot-info pointer on UEFI.
    mov x1, x21                    // Zero for direct boot; explicit magic for UEFI.
    bl kernel_main

hang:
    wfe
    b hang
.size _start, . - _start

.section .bss.stack, "aw", %nobits
.balign 4096
.global __stack_guard_start
__stack_guard_start:
    .skip 4096
.balign 4096
.global __stack_bottom
__stack_bottom:
    .skip 65536
.global __stack_top
__stack_top:
