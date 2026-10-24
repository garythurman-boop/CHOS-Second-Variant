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
