# Revised Stage 3: internet platform architecture

The user-provided [revised specification](STAGE3_REVISED_PROMPT.md) supersedes the
Stage 3 section of the earlier Word document. Full modern web rendering and real
wireless support remain completion requirements. This document does not certify
an engine port, Wi-Fi or an integrated production browser TLS policy on CHOS.

## Existing system to preserve

Direct and UEFI now have bounded isolated EL0 applications and a single-CPU
preemptive scheduler. UEFI retains its relocated PIE kernel, native PCI USB/NVMe/
GOP, identity DMA and disabled data caching, with CPU MMU isolation enabled in its
scheduled profile. New source still needs independent Fusion qualification.
CHCX retains its64KiB bound; static native ELF has a4MiB image span and8MiB owned
frame quota. There are four processes,32KiB main stacks and up to three secondary
join records per process. Native C allocation/errno, pthread synchronization,
private futexes, bounded process messages and explicit compiler-emulated C/C++ TLS
now have guest evidence. They do not provide engine-sized heaps/images, complete
POSIX/events/shared-memory/descriptor passing, a dynamic loader, C++ standard
library or a modern-engine sandbox. Native MbedTLS1.2/1.3 clients validate peers;
production trust/time/reseed policy and engine integration remain open. These are
implementation dependencies, not external-hardware blockers. A Linux ARM64 browser
binary cannot run on this ABI.

Preserve the validated direct/UEFI boot, installed NVMe/CHFS, genuine installer,
power lifecycle, native USB, GOP sizing and existing VirtIO paths. Networking has
DHCPv4, ARP, ICMP, bounded DNS A and four outbound TCP sockets, with single selected
VirtIO or VMXNET3 backend. The HTTP preview browser has URL/redirect/parser tests
and now correct nonblocking UEFI adapters. It remains a recovery/diagnostic app;
it is not the revised modern browser deliverable. No TLS/certificate validation,
IPv6, wireless driver/authentication or Network Manager is implemented initially.

## Immediate implementation order

1. Close Fusion's identified controller gap: PCI 8086:10d3 (82574L/e1000e).
   Keep device-specific ring/resource handling behind the common Ethernet API.
   Validate queues, link, DHCP, DNS and HTTP in an actual AArch64 QEMU e1000e guest;
   distinguish that evidence from the unavailable Fusion host.
2. Introduce an OS-owned network configuration/status service. Centralize link
   transitions, static/DHCP validation, route/resolver state and reconnection.
   The privileged shell configures it; browsers only consume sockets/status.
   Add persistent checked profiles and a functional graphical Network Manager.
3. Evaluate maintained browser engines and select a port architecture from actual
   upstream requirements. Implement the necessary isolated user runtime and I/O
   services; do not execute a modern engine or untrusted JavaScript in the UEFI
   kernel GUI loop. W^X, content isolation, bounded memory and preemption need real
   tests before hostile content is admitted. Do not replace the modern-engine goal
   with a text parser, host renderer or remote rendering service.
4. Integrate maintained, pinned TLS with verified entropy/time/trust roots, failure
   fixtures and update policy. VirtIO RNG has live QEMU evidence but Fusion entropy
   remains unavailable. Firmware-clock snapshots are not an authenticated network
   time service. HTTPS remains blocked until all verification prerequisites work.
5. Add explicitly selected wireless hardware through real bus/firmware/802.11 and
   WPA services. No device scan/profile UI may invent hardware or networks. Pi 5
   and laptop controller selection needs actual inventory/firmware evidence.
6. Expand IPv6, multiple interfaces/routes, browser APIs/conformance, downloads,
   persistent web storage and resilience; benchmark real guests and keep all
   unfinished requirements visible.

## Constraints and test matrix

The current target remains one CPU, 2 GiB RAM, identity DMA without native IOMMU.
Modern engines need larger address spaces, multiple isolated processes/threads,
substantial heap/stack growth, filesystem and IPC primitives and software-rendering
or GPU backend integration. Raising an image-size constant alone does not supply
those capabilities. Resource bounds and allocation-failure handling must accompany
runtime expansion. No modern-engine performance numbers exist yet.

| Platform | Available evidence/environment | Required independent gates |
|---|---|---|
| QEMU AArch64 direct | Live MMU/EL0, VirtIO Ethernet/RNG, CHFS, GUI | Retain regressions; add service/TLS/engine/IPv6 fixtures |
| QEMU AArch64 UEFI | Live native USB/NVMe/GOP, VirtIO/VMXNET3 | e1000e traffic, reconnect, manager, installer/power regression; isolated runtime |
| Fusion on M1 | User confirms prior lifecycle fixes and now Ethernet/example.com after 54b9731; NIC 8086:10d3 | Reconnect/failure tests; secure entropy; modern runtime/browser |
| Raspberry Pi 5 | Board unavailable | Real boot/IOMMU/DMA, Ethernet and selected Wi-Fi chipset/firmware/authentication |
| ARM64 laptop | Model/inventory unavailable | Explicit controller/firmware, Wi-Fi, ACPI/DMA, real browser/isolation |

