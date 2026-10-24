# CHOS 0.26.0-dev — integrated hardware and installation foundations

This is an integrated development snapshot based on the UEFI/GOP desktop path that the user has confirmed can boot in VMware Fusion. Native VMware input and networking remain unfinished.

## Implemented in this snapshot

- Stable UEFI boot-information ABI, ACPI parsing, GOP desktop, PMM/heap bootstrap, and safe post-ExitBootServices handoff retained.
- PCI ECAM configuration access helpers for aligned reads/writes, assigned memory-BAR decoding, and explicit memory-decode/bus-master enablement for a matched native driver.
- Experimental polled PCI NVMe backend: bounded controller reset/readiness, 4 KiB queue/page handling, admin queues, Identify Controller/Namespace, one I/O queue pair, 512-byte LBA reads/writes and flush. All data transfers use a single-page bounce buffer and at most eight sectors per command.
- Host-testable NVMe CAP and namespace identify parsing; unsupported page-size requirements, metadata-bearing namespaces, unsupported LBA formats, malformed ranges, and missing namespaces fail closed.
- UEFI startup attempts native NVMe after PMM and heap initialization and attaches the block device only after successful driver setup. It never formats a disk automatically. Existing CHFS is mounted only if it is directly present at LBA 0.
- Separate QEMU ET_EXEC and UEFI ET_DYN build modes preserved.

## Not complete yet

- Native xHCI host-controller, USB hub/device enumeration, USB HID keyboard/mouse drivers.
- VMware vmxnet3 networking; the existing VirtIO network stack remains QEMU platform-specific.
- GPT partition discovery/creation, FAT32 ESP formatting/file copy, CHOS installation wizard, and verified boot from an installed disk.
- Runtime testing on VMware Fusion and QEMU. This build environment can compile and run host tests but does not provide AArch64 QEMU firmware/runtime.

## Safety notes

The NVMe driver is experimental and must be runtime-validated on a disposable test disk before any destructive installer work. It recognizes only namespace ID 1 with 512-byte logical blocks and no metadata. No formatting occurs automatically. If a controller's BAR is not covered by an EFI ReservedMemory/MMIO descriptor, the driver declines to access it.

## Build

```sh
make clean
make CC=clang LD=ld.lld
make uefi-iso CC=clang LD=ld.lld
make test
```

`make uefi-iso` produces `build/chos-0.26.0-dev.iso`. QEMU's direct generic-loader workflow remains `build/chos.elf` (ET_EXEC); the UEFI ISO embeds a separate relocatable kernel.


## Storage and installation image foundation

- A read-only GPT parser validates header and entry-array CRC32, usable ranges, partition bounds, and overlap.
- A bounded block-device slice isolates filesystem I/O to one partition.
- The UEFI path probes a compatible PCI NVMe controller in polled mode and attaches namespace 1 only when it supports 512-byte logical blocks. It never auto-formats.
- `tools/mkchos_install_disk.py` builds a GPT disk image containing a FAT32 EFI System Partition and a CHFS data partition. The tool writes only to a validated regular image file; it refuses symlinks, device nodes, and protected system paths, and validates primary/backup GPT entry arrays and CRCs. `make install-disk-image` creates a 256 MiB image containing the UEFI loader/kernel plus browser and file-manager app payloads.

## Known limitations

This is not yet a full VMware-compatible or interactive installable release: native USB xHCI/HID and vmxnet3 drivers are not complete, NVMe runtime behavior has not been verified in Fusion, and the graphical on-device installation wizard/partition editor is not implemented. The host-side installed-image builder is a useful bootable-image foundation, not an interactive installer. Test with disposable virtual disks only.
