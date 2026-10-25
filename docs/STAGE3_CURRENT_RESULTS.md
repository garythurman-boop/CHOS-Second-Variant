# Revised Stage 3 current evidence

Stage 3 is **not complete**. The revised prompt's modern browser, verified TLS,
complete isolation/runtime and real Pi 5 wireless requirements remain active.
The earlier machine-readable [results](validation/stage3-input-ipv6.json) preserve
that component baseline and open gates and distinguish target reports from automated guest evidence.

The user confirms Fusion Ethernet and example.com after the prior 54b9731 build.
Typing build **d91d135** is published with held Backspace/characters/navigation,
500 ms initial delay, 25 Hz repeats and focus-aware blinking carets. All ten
published files were downloaded back and checksum-verified. Nine clean-build
guest profiles, full host/sanitizer suites and direct smoke passed for that build.

The subsequent IPv6 source iteration compiles a pinned, unmodified lwIP 2.2.1
dependency closure alongside the retained IPv4 stack. It passes actual QEMU
DAD/SLAAC/router/neighbor/echo traffic on Intel 82574L, VMXNET3 and PCI VirtIO,
plus direct VirtIO. Link loss invalidates state and reconnect repeats DAD/SLAAC.
IPv4 DHCP/echo and DNS/HTTP pass on each supported backend. This is IPv6 control
plane evidence, not IPv6 web browsing, DNS6/DHCPv6 or public Internet validation.

Fourteen accepted guest profiles also cover Network Manager configuration/save/
rollback/reconnect, keyboard/pointer typing, real EL0 IPv4/IPv6 status and IPv6
echo, protected profiles, guarded allocation/fault/reaping, original platform
behavior and graphical install/disk-only boot/persistence/reset/power-off.
Installer completion was 20.926 s, its largest measured step 18.508 ms and actual
shutdown 5.035 s in this shared-cloud sample. Memory returned to the same 117,999
free physical pages after normal, faulted and scheduled applications. These are
component observations, not modern browser benchmarks or Fusion certification.

For that earlier IPv6 control-plane baseline, compiled kernel source is **54bc0f5**. The final fixture commit **e1bb871** changes
only typing-test synchronization; kernel, headers, userland, dependency and build
inputs are identical. One parallel typing run sampled the browser before its last
queued character was consumed. Its failure is retained, and the corrected test
waits for all 36 characters before measuring held-key deletion; that rerun passes.
Full host/sanitizer and direct smoke checks pass on the clean compiled source.

That IPv6 follow-up was source-only under the then-current alternating artifact cadence.
The latest downloadable ISO remains d91d135 and contains the typing fixes; it does
not contain this later IPv6 implementation. See [IPv6 scope](IPV6_CONTROL_PLANE.md),
[typing behavior](TEXT_INPUT.md) and [Pi 5 wireless evidence](PI5_WIRELESS.md).

The user reports the tested build works without significant issues. Plain I
opening Setup was identified as an unwanted Welcome-view hotkey and removed.
This target report does not cover source-only IPv6 or future untested features.
New build publication is now held until every revised Stage3 gate is met and
thoroughly tested; this supersedes the earlier alternating binary cadence.

Native executable runtime progress is now documented in NATIVE_EXECUTABLES.md.
Actual direct-boot ELF applications exercise sparse RX/R/RW mappings,256KiB BSS,
arguments and user-only faults with complete reaping. Modern engine/runtime,
verified TLS, real Pi5 Wi-Fi and the other full-stage gates remain open.

See validation/stage3-native-runtime.json for local native-runtime evidence and
retained failures. Sparse-page accounting and the obsolete installer fixture are
corrected. One parallel UEFI typing run timed out at the firmware banner before
kernel entry; the standalone rerun passes, but that original failure remains
NOT PASS and its exact pre-kernel cause is unresolved. Full qualification is not
claimed. Release-hold tests reject forced publication; all binaries remain held.

