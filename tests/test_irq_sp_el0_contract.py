#!/usr/bin/env python3
"""Guard the assembly/C exception-frame contract for the per-task EL0 SP."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "kernel/arch/aarch64/vectors.S").read_text()
header = (ROOT / "include/chos/irq_context.h").read_text()

# IRQ and lower-EL synchronous vectors must both capture SP_EL0 before C runs.
assert source.count("mrs x0, sp_el0") == 2
assert source.count("str x0, [sp, #272]") == 2
# The common restore path must restore the selected frame's SP_EL0 before ERET.
assert "ldr x16, [sp, #272]" in source
assert "msr sp_el0, x16" in source
assert source.count("sub sp, sp, #288") == 2
assert "add sp, sp, #288" in source
# C and assembly must agree on offsets and keep the frame call-aligned.
assert "offsetof(chos_irq_context_t, sp_el0) == 272u" in header
assert "sizeof(chos_irq_context_t) == 288u" in header
print("IRQ frame contract: SP_EL0 is captured/restored and C/assembly offsets agree")
