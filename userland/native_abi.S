.section .note.chos,"a",%note
.balign 4
.long 5,4,0x43484f53
.asciz "CHOS"
.balign 4
.long 1 /* Native CHOS ABI version; not Linux or a signature. */