## Subsequent runtime and transport source work

The UEFI CPU/process profile now runs actual isolated applications in QEMU,
including deliberate RO, XN, sparse-hole and kernel-text access faults, timer
scheduling and complete deferred frame cleanup. The high-address fixture boots
the same kernel above 2 GiB and retains native high-BAR USB. See
[UEFI_PROCESS_RUNTIME.md](UEFI_PROCESS_RUNTIME.md). Identity DMA with data caching
off remains an explicit narrow profile; native IOMMU ownership is still absent.
Fusion must independently validate this new runtime source.

A real [native C allocation/string layer](NATIVE_C_RUNTIME.md) runs at EL0 using
owned MAP/UNMAP pages, including alignment/overflow/failure behavior and exhaustion
without corrupting live allocations. It is not full libc/C++/POSIX. Native text
editing now preserves held Backspace, caret blinking and durable bounded saves;
ROMFS/binary/oversized previews stay read-only. The HTTP recovery browser batches
bounded input events so key-up/motion traffic cannot require a full redraw for
every queued event. It remains an HTTP preview, with HTTPS blocked.

[IPv6 transport results](validation/stage3-ipv6-transport.json) add actual TCP6
streams and AAAA over UDP6 with RDNSS lifetime/withdrawal, hostile-response rejection,
process ownership, bounded retries and fail-closed entropy absence. This does not
make the recovery browser IPv6-capable or implement DHCPv6/manual IPv6 settings.

[SDIO/CYW protocol implementation](SDIO_CYW_PROTOCOL.md) adds real bounded command,
identity, backplane, framing and NVRAM primitives. These have host/sanitizer and
AArch64 compilation evidence. Pi 5 host-controller/regulator/pin/clock ownership,
firmware startup, radio authentication and actual scan/connect remain unimplemented;
no live wireless support is claimed.

Known regressions exposed by UEFI applications are retained with their fixes:
standalone Text Viewer lacked editing; browser redrew too often and lost queued
input; native Console's added state-name pointer table was invalid for relocation-
free CHCX and faulted only that application. The table was replaced by inline
characters. The established privileged UEFI desktop Console is preserved for
pci/driver/clock/bootlog diagnostics; the limited standalone Console remains an
explicit isolated application. No administrative network setter was added to EL0.

Test inputs now verify that symbols match the kernel actually embedded in each ISO
or installed ESP before launching QEMU. Earlier network fixture failures caused by
a mismatched copied kernel are not Ethernet failures or accepted evidence. Corrected
matched tests verify actual DHCP/DNS/HTTP traffic through isolated browser windows.

## Mandatory gates still open

* Integrate a maintained genuine modern engine and demonstrate HTML/CSS/JavaScript,
  browser features and conformance in CHOS. No engine currently renders in CHOS.
* Complete engine-sized memory/runtime, libc/C++/threads/futex/IPC/events/loader and
  web-content permission/isolation boundaries. Current static ELF and tiny process
  budgets are prerequisites rather than a browser-ready runtime.
* Implement maintained TLS, trust roots/update policy, certificate/hostname/date
  validation, reliable entropy on Fusion and negative security tests.
* Implement actual Pi 5 radio/firmware/authentication, scanning, connection manager,
  secure credentials and live AP/reconnect/failure tests.
* Finish IPv6 browser integration, DHCPv6 where required, manual IPv6 settings,
  multiple-interface policy and the complete network settings/service requirements.
* Run engine conformance/security/live-site tests, measured browser responsiveness
  and resource-pressure benchmarks, and independent Fusion/Pi/laptop qualification
  where those environments are available.

None of these software gaps is closed merely by unavailable hardware. The binary
publication hold remains active. Existing d91d135 downloads remain unchanged.

The [consolidated local runtime record](validation/stage3-uefi-runtime.json)
includes exact input manifests, matched binaries, accepted component profiles and
retained failures. It explicitly leaves full Stage 3 and binary release incomplete.
