.section .text.boot, "ax"
.balign 4
.global _start
.type _start, %function
_start:
    bl main
    stp x0, xzr, [sp, #-16]!
    bl __chos_libc_process_fini
    ldp x0, xzr, [sp], #16
    mov x8, #2              /* CHOS_SYS_EXIT */
    svc #0
.Lhang:
    wfe
    b .Lhang
.size _start, . - _start

/* A real local weak fallback avoids ARM64 undefined-weak ADRP semantics. Native
 * libc provides the strong cleanup hook; raw/flat binaries simply return. */
.weak __chos_libc_process_fini
.type __chos_libc_process_fini, %function
__chos_libc_process_fini:
    ret
.size __chos_libc_process_fini, . - __chos_libc_process_fini
