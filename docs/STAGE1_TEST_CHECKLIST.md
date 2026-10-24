# Build expectations and Stage 1 test checklist

Current main is a development kernel, desktop and driver test platform. The original
`prebuilt/` files are the old 0.30.0 baseline. Rebuild current source or use a package
whose source revision and SHA-256 manifest you have checked. The ISO filename alone
does not identify its source revision; boot diagnostics include `[BUILD]`.

## What to expect

| Configuration | Demonstrated behavior | Limits |
|---|---|---|
| QEMU direct ELF | Classic desktop; VirtIO keyboard/mouse/wheel; window drag/maximize/restore; CHFS writes/flush/reboot persistence; DHCP/ARP/ICMP; real RNG receipt and EL0 validation | QEMU virt, one CPU; MMIO transport; no arbitrary free-form resize |
| QEMU ARM64 UEFI ISO | AAVMF/ACPI/GICv3/GOP; native xHCI keyboard/relative mouse/absolute tablet; nested full-speed USB hubs, hotplug, held-input release; PCI VirtIO disk/network/RNG | Modern PCI; identity DMA with MMU/cache off; kernel GUI apps; HS/SS hub live testing pending |
| QEMU prepared image | Firmware boots EFI files from the disposable image; kernel attaches its GPT CHFS partition through PCI VirtIO | Image preparation is not an on-device installer |
| Fusion / M1 | Previous user screenshot proves EFI/GOP entry and a failing splash/input path; corrected code builds | New ISO needs actual Fusion retest; native NIC/storage/entropy depend on its inventory |
| Pi 5 / ARM64 laptop | Platform discovery requirements and missing families documented | Actual platform testing blocked; no universal compatibility claim |

Prototype apps include Console, File Manager, Settings, Diagnostics and text viewing.
General UEFI EL0 process execution, TLS/HTTPS, an on-device installer, native Wi-Fi,
SMMU ownership and accelerated physical GPUs are not established by Stage 1. NVMe
probing remains disabled in normal builds. Use disposable virtual disks.

## Automated checks

Source the cloud tool activation script, then use the exact targets in AGENTS.md.
`make test` and `make test-sanitize` include descriptor/ring/controller fault tests,
PCI capability and resource bounds, GIC EOI/IRQ behavior, GUI redraw interleaving,
block completion/timeout failures, entropy bounds and the existing regression suites.
`make qemu-smoke` exercises the direct serial path.

Use a fresh `--out` directory for every live run; failures and artifacts are retained:

```sh
python3 tests/qemu_platform.py --out /workspace/chos-evidence/direct-current
python3 tests/qemu_platform.py --mode uefi --pci-virtio --tablet --hub-depth 2 \
  --memory 2G --firmware /path/to/AAVMF_CODE.fd \
  --out /workspace/chos-evidence/uefi-current
python3 tests/qemu_platform.py --mode installed --pci-virtio --tablet \
  --disk build/chos-0.30.0-installed.img --memory 2G \
  --firmware /path/to/AAVMF_CODE.fd --out /workspace/chos-evidence/installed-current
```

On this cloud machine append `--qemu-data /workspace/chos-tools/root/usr/share/qemu`
and use `/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd` as firmware.
The optional `--stress 120` adds mixed real input and flushed filesystem transactions.
`--nvme` requires an explicitly compiled experimental kernel/ISO; `--storage-error`
adds QEMU blkdebug write failures on a disposable image. Do not pass that kernel to
a valuable disk. Normal artifacts keep probing disabled.

Also run `--no-rng` to prove explicit entropy failure, relative-pointer variants,
and the documented deliberately unrouted timer build. Normal performance runs omit
`--trace`. The runner injects actual emulated USB/VirtIO hardware input, reads guest
state for synchronization, verifies exact window geometry, and captures screenshots;
it does not inject normalized events into CHOS APIs.

## Fusion retest

Use one CPU and 2 GiB RAM initially, matching the reported M1 VM. Boot the current
ISO, wait for the splash to finish, and record the build revision. Open Console with
`C`. Run `pci` and `bootlog`; retain the controller IDs, USB root/route/VID/PID lines,
errors and timer-verification message. Missing-device failure must leave the desktop
usable. With no supported input, capture the visible log and VM configuration.

Check typing, Shift/Ctrl/Alt and release, rollover, pointer movement, all buttons,
wheel, title-bar drag, maximize/restore, desktop icons, menus and dialog cancellation.
Unplug/reconnect the pointer and keyboard; unplug with a key/button held and confirm
it is released. Test both a relative mouse and absolute tablet if Fusion offers them.
Confirm idle CPU drops and input still responds after five minutes.

On a supported disposable disk, use `write /TEST.TXT text`, `sync`, `type /TEST.TXT`
and a fresh VM boot to verify persistence. Do not format an unrecognized disk.
`rng-test` must receive hardware bytes or report unavailable/pending; repeat a pending
request. `netstatus`, `ping 10.0.2.2` (QEMU gateway only), then `netstatus` verify
actual network progress. On Fusion ping its reported gateway instead. A ready driver
or DHCP lease alone does not prove Internet/TLS support.

Send the actual PCI inventory before selecting vmxnet3 or another native network
backend. Pi 5 needs its board/firmware/DT and RP1 ownership data; laptops need exact
model, ACPI/IORT inventory and recovery access. Test only the unverified target gates
on those machines; QEMU evidence cannot close them.
