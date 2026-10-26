# Native libc++ allocation and container profile

This is a working static ARM64 prerequisite for the browser port, not a complete
C++ standard library, JavaScriptCore build or Stage3 completion. Binaries remain
held and version0.50.0 remains reserved for full revised Stage3 acceptance.

LLVM libc++19.1.7 supplies650 original files (about3.45MB) from its official HTTPS
release archive, including its license, four runtime sources and the real header
dependency closure. The release archive SHA256, immutable matching LLVM commit,
per-file hashes and exact source set are in
[the dependency manifest](dependencies/libcxx-native.json).
No release signature verification is claimed. `tools/verify_libcxx.py` rejects
modified files or added/removed closure members. Normal builds need no network.

`make libcxx CC=clang LD=ld.lld` produces the native `libchos-c++.a` archive.
The port compiles upstream new/new_handler/new_helpers/algorithm code unchanged.
A native explicit-instantiation unit uses upstream's own ABI-v1 string template
list; it does not compile or pretend to implement string.cpp's missing floating
conversion/formatting dependencies. The checked configuration comes from the
original upstream template, with original LLVM assertion handling. Native library
headers precede C headers to preserve include_next semantics; host C++ libraries
are excluded. All guest imports resolve to CHOS code at static link time.

The qualified C++20 profile has exceptions and RTTI disabled, real regular/array/
nothrow/aligned/sized allocation and deletion, atomic new-handler publication,
array/vector/string, local RAII, unique_ptr, default integer sort/search and
lock-free atomic operations. Concurrent containers are separate objects; their
allocations serialize through the existing native pthread allocator. No shared
std::vector is advertised as thread safe. Raw pthread APIs supply the tested
three workers; the external libc++ thread header is a type/declaration seam only,
with no fake runtime success implementations. std::thread/mutex runtime support,
global constructors/atexit/static guards, exceptions, full locale/streams and
further C++ ABI work remain open. Binary128 default sorting needs the unresolved
compiler-rt `__lttf2`; that unused specialization is discarded in the qualified
link. Using unsupported runtime functions fails to link.

Allocation failure with ordinary new invokes the configured handler and then
terminates the process with status134 in this no-exceptions profile. Nothrow new
returns null after handler retry, including overflowing aligned sizes. FAST
hardening bounds violations trap in EL0 and are reaped by the kernel (observed
BRK ESR0xF2000001, exit-188), preserving the kernel and permitting a new launch.
The dense native linker retains upstream's override-detection code and boundary
symbols in the RX segment. RO and RW segments remain separate; no W+X mapping,
PT_TLS, dynamic loading or global initarray dependency is used by this fixture.

The prerequisite C filename API implements real remove/rename via owned native
syscalls. Remove falls back to rmdir only after EISDIR; null names fail EINVAL.
Protected namespaces retain EPERM. CHFS rename refuses existing destinations;
no replacement/overwrite or broader POSIX rename atomicity is promised. The native
stdio header provides EOF and these two functions only: no FILE/stream/printf
implementation. The separate TLS formatting-declaration shim keeps its original
scope and precedence; unsupported format functions remain discarded from that
verified link, rather than receiving fake implementations.

## Verification

`tests/qemu_cxx.py` installs the exact ELF on a disposable CHFS disk, records its
hash, boots an immutable kernel/ISO copy, executes three complete processes and
three worker lifecycles per process, then checks OOM abort, hardening bounds
failure and a successful clean relaunch. Vectors grow/copy/move/erase, strings
handle UTF-8 bytes, RAII counts return to zero and over-aligned arrays remain
aligned. Each successful process returns all requested native heap bytes; PMM
pages remain stable after warm-up. File readback, rename, empty/nonempty directory
removal, missing paths, protected profiles and existing destinations are tested
through real EL0 syscalls. UTF-8 byte handling is not Unicode normalization or
text shaping evidence. See [exact results](validation/stage3-native-cxx.json).

Accepted evidence under `/workspace/chos-evidence`:

- `cxx-direct2` and `cxx-uefi1`: actual native C++ runtime and failure isolation PASS.
- `cxx-host-full.log` and `cxx-sanitize-full.log`: aggregate host/ASan/UBSan PASS,
  including filename error mapping. These are not sanitizers running in the guest.
- `cxx-tls6-uefi`: concurrent certificate-verified TLS1.3 AES256-GCM over IPv6 PASS.
- `cxx-smoke.log`: direct serial smoke PASS; local ISO/prepared-image build PASS.

The first guest run passed normal operation/OOM and stopped at a test-only wrong
expectation of Unix status132 for BRK. CHOS reports its own fault code-188;
`cxx-direct1` is retained as NOT PASS. The corrected runner requires that exact
fault, completed cleanup and successful relaunch. Earlier build logs retain the
missing stdarg wrapper, intentional unused upstream parameter and freestanding
C++ main linkage corrections. An initial filename host fixture inherited glibc's
leaf attributes through host stdio declarations; those incorrectly told GCC it
could not call the fixture's mock callbacks. The fixture now uses the actual
native declarations and prefixed names, with normal/sanitizer coverage passing.
No guest behavior was changed to satisfy these test expectations.

QEMU evidence does not establish new Fusion/Pi/laptop qualification. Full C++/
POSIX/events/memory/loader support, the actual modern browser and conformance,
production trust/time/reseed policy and real Pi5 wireless remain required.

The subsequent [static-guard iteration](NATIVE_STATIC_GUARDS.md) closes the local-
initialization-guard gap with direct/UEFI contention and exit/retry evidence.
The other full C++ ABI/engine limits above remain active.

The subsequent [JavaScriptCore bring-up](JAVASCRIPTCORE_BRINGUP.md) adds six original
headers, extending the closure to656 files and qualifying the same bounded
container/allocation profile under C++23. Direct/UEFI C++23 tests and the engine's
integer128 type/alignment compile probe pass. Added headers are dependencies,
not conformance evidence for every new ranges/utility facility. Full C++23/ABI
and engine integration remain open. See validation/stage3-cxx23.json.
