# CHOS 0.22.0-dev — loader and QEMU build-mode correction (revision 2)

## Corrective changes

- Restores `build/chos.elf` to static AArch64 `ET_EXEC`, the format expected by the existing QEMU generic loader used by `make run` and `make run-gui`.
- Builds a separate UEFI-only AArch64 PIE kernel at `build/uefi-kernel/chos.elf`; the ISO packages this PIE rather than the direct-QEMU kernel.
- Adds a build-mode regression test to prevent a future UEFI relocation change from silently breaking the ordinary QEMU run path.
- The EFI loader now examines UEFI conventional-memory descriptors and attempts `AllocateAddress` in an exact candidate range inside its current identity-mapped 1–2 GiB window, instead of relying on `AllocateMaxAddress` search behavior.
- Allocation failures now report the EFI status, number of conventional ranges large enough for the image, and the largest contiguous conventional range found in the supported window.

## Remaining limitations

This corrects a confirmed build-mode regression and makes the current EFI allocation decision more diagnosable. It does not make CHOS fully VMware Fusion compatible yet: the kernel still requires the existing FDT/QEMU-virt platform path and uses QEMU-specific PL011, GICv2, and VirtIO-MMIO addresses. If the VMware loader progresses past allocation but reports that no FDT is present, that is the next known platform-support boundary, not an ISO-detection failure.

The ISO is experimental boot groundwork, not an installer. HTTPS/TLS, JavaScript, full CSS layout, and actual image rendering remain unsupported by the browser.

## Validation

A clean static QEMU kernel build and separate UEFI PIE build succeed. The UEFI ELF/relocation checks, ISO structure check, and dedicated ET_EXEC/ET_DYN build-mode regression check pass. Runtime QEMU GUI and VMware Fusion boot still require testing on the target host.