Every completion requirement remains open until backed by working implementation
and appropriate runtime evidence. Host tests and QEMU Ethernet do not certify
Fusion, physical wireless or browser standards compliance.

The first network milestone is published as build-54b9731. The next runtime
source-only iteration adds the bounded EL0 mapping primitive documented in
USER_MEMORY.md; it does not provide a modern engine or change the full-stage
completion criteria.

The selected physical wireless target is now the Pi 5 onboard CYW43455 family,
not a Fusion virtual Ethernet adapter. See PI5_WIRELESS.md for pinned Raspberry Pi
firmware evidence and the SDIO2/platform/firmware/authentication dependencies.

IPv6 control-plane source progress is documented in IPV6_CONTROL_PLANE.md. It
preserves the IPv4 stack and adds a pinned lwIP core for actual DAD/SLAAC/ND/MLD/
ICMPv6. TCP6, DNS6/DHCP6, multi-interface routing and complete IPv6 settings remain
open; this is not full IPv6 browser support or completion of the revised stage.

The next runtime prerequisite is implemented in NATIVE_EXECUTABLES.md. Native
static CHOS-ABI ELF64 images can span up to2MiB with separate permissions and
sparse mappings; CHCX limits and existing owned-frame quotas remain. This is
validated direct EL0 support, not a complete modern browser or UEFI user runtime.
Binary publication is held by the latest user instruction until full Stage3
completion and thorough testing; source-only work continues.

Current source adds a bounded UEFI CPU/process isolation profile while retaining
identity DMA with data caching off. See [runtime contract and QEMU evidence](UEFI_PROCESS_RUNTIME.md).
This does not establish Fusion qualification for these new changes, native IOMMU
ownership, a modern browser, verified TLS or live wireless support. The binary
publication hold remains active until the complete revised Stage 3 passes.

Current runtime source now has tested [C++ synchronization](NATIVE_CXX_SYNCHRONIZATION.md)
and bounded [standard C++ threads](NATIVE_CXX_THREADS.md), in addition to the prior
native TLS, floating-point and static CRT profiles. The actual ICU threading
component runs; full target ICU and the modern engine still do not. Follow
STAGE3_CURRENT_RESULTS.md and BROWSER_ENGINE_PORT.md for current acceptance gates;
these component results do not complete the engine, security or wireless scope.

The native [ICU common library](ICU_COMMON_NATIVE.md) now builds all 202 units
and executes Unicode/file/thread/clock-error checks in direct and isolated UEFI
applications. Full data/i18n and the real engine remain open. The user now
authorizes selected verified interim test binaries; this supersedes the historical
binary-hold statements above, while full Stage3/version0.50.0 stays gated.

The [native i18n library](ICU_I18N_NATIVE.md) now builds all254 units alongside
common. Four original [normalization/UTS46 resources](ICU_NORMALIZATION_DATA_NATIVE.md)
add native Unicode18 normalization and domain-data qualification. These selected
resources are exported explicitly; they do not satisfy the full ICU data/CLDR
component. The last actual pinned JSCOnly preflight finds uc+i18n and fails for
missing data. Full data generation/packaging, engine-sized bounded memory and
subsequent engine/platform integration remain open; no engine execution is claimed.

The [default ICU data milestone](ICU_DEFAULT_DATA.md) now supplies an actual native
libicudata.a with4,321 generated resources. The complete pinned JSCOnly source
closure configures successfully against the SDK. Full embedded data is still
refused by the current2MiB executable limit; actual engine compilation stops at
missing libc++ bit-operation headers and sys/mman.h. This supersedes the earlier
missing-ICU-data configuration diagnosis. No engine execution, modern browser,
production security, live wireless or full Stage3/version0.50.0 success is claimed.

The subsequent [larger native profile and generated ICU resource execution](NATIVE_LARGE_RESOURCES.md)
qualifies1MiB allocations,4MiB images and8MiB owned frames in both QEMU boot
modes. C++23 byteswap is now supplied and tested. The historical2MiB/4MiB and
missing-byteswap limits above describe earlier snapshots. The full32MiB embedded
data package remains over budget; JavaScriptCore still requires real virtual
memory APIs and further platform work. This is not Stage3 completion.

The subsequent [native virtual-memory component](NATIVE_VIRTUAL_MEMORY.md) now
supplies owned anonymous protection/discard/partial unmap and bounded native
unistd operations. Original WebKit VM helpers execute against the SDK with a
checked CHOS discard adaptation. Actual JavaScriptCore compilation moves past
the missing memory headers to fcntl.h and stderr stream APIs; no complete engine,
modern browser or Stage3 completion is claimed.

