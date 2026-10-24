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
    assert(sizeof(frame) == 36u * sizeof(uint64_t));
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
