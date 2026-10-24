# CHOS 0.17.4-dev — VirtIO-net receive diagnostics

## Why this build exists

CHOS 0.17.3-dev increased DHCP Discover/Request payloads to the 300-byte BOOTP/DHCP minimum. A live QEMU run still reported link-up and completed TX descriptors but zero received frames, so DHCP never left `discovering`. This release does not claim that DHCP is fixed; it adds diagnostics to identify whether frames reach the VirtIO RX queue.

## Changes

- `netinfo` now reports the configured RX queue size/readiness, available-ring index, raw device used-ring index, driver-consumed index, and VirtIO interrupt status.
- Added `make run-gui-nettrace`, which attaches QEMU's `filter-dump` to the `chosnet` backend and writes `chos-network.pcap` in the current directory.
- The default `run-gui` target is unchanged.
- DHCP 300-byte padding from 0.17.3-dev is retained.

## How to diagnose the next run

1. Back up `chos-disk.img`; use a copy in this source directory.
2. Run `make clean && make CC=clang LD=ld.lld`.
3. Run `make run-gui-nettrace CC=clang LD=ld.lld`.
4. In the CHOS serial shell, run `netinfo`, wait a few seconds, and run `netinfo` and `netstatus` again.
5. Exit QEMU normally to flush `chos-network.pcap`.
6. On macOS, inspect the trace with `tcpdump -nn -vv -r chos-network.pcap`; alternatively upload the PCAP for packet-level analysis.

Interpretation:

- If the PCAP contains outgoing DHCP Discover packets but no incoming Offer, the QEMU user-network backend did not return a DHCP reply; inspect the actual outgoing packet fields and backend capture.
- If the PCAP contains an incoming Offer but `RX used_idx` stays unchanged, the issue is in the guest RX queue/notification/memory path.
- If `RX used_idx` advances but `Received frames` does not, inspect descriptor lengths/IDs and packet validation.

## Validation status

- Clean AArch64 build with Clang/LLD and `-Werror`: passed.
- Focused network packet/service/state-machine tests: passed.
- ASan/UBSan suite: passed.
- Userland build and CHCX verification: passed.
- Fresh QEMU runtime validation remains necessary; QEMU is not installed in the source packaging environment.
