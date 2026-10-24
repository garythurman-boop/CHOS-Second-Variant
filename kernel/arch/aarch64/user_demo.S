/* Tiny position-independent EL0 payload copied to a private user RX page. */
.section .text.user_demo, "ax"
.balign 16
.global chos_user_demo_start
.global chos_user_demo_end
.type chos_user_demo_start, %function
chos_user_demo_start:
    /* Prove the EL0 stack mapping is writable and the pointer is below its top. */
    sub sp, sp, #16
    mov x9, #0x5a
    str x9, [sp]
    ldr x10, [sp]
    cmp x9, x10
    add sp, sp, #16
    b.ne 2f

    /* First verify bad-pointer rejection: address 0x1000 is outside user VA space. */
    mov x8, #1                   /* CHOS_SYS_WRITE */
    mov x0, #0x1000
    mov x1, #4
    svc #0
    cmn x0, #14                 /* Expected -EFAULT from the kernel copy helper. */
    b.ne 2f

    mov x8, #1
    adr x0, 1f
    mov x1, #(.Lmessage_end - .Lmessage)
    svc #0
    cmp x0, x1                   /* write returns the number of bytes written */
    b.ne 2f
    mov x0, #0
    b 3f
2:
    mov x0, #1
3:
    mov x8, #2                   /* CHOS_SYS_EXIT */
    svc #0
4:
    b 4b                         /* Exit is non-returning. */
1:
.Lmessage:
    .ascii "CHOS: hello from EL0 via SVC!\n"
.Lmessage_end:
chos_user_demo_end:
.size chos_user_demo_start, chos_user_demo_end - chos_user_demo_start
