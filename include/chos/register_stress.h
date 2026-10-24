#ifndef CHOS_REGISTER_STRESS_H
#define CHOS_REGISTER_STRESS_H

#include <stdint.h>

/*
 * Keep unique values in x0-x30 and SP live across many timer interrupts.
 * Returns 0 if every register (including SP) retains its expected value,
 * or 1 if a mismatch is detected. FP/SIMD registers are intentionally outside
 * this kernel ABI because CHOS is compiled with -mgeneral-regs-only.
 */
int chos_register_preservation_probe(uint64_t loop_iterations);

#endif
