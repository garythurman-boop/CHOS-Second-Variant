.section .note.chos,"a",%note
.balign 4
.long 5,8,0x43484f53
.asciz "CHOS"
.balign 4
.long 1 /* Native ABI version. */
.long 1 /* Explicit bounded engine profile; older kernels reject this note. */
.section .rodata.chos_profile,"a",%progbits
.balign 4
.global __chos_resource_profile_id
.type __chos_resource_profile_id,%object
__chos_resource_profile_id:
.long 1
.size __chos_resource_profile_id,4
