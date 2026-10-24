# Stage 1 validation and handoff

Stage 1 remains **incomplete as a cross-platform acceptance gate**. The direct QEMU
VirtIO workflow and a native USB UEFI workflow are implemented and tested locally.
Fusion, Pi 5 and ARM64 laptop tests are BLOCKED by unavailable hardware. There are
also software gaps that can be implemented and tested in QEMU now; see
[build expectations and test checklist](STAGE1_TEST_CHECKLIST.md). Native USB
hubs, absolute tablets, arbitrary DMA/IOMMU platforms and vmxnet3 remain unsupported
or unimplemented; this report does not describe universal USB/platform compatibility.

## Source and baseline preservation

The uploaded ZIP is untouched. Its SHA-256 and original prebuilt copies/check results
are in `/workspace/chos-evidence/baseline/`. Original prebuilt SHA256SUMS all passed.
Local commit `93729cb` imports the supplied source before Stage 1 edits. The first
Stage 1 source commit `d15e25f` was pushed to GitHub `main`, and its remote SHA was
verified. The remote was empty at onboarding. Subsequent completed iterations are
committed and pushed to main under the user's standing instruction. New cloud-task
restoration/publication remains unverified; retain the prepared snapshot/export too.

Baseline clean direct, UEFI ISO and prepared-image builds passed, as did aggregate
`make test` and `make test-sanitize`; this machine did not reproduce the old handoff's
aggregate timeout. Baseline direct smoke passed after installing QEMU's relocatable
modules correctly. The initial missing `virtio-gpu-device` error was local tool setup,
not a kernel failure. The baseline guest reported a VMM self-test warning with 512 MiB;
the final guest passes that same check after excluding identity RAM from alias candidates
and checking the actual advertised RAM end.

## Final results

All final checks below passed. Logs use the `verified-` prefix under
`/workspace/chos-evidence/`; the package includes these logs and per-VM evidence.

| Check | Result | Evidence |
|---|---|---|
| Clean direct, UEFI ISO, prepared-image build | PASS | `verified-build.log` |
| HID parser target | PASS | `verified-hid.log` |
| Aggregate host regression | PASS: 39 host binaries, 13 Python test-script invocations, build/layout/API probes | `verified-tests.log` |
| Configured ASan/UBSan suite | PASS: 38 host binaries | `verified-sanitize.log` |
| Additional NVMe parser ASan/UBSan | PASS | `verified-nvme-parse-sanitize.log` |
| Direct serial smoke | PASS | `verified-smoke.log` |
| Direct GUI/storage/network/EL0 entropy/reboot | PASS | `verified-direct/result.json` |
| Missing RNG, including live EL0 failure behavior | PASS | `verified-no-rng/result.json` |
| Native USB UEFI ISO | PASS: keyboard, relative mouse, wheel, windows, 20 reconnects | `verified-usb/result.json` |
| Prepared image firmware boot/native USB | PASS: same input/window/reconnect checks | `verified-installed/result.json` |
| Reusable setup script repeated | PASS | `verified-setup-repeat.log` |

QEMU test inputs are copied and SHA-256 recorded before startup. No current suite
is reported as a timeout or zero-test success. Investigation failures are retained
and explained below; hardware exclusions remain separate.

## Methods and commands

Source `/workspace/chos-tools/activate` and work in `/workspace/CHOS-Second-Variant`.
Compiler: Debian Clang/LLD 19.1.7. Emulator: Debian QEMU 10.0.13, software emulation,
Cortex-A72, one CPU, 512 MiB. Firmware: Debian AAVMF 2025.02-8+deb13u1. Packages were
verified through signed Debian metadata; no TLS/checksum/signature bypass was used.

```
make clean
make CC=clang LD=ld.lld -j4 all uefi-iso install-disk-image build/qemu-smoke-disk.img
make test-usb-hid CC=clang LD=ld.lld
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
make qemu-smoke CC=clang LD=ld.lld
python3 tests/qemu_platform.py --out /workspace/chos-evidence/NEW-DIRECT \
  --qemu-data /workspace/chos-tools/root/usr/share/qemu
python3 tests/qemu_platform.py --mode uefi --out /workspace/chos-evidence/NEW-USB \
  --firmware /workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd \
  --qemu-data /workspace/chos-tools/root/usr/share/qemu
python3 tests/qemu_platform.py --mode installed --disk build/chos-0.30.0-installed.img \
  --out /workspace/chos-evidence/NEW-INSTALLED \
  --firmware /workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd \
  --qemu-data /workspace/chos-tools/root/usr/share/qemu
```

The runner refuses existing result directories and copies disks/firmware variables.
QMP injects input into QEMU devices; it never calls a CHOS input routine. Screenshots
verify visible responses and large canvas changes for maximize/drag. A guest-symbol
read synchronizes on the completed minimum splash interval, avoiding animation being
mistaken for input response. Reboot checks start a new emulator on the same disposable
disk. Serial logs, command arrays, screenshots and result JSON are retained per run.

## Changes and coverage

* Native PCI xHCI transport, root enumeration and control transfers fetch device,
  configuration and HID report descriptors. Boot-capable HID interfaces select report
  protocol. Interrupt-IN transfers decode keyboard arrays/modifiers/report IDs and
  signed relative pointer/buttons/wheel into the same GUI functions used by VirtIO.
