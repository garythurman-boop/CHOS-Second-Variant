.section .rodata.large,"a",%progbits
.balign 4096
.global chos_large_read_only
.type chos_large_read_only,%object
chos_large_read_only:
.fill 2*1024*1024,1,0x5a
/* If XN fails, this would return to the fixture and explicitly fail. */
.long 0xd65f03c0
.fill 1024*1024-4,1,0x5a
.size chos_large_read_only,.-chos_large_read_only
.section .note.GNU-stack,"",%progbits
