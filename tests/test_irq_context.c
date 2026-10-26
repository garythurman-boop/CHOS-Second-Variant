#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "chos/irq_context.h"

static void test_frame_layout(void) {
    chos_irq_context_t frame;
    size_t i;
    for (i = 0u; i < 31u; ++i) frame.x[i] = 0x100u + (uint64_t)i;
    frame.elr = UINT64_C(0x40201234);
    frame.spsr = UINT64_C(0x600003c5);
    frame.esr = UINT64_C(0x86000000);
    frame.sp_el0 = UINT64_C(0x2ffffff0);
    frame.reserved = 0u;
    assert(offsetof(chos_irq_context_t, elr) == 31u * sizeof(uint64_t));
    assert(offsetof(chos_irq_context_t, spsr) == 32u * sizeof(uint64_t));
    assert(offsetof(chos_irq_context_t, esr) == 33u * sizeof(uint64_t));
    assert(offsetof(chos_irq_context_t, sp_el0) == 34u * sizeof(uint64_t));
    assert(offsetof(chos_irq_context_t, reserved) == 35u * sizeof(uint64_t));
    assert(sizeof(frame) == 832u);
    assert(offsetof(chos_irq_context_t,q)==288u&&offsetof(chos_irq_context_t,fpcr)==800u&&offsetof(chos_irq_context_t,fpsr)==808u);
    for(i=0;i<32;i++){frame.q[i][0]=i;frame.q[i][1]=~(uint64_t)i;}
    frame.tpidr_el0=0x21001000u;frame.thread_reserved=0;
    frame.fpcr=0x400000u;frame.fpsr=0x11;
    chos_irq_context_t copy=frame;
    for(i=0;i<32;i++)assert(copy.q[i][0]==i&&copy.q[i][1]==~(uint64_t)i);
    assert(copy.tpidr_el0==frame.tpidr_el0&&copy.thread_reserved==0);
    assert(copy.fpcr==frame.fpcr&&copy.fpsr==frame.fpsr);
    assert((sizeof(frame) & 15u) == 0u);
    assert(frame.x[0] == 0x100u && frame.x[30] == 0x11eu);
    assert(frame.elr == UINT64_C(0x40201234));
    assert(frame.spsr == UINT64_C(0x600003c5));
    assert(frame.esr == UINT64_C(0x86000000));
    assert(frame.sp_el0 == UINT64_C(0x2ffffff0));
    assert(frame.reserved == 0u);
}

int main(void) {
    test_frame_layout();
    puts("irq-context: AArch64 exception frame offsets, SP_EL0 preservation and alignment ABI passed");
    return 0;
}
