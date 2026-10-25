# Install CHOS from the ISO

Use a disposable virtual disk while evaluating this development build. Back up
anything valuable. Installation erases every existing partition on the selected
target; cancellation after writing starts leaves a partial installation.

## Fusion and supported targets

On Apple Silicon, use an ARM64 UEFI Fusion VM with one CPU and 2 GiB RAM. Attach
the current ISO and a disposable NVMe virtual disk of at least 130 MiB (256 MiB
or larger is recommended). The current installer supports exactly one NVMe
controller, namespace 1 and 512-byte logical sectors. It refuses ambiguous or
truncated controller inventories and unverified IOMMU topology. Other storage
controllers and multiple-target selection are not implemented. Do not attach a
valuable disk as the installation target. The user confirms c4672fb installs and boots disk-only in Fusion. Later power,
automatic-write and network changes still need a Fusion retest.

## Existing volumes without installation

Open **Diagnostics** and select **Detect NVMe (read only)**. A valid CHFS volume
can be read without enabling writes. A blank disk remains blank. To modify an
existing CHFS volume, select **Enable CHFS writes...**, then **Confirm file writes**.
Neither action formats the disk. An installed NVMe boot volume is discovered and mounted writable automatically
after GPT/CHFS mount and live device identity/geometry validation. Failed validation
leaves recovery available. This never formats a disk. Manual ISO discovery remains
read-only until explicitly enabled.

## Installation wizard

1. Open **Diagnostics → Install CHOS...**, or press **I** on the desktop.
2. Read Welcome and choose Next. No target has been selected and no disk writes
   have been authorized.
3. Explicitly select the NVMe target. Check its capacity, opaque device identity
   and the existing-data warning. Media type is shown as unknown when it cannot
   be established. The current NVMe boot volume is protected from installation.
4. Review the GPT plan: 1 MiB alignment, a 64 MiB FAT32 EFI System Partition,
   and CHFS in the remaining usable space.
5. Read the loss-of-all-data warning. Select Acknowledge, then Install. The live
   controller/namespace identity and geometry are checked again before writing.
6. Wait for actual write/flush/readback progress and final filesystem/boot-file
   verification. Setup does not declare success after an error. Cancel stops
   future steps; it does not restore overwritten data. A retry requires selecting
   the target and confirming again; controller quarantine may require reboot.
7. After **Installation completed and verified**, shut down the VM, detach the
   ISO and boot from the installed NVMe disk alone.
8. CHFS files and settings are writable automatically on installed boots. Verify
   a test file survives Restart. Setup is hidden on installed boots; use the ISO
   for reinstall/recovery.

Tab changes focus; Enter/Space activates the focused control; Escape cancels.
Pointer controls use the same native input path as the desktop. Closing Setup
while installation is active has the same partial-disk cancellation semantics.
The installer owns storage exclusively while writing/formatting/verifying.

## Resolution and browser

Settings changes the logical desktop size immediately on firmware framebuffers.
After booting the installed system, the same choice saves a checked
resolution request in the ESP. Restart to apply a physical GOP mode offered by
firmware. Unsupported, corrupt or failed requests retain a safe firmware mode;
Fusion window resizing and arbitrary physical mode changes without reboot are
not implemented. The ISO itself is read-only, so boot-resolution persistence
requires the installed disk.

The browser opens through the UEFI cooperative runtime and retains HTTP, links,
redirects and text-layout support. HTTPS remains unavailable until TLS/certificate
verification exists. Opening the browser does not establish network-adapter
support. Native VMXNET3 (PCI 15ad:07b0) is now implemented and tested in QEMU;
the new build still requires a Fusion network retest. Other adapter IDs need their
own backend. Run `pci net` and `netstatus` in Console to inspect adapters, driver
status, DHCP, IPv4, gateway and DNS. A session resolver override is available as
`dnsserver A.B.C.D` after DHCP binds; it resets at reboot and does not add TLS.

See [the validation report](STAGE2_VALIDATION.md) for exact commands, results,
known limits and external target checks. Compare the source build ID and verify
the published SHA256SUMS before testing a download.

## Restart and shutdown

Restart flushes writable storage and invokes the PSCI SYSTEM_RESET service declared
by the checksummed ACPI FADT (or direct-boot FDT). It does not jump into the old
kernel. Shutdown displays “Shutting down...” for at least five seconds, flushes
storage and requests PSCI SYSTEM_OFF. A flush failure or returned/absent firmware
service reports an error and keeps the desktop usable. Power actions are refused
during active installation; cancel Setup first. No UEFI protocol callback is used
after ExitBootServices.

## Console history

Wheel up/down and Page Up/Page Down scroll retained output. Home selects the oldest
retained rows; End returns to the live tail. Typing returns to the current prompt,
and clear removes history. The built-in UEFI Console retains 512 older rows; the
EL0 Console retains 256 older rows within its existing 16-page image limit. Oldest
rows are evicted when full. Command history on Up/Down remains separate.

## Updating an earlier development installation

There is no automatic installed-system updater. Booting a new ISO does not replace
the kernel on an older installed disk. Evaluate this build using a fresh virtual
disk; preserve the existing VM/data if needed. Reinstalling erases the selected
disk's partitions and CHFS data after the wizard's confirmation.
