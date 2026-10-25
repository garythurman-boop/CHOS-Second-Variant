# Revised Stage 3 current evidence

Stage 3 is **not complete**. The revised prompt's modern browser, verified TLS,
complete isolation/runtime and real Pi 5 wireless requirements remain active.
The machine-readable [results](validation/stage3-input-ipv6.json) enumerate open
gates and distinguish target reports from automated guest evidence.

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

Compiled kernel source is **54bc0f5**. The final fixture commit **e1bb871** changes
only typing-test synchronization; kernel, headers, userland, dependency and build
inputs are identical. One parallel typing run sampled the browser before its last
queued character was consumed. Its failure is retained, and the corrected test
waits for all 36 characters before measuring held-key deletion; that rerun passes.
Full host/sanitizer and direct smoke checks pass on the clean compiled source.

The IPv6 follow-up is source-only under the user's alternating artifact cadence.
The latest downloadable ISO remains d91d135 and contains the typing fixes; it does
not contain this later IPv6 implementation. See [IPv6 scope](IPV6_CONTROL_PLANE.md),
[typing behavior](TEXT_INPUT.md) and [Pi 5 wireless evidence](PI5_WIRELESS.md).

The user reports the tested build works without significant issues. Plain I
opening Setup was identified as an unwanted Welcome-view hotkey and removed.
This target report does not cover source-only IPv6 or future untested features.
New build publication is now held until every revised Stage3 gate is met and
thoroughly tested; this supersedes the earlier alternating binary cadence.
