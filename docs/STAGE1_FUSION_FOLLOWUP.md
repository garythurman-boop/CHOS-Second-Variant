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