* Host tests cover descriptor truncation/length bounds, report IDs shared across keys,
  rollover, invalid reports, signed motion, release on disconnect, ring cycles/link TRBs,
  absent ports, malformed capabilities, reset/command timeout, lost/error completion,
  endpoint stall, fatal status and DMA quarantine. ASan/UBSan run these same paths.
* PCI windows are copied before ExitBootServices; BAR translation and RAM conflicts
  are checked. Optional validated SPCR enables bounded serial diagnostics. A bounded
  retained UEFI transcript also supports debugger inspection when no serial is present.
* VirtIO block uses a permanent 4 KiB bounce buffer and cache-line-isolated queue/status
  storage. Chunking, write/read/flush, I/O errors, bad/lost completion and reset timeouts
  have host coverage; live CHFS writes survive a new guest. No real host disk was used.
* RNG repeated init preserves pending DMA; used-ring overrun is rejected. Live reads
  receive 32 bytes without dumping them; the missing-device run fails without synthesizing
  bytes. The existing random syscall checks owning process, maximum length and the entire
  writable user range before consuming entropy, then copies only the returned count.
  The dedicated live EL0 test covers null/read-only/wrapping pointers, zero/oversized
  requests, guard bytes, actual receipt and missing-device EAGAIN. Entropy quality
  is not certified.
* NVMe automatic probing is compiled out unless `CHOS_ENABLE_EXPERIMENTAL_NVME` is
  explicitly defined. No NVMe I/O result is claimed. VMware adapter inventory/vmxnet3
  and Pi/laptop platform-specific device families are not fabricated.

## Performance observations

Corrected synchronized runs (`baseline-full-1`, `verified-direct`, `verified-usb`) measured:

| Method | Baseline direct | Final direct sample | Native USB UEFI sample |
|---|---:|---:|---:|
| Kernel/console milestone from QEMU launch | 0.134 s | 0.160 s | 6.361 s |
| Desktop after enforced minimum splash | 3.217 s | 3.244 s | 9.441 s |
| Keyboard shortcut to screenshot change | 55.62 ms | 50.96 ms | 26.13 ms |
| Relative pointer to screenshot change | 28.61 ms | 25.64 ms | 31.40 ms |
| Maximize visible response (includes deliberate button delays) | 271.34 ms | 272.00 ms | 266.87 ms |
| Drag visible response (includes injection delays) | 313.76 ms | 311.42 ms | 309.67 ms |
| QEMU process CPU over 2 s desktop idle | 50.99% | 1.50% | 1.00% |

These are short shared-cloud observations, not latency percentiles or benchmarks.
QMP/screenshot overhead, injected 40–60 ms button delays, scheduling and firmware boot
are included. Small differences do not establish a regression. Timer-based WFI replaces
the measured hot polling loop. Missing/NAKing USB input does not allocate, retry reset,
or busy-wait. Controller reset/commands/control transfers have deadlines and iteration
caps; mock no-response tests terminate and quarantine. General interactive window resize
is absent; maximize/restore is the implemented operation tested here.

Early `direct-final-1` failed because the runner sampled netstatus before the asynchronous
ping reply and used the wrong expected state name; subsequent queries verified a real
reply. `direct-full-1` correctly failed when synchronization let the splash consume input.
The runner was fixed to await actual splash completion. During EL0 test expansion,
`direct-el0-1` also timed out on its GUI shortcut while build outputs were being
replaced. Runs now copy/hash immutable kernels/ISOs and read symbols from that exact
copy. Three subsequent EL0/input repetitions passed. These earlier unsynchronized
samples are superseded, not acceptance evidence; their logs remain.

`usb-wheel-1` failed safely because a new conservative IORT guard treated QEMU's
MSI-only ITS routing table as an SMMU. The parser now validates bounded ITS/root
nodes and still refuses SMMU/unknown/malformed nodes. Host tests and a subsequent
native USB/wheel run passed. This was an implementation defect fixed locally.

## Outstanding validation and limits

* BLOCKED: Fusion/Apple Silicon PCI/USB inventory, keyboard/pointer/network/storage and
  timeout/recovery tests; Pi 5 and laptop discovery/boot/device tests. See hardware matrix.
* NOT IMPLEMENTED: hub topology/transaction translators, absolute USB tablets, IOMMU
  ownership and native vmxnet3. Directly attached QEMU relative HID is the declared profile.
* NOT RUN: physical NVMe I/O and audio; optional high-resolution display/mode changes;
  statistical entropy evaluation. USB wheel and malformed reports have host coverage;
  live wheel reports are also verified through QEMU transport into shared input.
  Long-document visual scrolling remains covered by host GUI tests rather than this
  live profile.
* UEFI without a supplied FDT does not attach existing fixed-aperture VirtIO block/net/RNG
  drivers. The tested prepared image boots its EFI files, but kernel CHFS attachment in
  this profile and an on-device installer are not claimed.

See `docs/DRIVER_STATUS.md` for exact bounded failure/ownership contracts. Review and
hardware expansion are still required before treating this new low-level driver as stable.
