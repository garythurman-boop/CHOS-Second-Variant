# Stage 2 graphical installation lifecycle

The user reports build 816aa55 boots Fusion and can use its desktop, keyboard and
pointer. Storage remains ROMFS, browser launch fails for lack of scheduled UEFI
EL0 runtime, and runtime GOP mode switching was unimplemented. The user authorizes
proceeding to Stage 2 while retaining the remaining Stage 1 platform limits.

## Implemented prerequisite corrections

The UEFI browser now shares the existing browser UI/parser/network state machine
through a cooperative kernel adapter. It runs without issuing SVC or claiming UEFI
EL0 process support. HTTP/redirect/link behavior retains the existing browser limits;
HTTPS remains unavailable without verified TLS. No VMware NIC driver is claimed.
Diagnostics recognizes native USB and the actual framebuffer backend. Explicit
NVMe detection exposes a read-only filesystem view; enabling file writes requires
another explicit enable/confirm action and live identity/geometry revalidation.
Unknown or blank disks are never automatically formatted. More than one NVMe
controller or truncated inventory is refused rather than silently choosing a disk.

GOP supports scaled logical desktop sizes at runtime while preserving the physical
firmware scanout. This is labelled desktop sizing; it is not a physical mode switch.
Persistent pre-ExitBootServices physical mode requests are implemented below.

## Real installer

Diagnostics has **Install CHOS...**; `I` on the desktop opens the same wizard.
Welcome, explicit NVMe target selection, identity/capacity/data-loss warning,
partition summary, separate acknowledgement and Install action precede all writes.
Tab changes focus, Enter/Space activates, Back revisits nondestructive pages, and
Cancel terminates. After writing starts cancellation leaves a partial disk; no
rollback is promised. Retry requires selection and confirmation again; quarantined
controllers require reboot. No inert success state advances installation.

The supported installer target is exactly one NVMe controller, namespace 1, 512-byte
sectors, a fully enumerated PCI inventory and verified identity DMA topology with
MMU/cache off. The existing PCI VirtIO runtime remains supported but is not yet an
installer target. Capacity is bounded by CHFS's 32-bit sector format. The pure
planner remains side-effect free and its cookie is not an authorization boundary.

ABI v6 carries an optional bounded sparse ESP payload in preserved EFI LoaderData.
The ISO builder generates it from the actual dependency-tracked prepared ESP, with
the matching ARM64 EFI loader and kernel. Installed ESPs omit the install payload.
The kernel validates the payload bounds/run ordering/CRC and re-identifies the live
controller/namespace at selection, confirmation and immediately before first write.

One bounded 64 KiB ESP write/flush/readback step runs per GUI pump. CHFS metadata is
also individually read-back verified. GPT backup entries/header, primary entries/
header and protective MBR are flushed and verified. The parser validates both GPT
headers and compares both arrays. Final verification checks FAT32 geometry, EFI
boot paths and MZ/AArch64 ELF headers, GPT and mounted CHFS. Completion requires all
checks. No test opens a developer host disk. CRC is corruption detection, not a
signature or authenticated boot claim.

## Evidence and reproduction

`/workspace/chos-evidence/stage2-prerequisites{2,3}` passes browser launch/close,
read-only native NVMe/CHFS attach with unchanged disk hash, and scaled GOP sizing.
`stage2-install2/result.json` passes actual ISO → GUI install → ISO detached → NVMe
boot → installed file write → fresh reboot/read. Installation measured 21.428 s in
one shared-cloud QEMU run; not a performance guarantee. Guest and installed ESP
kernel bytes match. `stage2-fault1/result.json` injects a real NVMe write error and
passes no false success, unchanged disk on that first failed write, responsive GUI
and progressing timer. The initial install attempt booted an old kernel from a
stale prepared-image payload; it is retained as failed evidence. The Make dependency
is corrected and the subsequent exact-kernel run passes.

Host installer tests cover no writes before confirmation, changed identity, changed
identity immediately before first I/O, write/short-write/short-read/read/flush faults,
removal, pre-write and mid-install cancellation, explicit reselection/retry and a
complete verified install. GPT tests include backup-header CRC, mismatched backup
location and backup-array corruption. Aggregate host and ASan/UBSan pass.

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld -j4 all uefi-iso build/qemu-smoke-disk.img
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
python3 tests/qemu_firmware_apps.py --out /workspace/chos-evidence/NEW-APPS
python3 tests/qemu_installer.py --out /workspace/chos-evidence/NEW-INSTALL
python3 tests/qemu_installer.py --out /workspace/chos-evidence/NEW-ERROR --write-fault
```

The runners create clearly named disposable image files and firmware-variable
copies. They use QEMU virt/GICv3, Cortex-A72, one CPU, 2 GiB RAM, RAMFB, native xHCI
keyboard/tablet, NVMe and AAVMF. Disk-only boot uses fresh firmware variables and no
ISO drive. Input is injected into QEMU hardware, not CHOS APIs. Physical Fusion
installation, clock and resolution retests remain external; QEMU does not certify
Fusion or complete remaining Pi/laptop/IOMMU/network work.

## Installed-volume discovery and physical resolution requests

The EFI loader records a bounded, validated device-path observation for NVMe
namespace 1. Only that boot path automatically probes/mounts the single NVMe volume
read-only; it does not autoformat or enable writes. An installed boot volume is
protected from destructive target selection. Manual write enablement still requires
confirmation and a live identity/geometry snapshot. Read-only detection, rebinding,
and display writes are blocked while the installer owns storage.

The prepared/installed ESP contains a fixed-size DISPLAY.CFG. The GUI updates only
that existing file's checked sector after explicit volume write enablement, with
snapshot revalidation, flush and readback. A bounded CRC-protected request is read
before ExitBootServices. GOP enumeration rejects unsupported formats/stride; failed
SetMode attempts restore the previous mode. Missing/corrupt/unavailable requests
leave a usable firmware mode. No firmware pointer is retained or invoked by the OS.
The kernel retains runtime desktop scaling for the read-only ISO.

`stage2-display-install{1,2}` verifies a preference round trip and unsupported-mode
fallback. The keyboard wizard run measured 20.913 s installation, three deliberate
150 ms key intervals totaling 459.06 ms, and a maximum installer step of 51.551 ms.
These are individual shared-cloud observations, not latency guarantees. A supported
physical mode and the final clean delivery bytes are verified in the final report
when their runs complete. The [installation guide](INSTALL_GUIDE.md) distinguishes
logical desktop sizing, physical mode changes on reboot and remaining target limits.

`stage2-display-supported2/result.json` passes the keyboard installer and real
1024x768 physical GOP selection after reboot, with automatic read-only CHFS boot
discovery and preserved file data. That sample measured 20.854 s installation and
a maximum 10.897 ms installer step. The earlier supported1 attempt used the wrong
height offset in the runner's pointer normalization; the guest was not clicked.
The runner now checks surface dimensions and the corrected run passes.

The installer payload requires matching source builds: a previously omitted shared
browser source file in the source-only push was corrected in c740e1e before binary
publication. Final delivery is rebuilt from committed source, not those earlier
working-tree binaries. This report does not convert older failures into passes.

## Final delivery: c4672fb

The committed delivery source is `c4672fb9af49ba221d1c3b16b26cc1a5974ebef1`.
Both kernels embed its clean build ID. The prepared image and installer payload
are dependency-tracked; publication also refuses a prepared disk missing the
exact staged EFI loader or UEFI ELF. Binary publication follows the second
implementation iteration; source changes were pushed throughout.

All locally testable Stage 2 gates PASS for the declared single-NVMe profile:

| Gate | Final evidence | Result |
|---|---|---|
| No writes before explicit selection, acknowledgement and Install | `stage2-release-install`, `stage2-release-fallback` | PASS: disk hashes unchanged before confirmation |
| Live identity/geometry checked again before writes | host installer I/O tests, actual NVMe Identify snapshots | PASS |
| Primary/backup GPT, ESP, ARM64 EFI/kernel and CHFS created/verified | guest installer, GPT/FAT/PE/ELF verification, exact installed ELF comparison | PASS |
| Failed/short I/O, bad CRC/bounds, changed identity, removal, retry | host/ASan/UBSan tests and `stage2-release-error` | PASS; no false success |
| In-guest installer followed by boot without ISO | `stage2-release-install`, `stage2-release-fallback` | PASS: fresh disk-only AAVMF boot |
| Installed file creation and persistence | same runs | PASS across a fresh reboot |
| Keyboard and pointer wizard, responsive progress | keyboard supported-mode run and pointer fallback run | PASS |
| Cancel before writes, cancel during writes, reselection | `stage2-release-cancel` plus host tests | PASS; no writes after cancellation or during reselection |
| Boot-volume protection | both installed-system runs | PASS; attempted selection leaves disk unchanged |
| Physical mode selection / safe unsupported fallback | installed ESP request read before EBS | PASS: 1024×768 selected; 1024×640 request retains safe firmware output |
| Original direct/USB/clock/storage/network/RNG behavior | `stage2-release-direct`, `stage2-release-native` | PASS |
| UEFI browser launch, read-only discovery, scaled desktop | `stage2-release-apps` | PASS |
| Terminal panic and nested-fault containment | `stage2-release-panic/{serial,no-uart}` | PASS |

The exact supported-mode installation measured 20.844 s and a maximum 27.223 ms
installer step. The pointer/fallback installation measured 20.906 s and a maximum
11.364 ms step. Deliberate key-send delays are included in recorded UI timing;
these individual shared-cloud samples are not benchmarks or latency guarantees.
The aggregate host suite, direct smoke and six publication tests pass. ASan/UBSan
passes for the same kernel C sources at a89652f; c4672fb changes only the validation
runner and publication integrity checks. The latter corrects button coordinates
relative to moved/resized windows. The earlier final-fallback attempt failed that
runner's last protection click, not the installation or persistence; the corrected
pointer run above passes the protection check as well.

Runtime evidence, exact artifact hashes and limitations are bundled with
[the versioned downloads](https://github.com/garythurman-boop/CHOS-Second-Variant/tree/build-c4672fb/artifacts/c4672fb).
No mutable test disks or firmware-variable files are published. The optional
prepared distribution image is not used as a substitute for graphical acceptance.

**External gate:** actual Fusion installation, newly implemented storage/display
and browser checks remain NOT RUN on the user's Mac. Use a disposable NVMe VM
with one CPU and 2 GiB RAM and follow INSTALL_GUIDE.md. QEMU results do not certify
Fusion, arbitrary NVMe hardware, Pi 5, laptops, IOMMU ownership, networking or TLS.
This is locally validated Stage 2 for the declared profile, not universal hardware
support or closure of every remaining Stage 1 platform gate.
