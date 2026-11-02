.section .note.chos,"a",%note
.balign 4
.long 5,4,0x43484f53
.asciz "CHOS"
.balign 4
.long 1 /* Native CHOS ABI version; not Linux or a signature. */
.section .rodata.chos_profile,"a",%progbits
.balign 4
.global __chos_resource_profile_id
.type __chos_resource_profile_id,%object
__chos_resource_profile_id:
.long 0
.size __chos_resource_profile_id,4
