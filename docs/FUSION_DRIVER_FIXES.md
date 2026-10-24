# Fusion timer, clock and input correction

The user confirms that revision 979fa8c boots Fusion to the desktop, but the clock
and keyboard/pointer do not work. The cloud machine cannot run Fusion. This report
distinguishes fixes reproduced locally from a target retest.

## Separate reference project

The user authorizes adapting their independently tested
[garythurman-boop/Test-CHOS](https://github.com/garythurman-boop/Test-CHOS), inspected
at commit `17cefd1`. It stays separate: no merge, submodule, build dependency, desktop
or filesystem replacement. Its MIT notice is retained in `licenses/Test-CHOS-MIT.txt`.
The useful reference is the EP0 descriptor/actual-length sequence, composite-function
selection, firmware clock snapshot, and recorded Fusion controller/device facts.
Its successful Fusion result does not automatically validate this kernel.

Reference records: `docs/fusion-observations-m7e.md`, `docs/milestone-7h.md`,
`docs/milestone-7i.md`, `docs/evidence/m7i/fusion-observations-user.json`,
`drivers/usb/{layout,xhci}.c`, and `boot/uefi/main.c`. They identify VMware xHCI
15ad:077a at bus 0a, a report-HID composite device 0e0f:0003 with two interfaces,
and USB2 hubs 0e0f:0002. Addresses are not copied into this kernel.

## Reproduced defects and corrections

* The previous UEFI GUI read QEMU's fixed PL031 address for wall time, regardless
  of firmware/platform. ABI version 5 now carries a validated EFI GetTime calendar
  converted to Unix time plus architectural-counter/frequency values captured
  before ExitBootServices. The kernel advances that snapshot without firmware
  calls, QEMU addresses, or timer-IRQ dependency. Direct boot binds the PL031 only
  from its validated FDT. GUI clock polling uses the counter and handles missed
  timer boundaries. Console `clock` identifies source and current seconds.
* The reference's recorded Fusion CAP/version word `0x00120020` has HCIVERSION
  `0x12`. Our prior strict `0x100..0x120` check rejects it. The new recorded-layout
  host fixture failed before correction (`clock-usb-targeted.log`). A bounded
  compatibility table normalizes **only** VMware 15ad:077a reporting that value to
  1.20. All BAR, block bounds, page size, reset, ownership and DMA checks remain.
  Other unknown versions continue to fail. The corrected fixture passes.
* EP0 now reads the standard eight-byte device prefix, validates its packet size,
  evaluates the endpoint context when necessary, then reads the full descriptor.
  Data-stage IOC is required; a successful status event alone cannot establish
  initialized descriptor bytes. Missing, short and invalid-residual data events
  fail with a useful error.
* A composite HID interface outside the supported input-report policy is left
  unclaimed, allowing the other keyboard/pointer interfaces to configure. One
  unsupported consumer/vendor function no longer rejects an otherwise usable
  physical device. Generic Desktop X/Y-only reports count as pointer capability.
* Modifier transitions precede letters in a combined report, so a Shift+letter
  packet does not type the letter before activating Shift.

The host controller mock now exercises actual Data/Status TRB events, EP0 packet
learning, the recorded compact Fusion register layout, a subclass-zero two-function
configuration with an unclaimed vendor function, live normalized key delivery,
and missing/short/invalid-residual completions. Host and sanitizer suites pass.
`clock-usb-targeted2.log` and `composite-test.log` retain the corrected checks.
`prefix-live1` passes native keyboard/tablet, nested hubs, PCI storage/network/RNG,
20 reconnects, whole-subtree removal and reboot persistence. The separate clock
host test covers calendar/timezone limits, counter progress/wrap and zero fixed
MMIO reads in UEFI mode. Actual Fusion retest remains required.

Aggregate host tests and sanitizers pass (`iteration1-host.log`,
`iteration1-sanitize.log`), as does direct serial smoke (`iteration1-smoke.log`).
`clock-composite-live` verifies UEFI time advancement and visible taskbar minute
rollover, plus the full nested USB, PCI storage/network/RNG and persistence profile.

## Common registry and diagnostics

The [driver acceptance guide](DRIVER_ACCEPTANCE.md) documents the reusable PCI/FDT
matching, phased resource/capability admission, priority/clean refusal, failed-owner
quarantine and runtime polling interfaces. Boot now uses this registry for real
PCI VirtIO and xHCI selection. A driver module/registration can add another PCI
backend without changes to the kernel boot loop or GUI. Existing QEMU MMIO remains.

The normal ISO automatically opens rotating driver reports if native keyboard or
pointer admission remains missing. It needs no input or storage, displays actual
PCI IDs/refusal reasons and current xHCI enumeration/control progress, and avoids
logging live input contents. `automatic-no-usb` demonstrates automatic opening and
page rotation with no USB controller, and low CPU use.

A new strict controller mock sets output endpoint/slot state like real hardware.
It fails before the reserved-field correction (`reserved-context-before.log`).
EP0 Evaluate Context now clears output-only Endpoint State; Configure Endpoint
always clears the output-only slot address/state word, not only on hubs. The
corrected host/sanitizer tests pass (`reserved-context-after.log`). This fixes a
specification violation; it is not claimed as a new Fusion measurement.

Eight interrupt-IN receives per endpoint and a bounded 1024-entry event ring now
cover bursts without leaving the endpoint unarmed until the next GUI poll. USB
held-state ownership is shared across interfaces, and recent absolute motion from
a physical device suppresses its duplicate relative stream. `queued-burst-live`
passes 32 rapid USB keypress/release pairs plus clock rollover, nested hubs,
reconnects and storage/network/entropy persistence. `direct-clock-live` separately
passes the preserved direct MMIO profile and visible FDT RTC minute rollover.

The recorded Fusion fixture reproduces the supplied CAP/HCS/HCC/offset fields;
other controller behavior is a host mock. No QEMU scenario or host fixture is
relabeled as a Fusion boot. The user has an early `81e3550` candidate while final
registry/diagnostic validation continues.

## Final local acceptance

Final source passes a clean Clang/LLD direct build, UEFI ISO and prepared-image
build, aggregate host regressions/sanitizers and direct serial smoke:
`final-clean-build.log`, `final-host.log`, `final-sanitize.log`, `final-smoke.log`.

| Live case | Result | Evidence |
|---|---|---|
| Final native USB + PCI VirtIO + clock, 120 mixed input/storage cycles | PASS; 191.908 s stress; 1.00% idle host CPU after stress; fresh-guest persistence | `final-native-stress/result.json` |
| Prepared-image PCI boot/CHFS persistence and USB input | PASS | `final-prepared/result.json` |
| Empty controller with no input | PASS; automatic report opening/rotation; 2.45% observed host CPU | `final-no-input/result.json` |
| Absent USB controller | PASS; automatic report opening/rotation | `automatic-no-usb/result.json` |
| Different NEC xHCI model behind ten bridges, actually enumerated on bus 0a | PASS; fast keys, pointer/window operations, hub reconnects and PCI persistence; 0.50% idle host CPU | `nec-bus10/result.json` |
| Deliberately unrouted timer, wall-clock minute rollover and USB input | PASS through counter/polling fallback; 99.49% host CPU is the documented degraded mode | `timer-fault-clock/result.json` |
| Preserved direct MMIO + RTC | PASS; visible minute rollover, EL0 entropy and disk/network/input regression | `direct-clock-live/result.json` |

These are shared-cloud QEMU measurements including test delays and observation
overhead, not Fusion latency/CPU measurements. The NEC/bus-0a case is a QEMU
compatibility regression, not a VMware test. Failed intermediate host fixtures are
retained with their corrections; no failed run is counted as passing.

## Fusion validation still required

Use the final revision-labelled ISO in the same Fusion VM: one CPU, 2 GiB RAM.
Verify the displayed clock advances, type combined Shift+letters, test pointer
alignment/buttons/wheel, move/maximize/restore windows, then disconnect/reconnect.
Console `clock` identifies the wall-clock source; `drivers` opens admission reports.
If keyboard/pointer admission is missing, wait five seconds after startup: the same
ISO opens reports automatically and rotates them without input. Capture the xHCI
CAP/HCS/HCC, control requested/received/completion, enumeration stage/failure,
selected driver, reason and PCI IDs. No serial setup or second ISO is needed.

Fusion compatibility remains the required target gate; only an actual target test
can confirm it. The source fixes are based on reproduced defects and the user's
separate tested reference, rather than an assumption that QEMU success closes it.
NVMe remains disabled in the standard ISO, and no native NIC is claimed without
current-project adapter inventory. The experimental NVMe path remains an explicit
opt-in legacy attachment outside the current PCI registry adapters.

## Endpoint-padded report compatibility

A final comparison with the tested reference decoder identified another acceptance
difference: it accepts a report padded to the endpoint packet size, while this
branch required an exact descriptor-defined length. A five-byte numbered mouse
report carried in an eight-byte packet failed in the new regression before the
correction (`padded-report-before.log`). The decoder now requires at least the
descriptor's logical payload and trims interpretation to that payload. The complete
wire transfer must still fit the endpoint and the 1024-byte driver bound; unknown
IDs and truncated/oversized reports still fail without state changes. Tail bytes
cannot produce input events. Corrected host/sanitizer and full regression results
are in `padded-report-after.log`, `padded-full-host.log`, `padded-full-sanitize.log`.
