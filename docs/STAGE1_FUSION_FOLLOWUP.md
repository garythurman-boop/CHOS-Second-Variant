# Stage 1 Fusion follow-up

Reported configuration: Fusion 26H1u1, macOS 27, M1 MacBook Air, one virtual CPU,
2 GiB RAM. The user reports that the previous QEMU build passes the manual criteria.
The Fusion screenshot shows EFI/GOP boot, xHCI controller startup, zero HID devices,
`USB invalid device descriptor or unsupported hub`, and a stationary splash after
IRQ delivery was enabled. This is useful runtime evidence of a failing Fusion path;
it is not evidence that Fusion input or timer delivery works.

## Iteration 1: timer contracts and absolute HID

* GICv3 explicitly selects combined EOI/deactivation rather than inheriting firmware
  split mode while acknowledging with EOIR alone. Both non-secure Group 1 enable
  bits are programmed to cover the distributor's security views.
* Before UEFI WFI idle, the kernel proves that at least two timer interrupts arrive,
  using the independent architectural counter and a bounded iteration cap. Failure
  disables the unverified timer path and keeps the GUI polling. A terminal kernel
  exception now also writes a bounded GOP/retained-log diagnostic, instead of relying
  on a potentially absent serial UART.
* USB HID logical ranges are retained with correct unsigned maximum semantics;
  absolute X/Y reports normalize to the shared 0..32767 pointer interface. Keyboard,
  relative pointer, buttons, wheel and report-ID behavior are preserved.
* Live tests now inspect the foreground window's actual geometry and ID. Fixed
  coordinates previously could minimize/focus a different window and still satisfy a
  screenshot-difference assertion. Those older maximize/restore assertions alone
  are superseded by the stronger check; host window-manager coverage remains valid.

Evidence under `/workspace/chos-evidence/stage1b/`:

* `iteration1-host.log`, `iteration1-sanitize.log`, `iteration1-smoke.log`: PASS.
* `gic-test-build.log`: host and sanitizer GIC tests PASS for inherited split EOI,
  both security views, repeated PPI delivery and bounded setup failure.
* `tablet-host.log`: descriptor ranges, unsigned maxima and malformed range rejection PASS.
* `tablet-final-gic/result.json`: 2 GiB UEFI absolute tablet, keyboard/wheel and 20
  reconnects PASS. Follow-up tests use the corrected window-geometry assertions.
* `timer-unrouted-live/result.json`: deliberately unrouted timer falls back to polling;
  desktop/input/reconnect remain functional. This forced degraded mode consumes a
  CPU; normal measured timer-driven idle remains low. No efficient idle guarantee is
  made with a deliberately broken interrupt controller.

The GIC defects are concrete architectural problems. They are plausible contributors
 to the Fusion splash freeze, but Fusion verification of their correction is pending.
USB hub enumeration remains a separate required fix; a controller running with zero
HID devices must not be described as working keyboard/mouse support.

### GUI redraw interleaving correction

A new host regression invalidates the scene during an in-progress GPU presentation.
The old implementation loses that invalidation when it unconditionally clears the
boolean dirty flag. Running the new regression against the prior committed GUI source
fails (`gui-generation-before.log`); the generation-based fix passes the GUI host and
sanitizer suites. The compositor now schedules another frame if a task/syscall changed
the scene during presentation. This is a concrete in-environment defect, not a hardware
blocker. `direct-generation/result.json` and `tablet-generation/result.json` pass with
actual foreground-window ID/geometry checks. Full updated host and sanitizer suites
also pass (`iteration1-final-host.log`, `iteration1-final-sanitize.log`).

## Iteration 2: USB hub topology and retained diagnostics

The xHCI path now supports bounded root/hub topology, class hub descriptors,
configuration, per-port power/status/change acknowledgement, asynchronous debounce
and reset, route strings, HS single-TT child addressing and recursive child-first
teardown. Standard SS hub depth/context handling is implemented but has no live SS
hub evidence here. The QEMU hub model exercises full-speed hub topology; that does
not establish physical HS/SS hub compatibility.

