# CHOS power controls (0.9.3-dev)

The Start menu provides confirmed Restart and Shutdown actions.

- **Restart** masks exceptions, disables the EL1 MMU translation bit, invalidates EL1 stage-1 TLB entries, and branches to the CHOS boot entry. This is a kernel software restart, not a hardware reset or a firmware reset request. It must be exercised on QEMU before being treated as accepted.
- **Shutdown** masks exceptions and halts the CPU with `WFI`. The current QEMU `virt` loader path has no verified platform system-off service, so it cannot promise that the virtual machine process exits. The UI and PL011 log tell the user to close the VM from the host.
- Both actions require explicit confirmation. Press Escape to cancel. Enter confirms the current action.
- A future platform power interface may provide PSCI system-off/reset or another firmware-backed implementation, but only after a boot contract for that platform is present and tested.

The software shutdown deliberately prefers an honest, stationary CPU halt over writing an undocumented MMIO register or claiming a host power-off that the current platform cannot guarantee.
