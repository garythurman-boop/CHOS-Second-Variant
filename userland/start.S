.section .text.boot, "ax"
.balign 4
.global _start
.type _start, %function
_start:
    bl main
    mov x8, #2              /* CHOS_SYS_EXIT */
    svc #0
.Lhang:
    wfe
    b .Lhang
.size _start, . - _start
