/* Small position-independent CHCX v1 programs packaged in the read-only ROMFS.
 * Each entry is a complete 24-byte CHCX header followed by AArch64 payload.
 * The loader copies payload bytes into a private user RX page before execution. */
.section .rodata.chcx,"a",%progbits
.balign 16
.global chos_romfs_hello_start
.global chos_romfs_hello_end
chos_romfs_hello_start:
    .ascii "CHCX"
    .hword 1
    .hword 24
    .word .Lhello_payload_end - .Lhello_payload
    .word 0
    .hword 1
    .hword 0
    .word 0
.Lhello_payload:
    /* Verify invalid user pointers are rejected, then perform a valid write. */
    mov x8, #1
    mov x0, #0x1000
    mov x1, #4
    svc #0
    cmn x0, #14
    b.ne .Lhello_bad
    mov x8, #1
    adr x0, .Lhello_message
    mov x1, #(.Lhello_message_end - .Lhello_message)
    svc #0
    cmp x0, x1
    b.ne .Lhello_bad
    mov x0, #0
    b .Lhello_exit
.Lhello_bad:
    mov x0, #1
.Lhello_exit:
    mov x8, #2
    svc #0
    b .Lhello_exit
.Lhello_message:
    .ascii "CHOS: hello from /BIN/HELLO.CHX via EL0!\n"
.Lhello_message_end:
.Lhello_payload_end:
chos_romfs_hello_end:

.balign 16
.global chos_romfs_about_start
.global chos_romfs_about_end
chos_romfs_about_start:
    .ascii "CHCX"
    .hword 1
    .hword 24
    .word .Labout_payload_end - .Labout_payload
    .word 0
    .hword 1
    .hword 0
    .word 0
.Labout_payload:
    mov x8, #1
    adr x0, .Labout_message
    mov x1, #(.Labout_message_end - .Labout_message)
    svc #0
    cmp x0, x1
    b.ne .Labout_bad
    mov x0, #0
    b .Labout_exit
.Labout_bad:
    mov x0, #2
.Labout_exit:
    mov x8, #2
    svc #0
    b .Labout_exit
.Labout_message:
    .ascii "CHOS 0.7.0: persistent CHFS storage, CHCX apps, and ROMFS recovery are active.\n"
.Labout_message_end:
.Labout_payload_end:
chos_romfs_about_end:

.balign 16
.global chos_romfs_fault_start
.global chos_romfs_fault_end
chos_romfs_fault_start:
    .ascii "CHCX"
    .hword 1
    .hword 24
    .word .Lfault_payload_end - .Lfault_payload
    .word 0
    .hword 1
    .hword 0
    .word 0
.Lfault_payload:
    mov x9, #0x1000
    ldr x10, [x9]            /* Intentional EL0 translation fault. */
    mov x0, #0
    mov x8, #2
    svc #0
    b .Lfault_payload
.Lfault_payload_end:
chos_romfs_fault_end:

.balign 16
.global chos_romfs_async_start
.global chos_romfs_async_end
chos_romfs_async_start:
    .ascii "CHCX"
    .hword 1
    .hword 24
    .word .Lasync_payload_end - .Lasync_payload
    .word 0
    .hword 1
    .hword 0
    .word 0
.Lasync_payload:
    /* Exercise GETPID. The scheduler binds this EL0 program to a process record. */
    mov x8, #3                  /* CHOS_SYS_GETPID */
    svc #0
    cmp x0, #0
    b.le .Lasync_bad

    mov x8, #1                  /* CHOS_SYS_WRITE */
    adr x0, .Lasync_started
    mov x1, #(.Lasync_started_end - .Lasync_started)
    svc #0
    cmp x0, x1
    b.ne .Lasync_bad

    mov x0, #3                  /* Sleep for three 100 Hz ticks. */
    mov x8, #5                  /* CHOS_SYS_SLEEP */
    svc #0
    cmp x0, #0
    b.ne .Lasync_bad

    mov x8, #1                  /* CHOS_SYS_WRITE */
    adr x0, .Lasync_resumed
    mov x1, #(.Lasync_resumed_end - .Lasync_resumed)
    svc #0
    cmp x0, x1
    b.ne .Lasync_bad

    mov x8, #4                  /* CHOS_SYS_YIELD */
    svc #0
    cmp x0, #0
    b.ne .Lasync_bad

    mov x0, #0
    mov x8, #2                  /* CHOS_SYS_EXIT */
    svc #0
.Lasync_hang:
    b .Lasync_hang
.Lasync_bad:
    mov x0, #9
    mov x8, #2
    svc #0
    b .Lasync_hang
.Lasync_started:
    .ascii "[ASYNC] started in EL0; sleeping for three timer ticks\n"
.Lasync_started_end:
.Lasync_resumed:
    .ascii "[ASYNC] resumed; yielding once before exit\n"
.Lasync_resumed_end:
.Lasync_payload_end:
chos_romfs_async_end:
