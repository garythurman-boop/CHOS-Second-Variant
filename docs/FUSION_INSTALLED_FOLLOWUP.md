# Installed Fusion system: power, storage, Ethernet and Console

The user confirms build c4672fb installs through the GUI to NVMe and boots with the
ISO removed. Restart then stalled, writes required manual enablement, and DHCP was
offline. This follow-up preserves that installation path and addresses those gaps.
The user's screenshot shows VMware PCI bridges (15ad:07a0); the network adapter
line was outside the retained visible tail. The new native NIC backend claims only
15ad:07b0 / Ethernet. It does not treat every VMware PCI function as a NIC.

## Changes

* Firmware reset/power-off uses PSCI declared in a checksummed ACPI FADT, or a
  supported PSCI FDT node in direct boot. The old jump back into the kernel lost
  the UEFI boot context and did not reset device ownership. The old shutdown only
  halted. Neither behavior is retained as a misleading fallback.
* Restart flushes writable storage. Shutdown shows “Shutting down...” for at least
  five architectural-counter seconds, idles on timer IRQs when possible and flushes
  again immediately before requesting SYSTEM_OFF. An absent/returned service or
  failed flush reports an error and returns to the desktop. Active installation
  must be cancelled before a power action.
* A validated installed NVMe/CHFS volume becomes writable automatically after live
  identity/capacity checks. Invalid mounts retain recovery. Manual discovery from
  the ISO is read-only by default; blank disks are never autoformatted. The Setup
  button and shortcut are hidden on installed boot; destructive recovery stays on
  the ISO. No unimplemented Reset control is offered.
* `chos_net_device_ops_t` provides readiness, status, error and Ethernet TX/RX
  callbacks. Existing VirtIO and new VMXNET3 use the same IP/DHCP/DNS/TCP stack.
  New hardware needs its own matching backend and resource/DMA checks, not browser
  or IP-stack rewrites. Claimed failure remains owned; no automatic DMA fallback.
* VMXNET3 uses revision 1/UPT 1, one TX/RX queue, 32-entry aligned permanent rings,
  full-frame bounce buffers and no offloads. Commands/producer indices are polled;
  interrupts stay masked. Wrong completion indices, queue IDs, fragmented layouts,
  device errors and bounded TX timeout quarantine DMA and disable PCI mastering.
  Standard 1500-byte MTU only; no native IOMMU mapping, MMU/cache-on DMA or hotplug.
* The UEFI browser adapter now maps no-data/busy TCP to EAGAIN, distinguishes peer
  EOF after draining data, and preserves retry/error semantics for send and close.
  Previously an empty receive poll prematurely parsed an empty HTTP response.
  HTTPS remains blocked; verified TLS is Stage 3 work.
* `pci` logs every stored function (up to the existing checked inventory limit) in
  compact form; `pci net` filters adapters. `netstatus` shows backend/error/DHCP/IP/
  gateway/DNS. `dnsserver IP` supplies an explicit session-only unicast resolver
  after DHCP binds, refused while a query is active. DHCP renewals preserve that
  override; reboot resets it.
* Console scrollback uses fixed-size bounded rings: 512 historical built-in rows,
  256 EL0 rows. Wheel, Page Up/Down, Home/End, return-to-prompt on typing and clear
  work. Scroll events now reach focused client windows, including EL0 Console and
  browser. Oldest rows are evicted; an existing viewport remains anchored until
  its oldest data is evicted. Up/Down command history is unchanged.

## Validation and scope

`lifecycle-install2` passed graphical install, disk-only auto-writable boot, saved
file/settings, actual in-process firmware reset and power-off after a 5.039 s visible
shutdown interval. Installer timing: 20.858 s, maximum bounded step 15.865 ms. These
are individual shared-cloud observations, not performance guarantees.

`combined-host1.log`, `combined-sanitize1.log`, `net-device-host.log` and
`combined-smoke1.log` pass. They include power/FADT/FDT, installer fault cases,
Ethernet binding/failure ownership, VMXNET3 rings/bounds/timeout, browser EAGAIN/EOF,
scrollback eviction and real GUI history navigation. See final delivery evidence
for the exact committed build and runtime run table.

