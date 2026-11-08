.text
.global chos_test_setjmp_registers
.type chos_test_setjmp_registers,%function
// Inputs: x0 jump buffer,x1 output22x64,w2 desired return. Save our caller's
// complete AAPCS64 callee state before installing tags for the tested routines.
chos_test_setjmp_registers:
    sub sp,sp,#192
    stp x19,x20,[sp,#0]
    stp x21,x22,[sp,#16]
    stp x23,x24,[sp,#32]
    stp x25,x26,[sp,#48]
    stp x27,x28,[sp,#64]
    stp x29,x30,[sp,#80]
    stp d8,d9,[sp,#96]
    stp d10,d11,[sp,#112]
    stp d12,d13,[sp,#128]
    stp d14,d15,[sp,#144]
    stp x0,x1,[sp,#160]
    str w2,[sp,#176]
    mov x9,sp
    str x9,[sp,#184]
.macro TAG reg,value
    movz \reg,#\value
    movk \reg,#\value,lsl #48
.endm
    TAG x19,0x1919
    TAG x20,0x2020
    TAG x21,0x2121
    TAG x22,0x2222
    TAG x23,0x2323
    TAG x24,0x2424
    TAG x25,0x2525
    TAG x26,0x2626
    TAG x27,0x2727
    TAG x28,0x2828
    TAG x29,0x2929
.macro FPTAG reg,value
    TAG x9,\value
    fmov \reg,x9
.endm
    FPTAG d8,0x0808
    FPTAG d9,0x0909
    FPTAG d10,0x1010
    FPTAG d11,0x1111
    FPTAG d12,0x1212
    FPTAG d13,0x1313
    FPTAG d14,0x1414
    FPTAG d15,0x1515
    bl setjmp
    cbnz w0,2f
    // Clobber every tagged register and enter a deeper,unreturned stack frame.
    mov x19,xzr
    mov x20,xzr
    mov x21,xzr
    mov x22,xzr
    mov x23,xzr
    mov x24,xzr
    mov x25,xzr
    mov x26,xzr
    mov x27,xzr
    mov x28,xzr
    mov x29,xzr
    fmov d8,xzr
    fmov d9,xzr
    fmov d10,xzr
    fmov d11,xzr
    fmov d12,xzr
    fmov d13,xzr
    fmov d14,xzr
    fmov d15,xzr
    sub sp,sp,#64
    ldr x0,[sp,#224]
    ldr w1,[sp,#240]
    bl longjmp
    brk #1
2:
    ldr x1,[sp,#168]
    stp x19,x20,[x1,#0]
    stp x21,x22,[x1,#16]
    stp x23,x24,[x1,#32]
    stp x25,x26,[x1,#48]
    stp x27,x28,[x1,#64]
    str x29,[x1,#80]
    mov x9,sp
    str x9,[x1,#88]
    stp d8,d9,[x1,#96]
    stp d10,d11,[x1,#112]
    stp d12,d13,[x1,#128]
    stp d14,d15,[x1,#144]
    str w0,[x1,#160]
    ldr x10,[sp,#184]
    cmp x9,x10
    cset x9,eq
    str x9,[x1,#168]
    ldp d8,d9,[sp,#96]
    ldp d10,d11,[sp,#112]
    ldp d12,d13,[sp,#128]
    ldp d14,d15,[sp,#144]
    ldp x19,x20,[sp,#0]
    ldp x21,x22,[sp,#16]
    ldp x23,x24,[sp,#32]
    ldp x25,x26,[sp,#48]
    ldp x27,x28,[sp,#64]
    ldp x29,x30,[sp,#80]
    add sp,sp,#192
    ret
