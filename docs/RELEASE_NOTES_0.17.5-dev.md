# CHOS 0.17.5-dev — VirtIO-net v1 header correction

## Root cause

The PCAP from the live QEMU run showed malformed outgoing Ethernet frames. The intended broadcast destination was shifted so the captured frame appeared to have destination `ff:ff:ff:ff:52:54` and EtherType `0x4500`. The driver negotiates `VIRTIO_F_VERSION_1` but previously supplied/removed only a 10-byte network header. Modern VirtIO 1.x uses the 12-byte `virtio_net_hdr_v1`, whose final field is `num_buffers`; the two-byte mismatch caused QEMU to treat the first two Ethernet bytes as part of the transport header.

## Changes

- Introduced `include/chos/virtio_net_abi.h` as the shared definition of the 12-byte VirtIO 1.x network header.
- TX descriptors now supply 12 bytes of header; RX buffers reserve 12 bytes and the receive path skips 12 bytes before returning the Ethernet frame.
- Added ABI/alignment regression tests for header size, `num_buffers` offset, broadcast destination and IPv4 EtherType.
- `netinfo` reports the driver-configured RX queue size and the raw QueueNum register readback separately.

## Validation

The release must pass a clean AArch64 Clang/LLD `-Werror` build, host regression suite, ASan/UBSan suite, userland packing and fresh-extraction validation. Host tests validate framing layout but do not substitute for the final live test.

## Live test still required

Boot this build using `make run-gui-nettrace`. Inspect `chos-network.pcap`. The DHCP packet should decode as Ethernet broadcast + IPv4 + UDP source port 68 to destination port 67, rather than EtherType `0x4500`. In CHOS, verify RX frames rise above zero and `netstatus` transitions from discovering to requesting and then bound. If `QueueNum readback` still differs from the configured size, preserve both values in the report.