Limits are 16 owned slots, up to 255 advertised root ports (validated within the BAR),
32 ports/hub and five route nibbles of depth. Interrupt payloads up to 1024 bytes are
bounded by port speed. Unsupported SS burst/stream companion capabilities are rejected.
Ring segments, input/output contexts and data buffers use alignment that prevents
crossing 64 KiB DMA boundaries. Invalid ring states are rejected before doorbells.
Control-transfer timeouts cancel the owning slot when the command queue still works;
a command queue timeout quarantines the controller. Failed port reset/overcurrent
states do not trigger endless reinitialization.

Live evidence:

* `hub-live-2`, `hub-relative-1`: one hub, absolute/relative pointer variants and 20
  downstream mouse/tablet reconnects PASS.
* `hub-nested-1`: two hubs plus keyboard/tablet PASS.
* `hub-subtree-1` and `hub-aligned-live`: nested hubs, 20 downstream reconnects,
  whole-subtree unplug/reconnect and held Shift/button release PASS.
* `hub-fault-test.log`: host and sanitizer coverage includes descriptor truncation,
  stuck downstream reset, overcurrent, recursive teardown and lost control TD cleanup.

A controller starting with no attached HID is now labelled as controller status,
not successful keyboard/mouse support. Retained USB logs identify root, route, slot,
VID/PID and attachment/failure. After startup these messages no longer draw over the
desktop. In the fallback kernel Console, `bootlog` shows retained initialization/USB
messages and `pci` filters the PCI inventory. This gives Fusion a diagnostic path even
when no supported serial UART exists. Boot logs include the source revision and dirty
state; generated artifacts continue to carry SHA-256 hashes in the delivery manifest.

## Iteration 3: modern PCI VirtIO in ACPI-only UEFI

A standards-discovered PCI capability transport now shares the device queue drivers
with the original FDT MMIO path. Common/notification/ISR/device regions must fit a
sized firmware-assigned BAR. Capability loops, malformed lengths, unaligned regions,
invalid queue/notification offsets, RAM conflicts and unverified IORT translation
are rejected. The upper dword of a 64-bit BAR cannot be treated as another BAR.
PCI operation currently requires identity DMA with MMU/cache off. Legacy-only PCI
and general IOMMU mapping remain unsupported. Firmware runtime still uses kernel
GUI applications; this change does not establish generic UEFI EL0 processes.

The fallback Console adds `write PATH TEXT`, `sync`, `rng-test`, `netstatus` and
`ping a.b.c.d`. Entropy receipt is verified through actual device completion and
never derived from timers. No file system is automatically formatted.

`pci-virtio-live1` and `pci-virtio-debug` failed DHCP/TX during development; evidence
is retained. A PCI queue-enable zero write differs from the MMIO reset vocabulary;
the PCI adapter now ignores zero queue-ready writes and uses device reset.
`pci-virtio-trace` then received DHCP, wrote/flushed/read CHFS and received entropy,
but failed because the test attempted to read a compiler-split static symbol. The
runner now checks the actual Console ping result. `pci-virtio-live2` passes without
tracing: nested USB hubs/tablet, PCI disk persistence after a fresh boot, hardware
RNG and DHCP/ARP/ICMP. `pci-direct-regression` also passes with MMIO storage/network,
EL0 entropy validation and the stronger window geometry checks.

The runner uses copied disks and firmware variables and hashes its kernel/ISO. The
PCI profiles never write a host physical disk. `--trace` is optional diagnostic
QEMU tracing and is absent in normal performance runs. Host/sanitizer suites pass
(`pci-final-host.log`, `pci-final-sanitize.log`).

`pci-no-rng` passes with explicit entropy unavailability while storage, networking,
input and reboot persistence continue working. `pci-prepared-live2` passes firmware
boot plus actual kernel CHFS partition write/flush/read/reboot, PCI network/RNG and
USB hub/tablet checks. An earlier `pci-prepared-live.log` records a mistyped image
filename before QEMU launched, corrected using the Makefile's actual artifact path.
Measured normal idle was 2% of one host CPU over the two-second observation in the
ISO PCI and MMIO regression runs. Pointer visible response was about 45 ms including
the runner's 40 ms post-input delay; these are emulator observations, not Fusion
latency measurements.
