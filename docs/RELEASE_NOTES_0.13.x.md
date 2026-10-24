# CHOS 0.13.x release notes

This document collects the 0.13.x development release series to date. These are development milestones. The user subsequently reported that the 0.13.1 context fix resolved the previously observed EL0 fault in their run scenario.

## 0.13.1-dev — preserve EL0 process context

- Save `SP_EL0` into every IRQ and lower-EL synchronous exception frame and restore the selected frame's value before exception return. This prevents stack state from being shared across separately scheduled EL0 applications.
- Expand the exception frame to 288 bytes, preserving 16-byte stack alignment. Initialize the new fields in synthetic scheduler frames.
- Add C-layout assertions and regression coverage for SP_EL0 frame offsets and the assembly entry/return contract.
- Correct version reporting in the kernel, graphical Console and bundled EL0 Console.
- Remove unused negotiated-feature storage that triggered `-Werror` during the release build.

## 0.13.0-dev — desktop hit testing and Ethernet foundation

- Align minimize, maximize/restore and close button hitboxes to the same physical geometry used to draw the titlebar buttons at UI scales 1x, 2x and 3x. Regression coverage includes edge exclusion and minimized windows.
- Add a polled VirtIO-MMIO network device driver for QEMU, including MAC/link diagnostics, bounded eight-entry RX/TX virtqueues, raw Ethernet frame transmission/reception, completion timeouts and packet counters.
- Add allocation-free, bounds-checked Ethernet, ARP, IPv4 length/checksum and UDP parsers with malformed/truncated packet tests.
- Add serial `netinfo` and attach QEMU user-mode networking to the run targets.

## 0.13.x scope summary

**Available in the source:** core window-control hit-testing correction; VirtIO Ethernet device slice and raw-frame operations; packet parsers and tests; `netinfo`; exception-frame SP_EL0 preservation in 0.13.1-dev. The code has passed the host regression suite and clean AArch64 `-Werror` compilation in the authoring environment.

**Not yet a usable Internet stack:** DHCP, fully configured IPv4 connectivity, robust ARP neighbor resolution for ordinary peers, ICMP behavior, DNS, TCP, user-facing UDP/TCP socket syscalls, TLS, certificate validation and browser support. VirtIO NIC presence alone does not imply Internet access.

**Acceptance note:** after applying 0.13.1-dev, the user reported that the graphical Console `run` fault was fixed. The original authoring environment did not independently replay the same QEMU session.
