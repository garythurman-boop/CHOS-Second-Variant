#ifndef CHOS_IRQ_CONTEXT_H
#define CHOS_IRQ_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Exception frame created by vectors.S for IRQs and lower-EL synchronous traps.
 * Keep offsets in sync with assembly. SP_EL0 is task-local state: the scheduler
 * must save/restore it when switching between independently executing EL0 apps.
 * The 816-byte size preserves the AAPCS64 16-byte stack alignment.
 */
typedef struct {
    uint64_t x[31];
    uint64_t elr;
    uint64_t spsr;
    uint64_t esr;
    uint64_t sp_el0;
    uint64_t reserved;
    uint64_t q[32][2]; /* Complete128-bit FP/SIMD registers, including upper halves. */
    uint64_t fpcr,fpsr;
} __attribute__((aligned(16))) chos_irq_context_t;

_Static_assert(offsetof(chos_irq_context_t, x) == 0u, "IRQ GPR frame offset mismatch");
_Static_assert(offsetof(chos_irq_context_t, elr) == 248u, "IRQ ELR frame offset mismatch");
_Static_assert(offsetof(chos_irq_context_t, spsr) == 256u, "IRQ SPSR frame offset mismatch");
_Static_assert(offsetof(chos_irq_context_t, esr) == 264u, "IRQ ESR frame offset mismatch");
_Static_assert(offsetof(chos_irq_context_t, sp_el0) == 272u, "IRQ SP_EL0 offset mismatch");
_Static_assert(offsetof(chos_irq_context_t, reserved) == 280u, "IRQ reserved offset mismatch");
_Static_assert(offsetof(chos_irq_context_t,q)==288u,"FP/SIMD frame offset mismatch");
_Static_assert(offsetof(chos_irq_context_t,fpcr)==800u,"FPCR frame offset mismatch");
_Static_assert(offsetof(chos_irq_context_t,fpsr)==808u,"FPSR frame offset mismatch");
_Static_assert(sizeof(chos_irq_context_t) == 816u, "IRQ frame size must be816 bytes");
_Static_assert(_Alignof(chos_irq_context_t)==16u,"IRQ frame alignment");

#endif
