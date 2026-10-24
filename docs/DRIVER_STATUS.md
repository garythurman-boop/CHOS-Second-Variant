# CHOS driver status at 0.30.0

This matrix distinguishes code and host tests from runtime validation.

| Subsystem | Status | Notes |
|---|---|---|
| ARM64 UEFI loader / GOP | Implemented; structure/build tests pass | Live UEFI boot unavailable here. |
| VirtIO GPU | Existing implementation | Not runtime-verified in this environment. |
| VirtIO input (QEMU) | Existing implementation; UEFI call path guarded and connected | Must be verified in a live guest. This does not drive VMware USB. |
| VirtIO block / CHFS | Existing path; host filesystem tests pass | Guest block I/O/runtime persistence not verified here. |
| VirtIO network | Existing QEMU-oriented implementation | Guest networking not verified here. |
| VirtIO RNG | Added polled modern-MMIO driver, host-mock tests and sanitizer coverage | Hardware/DMA/cache behavior not runtime-verified. QEMU targets now attach the device. |
| PCI inventory / GPT / block slices | Existing code and host validation | Does not mean every device or disk is supported. |
| PCI NVMe | Experimental implementation and parser tests | No live controller I/O test; do not trust with important data. |
| USB HID descriptor parser | Implemented, bounded and host-tested | Parses descriptor data only; not an xHCI or USB driver. |
| USB xHCI | Not implemented | No host-controller initialization, command/event rings, transfer rings, USB enumeration or HID endpoint polling. |
| VMware keyboard/mouse over USB | Not implemented / unverified | The outstanding hardware-input milestone. |
| VMware `vmxnet3` | Not implemented | VirtIO-net support is not interchangeable with vmxnet3. |
| TLS / HTTPS | Not implemented | HTTPS parses correctly but is refused before TCP connect; no downgrade. |
| Installer block writer / GUI | Not implemented | Only a safe layout/confirmation planner and host image builder are present. |

## Driver implementation safety rules

- Validate MMIO/BAR addresses and lengths before access; avoid probing fixed addresses on a platform that did not advertise them.
- Put timeouts on controller reset, queue readiness, transfers and shutdown. Do not busy-wait forever.
- Treat DMA addresses, physical/virtual mappings, cache maintenance and alignment as explicit correctness requirements, not details that host mocks prove.
- Validate device descriptors and completion lengths before consuming data; reject malformed input.
- Fail closed when required features, interrupts, memory mappings or device readiness are missing.
- Keep driver status logs explicit about available, missing, failed, or not-yet-tested devices.