`tests/qemu_network.py` runs a local HTTP redirect/body fixture and a deterministic
UDP DNS fixture over the actual emulated NIC. It also checks HTTPS stays blocked.
The VMXNET3 profile uses NVMe and no VirtIO RNG/network/block transport; the VirtIO
profile checks the existing backend. The DNS fixture listens on loopback port 53
(the cloud allows unprivileged binding); run in isolation with that port free.
Tests select it through the real Console `dnsserver` command. Default upstream DNS
is not silently replaced in production; public DNS/site access is not certified by
this fixture. Mutable test disks, firmware variables and network captures are not
published. The fixture verifies protocol behavior rather than public-site access.

Runtime commands (fresh disposable output directories required):

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld -j4 all uefi-iso install-disk-image
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
python3 tests/qemu_installer.py --out /workspace/chos-evidence/NEW-INSTALL --keyboard --supported-mode
python3 tests/qemu_installer.py --out /workspace/chos-evidence/NEW-ERROR --write-fault
python3 tests/qemu_network.py --out /workspace/chos-evidence/NEW-VMX
python3 tests/qemu_network.py --out /workspace/chos-evidence/NEW-VIRTIO --net-model virtio
python3 tests/qemu_console.py --out /workspace/chos-evidence/NEW-CONSOLE
python3 tests/qemu_console.py --out /workspace/chos-evidence/NEW-EL0 --mode direct
```

Investigation failures are retained: the first VMXNET3 run rejected QEMU's type-0
RX completions. Revision-1 parsing now accepts types 0/3 with all phase, queue,
index, SOP/EOP and length checks intact. An attempted rerun after a naming-edit
compile failure used an old snapshot; it is not acceptance evidence. HTTP tests
then found the UEFI empty-read/EOF bug above. Default cloud DNS did not answer;
the deterministic loopback DNS fixture now proves actual queue/stack exchange.
Console-runner comparisons initially included tablet cursor movement at the top
edge, then used a static-struct symbol eliminated by compiler scalar replacement;
corrected checks compare visible Console output and navigation without that symbol.

QEMU VMXNET3 behavior was inspected in upstream QEMU v10.0.0 `hw/net/vmxnet3.c` and
its interface definitions. The CHOS backend is independently written register/byte
encoding; no QEMU/Linux driver implementation or GPL source is vendored. The separate
Test-CHOS reference informed the PSCI metadata approach, remaining a separate project.

Fusion power/network retest remains external. If `pci net` identifies another NIC,
that adapter needs its own backend. New Fusion driver support, public internet/TLS,
entropy on Fusion, arbitrary disks, Pi/laptop and native IOMMU support are not
established by these QEMU results. Stage 3 still needs verified TLS, trust roots,
secure entropy and justified certificate time; no verification is bypassed here.

The combined runtime iteration passes VMXNET3 and VirtIO DNS/HTTP/redirect fixtures,
native nested-USB/VMXNET3 traffic/storage regression, direct QEMU regression,
UEFI Console (`combined-console-uefi2`) and EL0 Console navigation.
`combined-install` passes the complete installation lifecycle: 20.963 s
installation, 24.593 ms maximum step, 5.037 s shutdown.
`combined-install-error` passes injected write failure with no false success and
responsive UI. Final delivery checks below identify the committed source.

## Final delivery: 107ef51

Clean source `107ef511cb076969bbc45a9fd162ff07db280304` passes all eight final
profiles in `followup-final-runs.json`: VMXNET3 HTTP/DNS with no usable VirtIO
transport; VirtIO HTTP/DNS; UEFI and EL0 Console scrollback; keyboard installation
with physical-mode persistence, automatic writes, real reset and power-off; injected
NVMe write error; nested USB with VMXNET3/storage/RNG; direct boot regression.
Final install: 20.969 s, maximum step 13.203 ms, shutdown status 5.034 s. Clean
aggregate host, ASan/UBSan and direct smoke checks pass. The handoff contains
`FOLLOWUP_FINAL_RESULTS.json`, immutable input hashes, logs and screenshots.
Actual Fusion retest of this new build remains pending. The prior user-confirmed
installation result is preserved separately.