The subsequent [native bmalloc service milestone](NATIVE_BMALLOC_SERVICES.md)
builds the actual upstream system-malloc library and executes allocation, entropy,
diagnostics and mutex paths in CHOS. Original C++23 expected/hash facilities also
run; the original numbers header's widened-double precision limitation is explicit.
Actual engine compilation now reaches WTF threading/numeric dependencies beyond
the earlier fcntl/stderr blocker. This is component evidence, not JavaScript,
rendering, production entropy, new hardware qualification or Stage3 completion.

The subsequent [native WTF synchronization and clock component](NATIVE_WTF_SERVICES.md)
executes original primitive/time headers and clock units using real private pthread
and checked clock services. Original numeric algorithms and transactional native
header import are qualified. Complete WTF/JSC compilation, Thread lifecycle/GC
suspension, engine-sized memory and all full browser/wireless/platform gates remain
open. The development version remains0.30.0; this source work does not replace the
cc29ea8 user-test ISO or imply Stage3 completion.

The subsequent [native current-thread context component](NATIVE_THREAD_CONTEXT.md)
provides real own-thread stack/GPR/SIMD/FPU snapshots with read-only capability
checks and actual AArch64 SDK assembly links. Native WTF register declarations and
current stack discovery compile. Foreign-thread suspension/GC, complete Thread
lifecycle, engine-sized stack/memory and the other modern browser/wireless/platform
requirements remain open; this is not Stage3/version0.50.0 completion.

The [native WTF entropy/process/page component](NATIVE_WTF_ENTROPY.md) now
executes actual upstream adapters in both QEMU boot modes, with explicit denied
and missing-source failure and modeled partial/retry error tests. Full engine,
production entropy security, browser, wireless and platform gates remain open.

The [native standard-stream component](NATIVE_STANDARD_STREAMS.md) supplies
checked unbuffered console output and actual upstream WTF FilePrintStream/PrintStream
execution. Normal/sanitizer,SDK links and seven guest profiles qualify the component.
Ordinary files stay read-only;complete stdio,engine/browser and hardware gates remain open.

The [native numeric/search component](NATIVE_CHARCONV.md) now executes original
libc++ charconv/Ryu and musl memmem through the actual WTF span search path.
Seven guest profiles,independent Decimal vectors and full host/sanitizer suites
qualify the bounded subset. Full WTF compilation still records17 blocked units;
engine/browser,production security,wireless and platform acceptance remain open.

The [native UTC/WTF date and UTF component](NATIVE_WTF_CALENDAR.md) executes
actual upstream parsing,conversion and process helpers with bounded native APIs.
Eight accepted guest profiles and full host/sanitizer suites qualify this component;
earlier timeout traces remain recorded. Full WTF still has eight blocked units,
and subsequent engine/browser,security,wireless and platform gates remain open.

The native engine platform now has real self-only CPU and memory accounting:
[resource service and WTF qualification](NATIVE_WTF_RESOURCES.md). Architectural
counter intervals are split at native user/kernel boundaries and paused during
blocked/idle execution. Process totals retain exited threads without exposing
foreign state; memory charges owned user frames. Complete WTF linking, GC/thread
lifecycle, JavaScript execution, browser rendering/network integration and native
Pi 5 Wi-Fi remain open. No version/completion gate changes from this prerequisite.

The [native positional-read component](NATIVE_POSITIONAL_READS.md) exposes actual
open-snapshot length/cursor and bounded cursor-independent reads. It preserves
process ownership and READ permission aliases. A live cross-page test also
corrected the shared copy-to-user helper to prevalidate every destination while
holding its AS lock through the copy. Fourteen accepted guest profiles and full
host/sanitizer checks qualify this prerequisite; writable descriptors and browser
cache/download integration remain unimplemented. No completion/version change.

The CHFS writable-descriptor prerequisite now has kernel-private runtime leases
and conditional commit, preventing a stale working copy from overwriting a newer
record. The real block I/O proxy quarantines uncertain writes until verified
remount and actual flush acknowledgement; it preserves driver descriptor/DMA
ownership. This does not yet expose writable handles or remove whole-file I/O
latency and engine storage limits. See `NATIVE_STORAGE_SNAPSHOTS.md`.

The native file service now supports bounded writable CHFS working copies with
conditional commit and explicit abort. Legacy owned read snapshots participate in
the same pre-allocation budget. No process lock is held over storage I/O; existing
whole-file storage-lock latency and CHFS format/path limits remain. Public calls
reuse existing capability classes; browser content still must be denied privileged
storage/configuration authority. Full engine filesystem adaptation remains open.

Original WTF Int128 arithmetic now has native compile/link and component-test
coverage after closing real LLVM/musl runtime imports. Optional C++ iostream
logging remains explicitly outside the selected no-localization profile, while
WTF PrintStream bodies remain original. Current full WTF compilation has five
filesystem blockers; complete engine linking/runtime, memory/GC and all modern
browser/wireless gates remain separate unfinished work.
