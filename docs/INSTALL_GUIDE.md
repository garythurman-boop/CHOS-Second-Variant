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
valuable disk as the installation target. Fusion testing of this new installer
remains required; automated installation evidence is from QEMU/AAVMF.

## Existing volumes without installation

Open **Diagnostics** and select **Detect NVMe (read only)**. A valid CHFS volume
can be read without enabling writes. A blank disk remains blank. To modify an
existing CHFS volume, select **Enable CHFS writes...**, then **Confirm file writes**.
Neither action formats the disk. An installed NVMe boot volume is discovered and
mounted read-only at startup; the same confirmation enables writes.

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
8. CHFS files are available read-only. Enable and confirm file writes in
   Diagnostics to save documents and settings. Verify a test file survives a
   reboot before using this development filesystem for anything important.

Tab changes focus; Enter/Space activates the focused control; Escape cancels.
Pointer controls use the same native input path as the desktop. Closing Setup
while installation is active has the same partial-disk cancellation semantics.
The installer owns storage exclusively while writing/formatting/verifying.

## Resolution and browser

Settings changes the logical desktop size immediately on firmware framebuffers.
After installation and explicit write enablement, the same choice saves a checked
resolution request in the ESP. Restart to apply a physical GOP mode offered by
firmware. Unsupported, corrupt or failed requests retain a safe firmware mode;
Fusion window resizing and arbitrary physical mode changes without reboot are
not implemented. The ISO itself is read-only, so boot-resolution persistence
requires the installed disk.

The browser opens through the UEFI cooperative runtime and retains HTTP, links,
redirects and text-layout support. HTTPS remains unavailable until TLS/certificate
verification exists. Opening the browser does not establish network-adapter
support; a Fusion adapter still needs a supported native network driver.

See [the validation report](STAGE2_VALIDATION.md) for exact commands, results,
known limits and external target checks. Compare the source build ID and verify
the published SHA256SUMS before testing a download.
