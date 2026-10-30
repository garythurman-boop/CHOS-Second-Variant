/* Native AAPCS64 test harness. Preserve the caller's callee-saves, make the
 * incoming heap roots live in x19..x28, and exclude this caller frame
 * (including the reference vector) from the captured callee stack interval. */
.text
.balign 4
.global capture_seeded_registers
.type capture_seeded_registers,%function
capture_seeded_registers:
 sub sp,sp,#112
 stp x19,x20,[sp,#0]
 stp x21,x22,[sp,#16]
 stp x23,x24,[sp,#32]
 stp x25,x26,[sp,#48]
 stp x27,x28,[sp,#64]
 stp x29,x30,[sp,#80]
 str x1,[sp,#96]
 mov x9,x0
 ldp x19,x20,[x9,#0]
 ldp x21,x22,[x9,#16]
 ldp x23,x24,[x9,#32]
 ldp x25,x26,[x9,#48]
 ldp x27,x28,[x9,#64]
 ldr x0,[sp,#96]
 mov x1,sp
 mov x2,xzr
 mov x3,xzr
 mov x4,xzr
 mov x5,xzr
 mov x6,xzr
 mov x7,xzr
 mov x8,xzr
 mov x9,xzr
 mov x10,xzr
 mov x11,xzr
 mov x12,xzr
 mov x13,xzr
 mov x14,xzr
 mov x15,xzr
 mov x16,xzr
 mov x17,xzr
 bl capture_actual_registers
 ldp x19,x20,[sp,#0]
 ldp x21,x22,[sp,#16]
 ldp x23,x24,[sp,#32]
 ldp x25,x26,[sp,#48]
 ldp x27,x28,[sp,#64]
 ldp x29,x30,[sp,#80]
 add sp,sp,#112
 ret
.size capture_seeded_registers,.-capture_seeded_registers
