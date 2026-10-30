# Native ARM64 interpreter platform boundary

The actual pinned JavaScriptCore build now compiles and links its two offlineasm
metadata extractors, generates native ARM64 settings/offsets and compiles its
original LowLevelInterpreter assembly object. The complete JavaScriptCore target
still fails at the missing standard `<chrono>` header. Generated tables, metadata
executables and an interpreter object do not establish a complete engine link,
JavaScript execution, WebAssembly execution or browser rendering.

The selected profile already disables JIT, DFG and FTL. Its assembler header is
also needed by interpreter metadata generation. CHOS has no dynamic executable
allocator or code-publication/cache-flush backend. The no-JIT header path now
uses a release assertion if called at runtime. It terminates through native
`abort` with exit134; it never reports a successful cache flush. A compile probe
that enables JIT retains the explicit missing-backend diagnostic. This is an
unsupported-feature boundary, not an implementation of instruction-cache flushing.

Native offlineasm now selects ELF GOT relocations for global addresses. The
common assembly macros select ELF hidden symbols and GNU local labels under the
explicit CHOS predicate. Linux feature identity and system APIs are not enabled.
The guest fixture executes a small representative GOT/local-label sequence,
checks that its result is the actual native global's address and modifies that
global through it. The pre-link object must declare the helper global/hidden;
the final static linker may localize a hidden symbol. The fixture then launches
a child that calls the actual upstream ARM64Assembler cacheFlush method and
checks exact exit134 and exited state. The parent remains runnable. Three
complete process lifecycles must return to the cold physical-page baseline.

The original compile failures and two incorrect fixture expectations are retained.
The first fixture assumed hidden symbols would remain global after final linking.
The second assumed a release assertion would raise a CPU exception rather than
use native abort. Both checks now match the actual contracts: hidden visibility
is checked before and after linking, and abort's exact exit is required. Neither
failure is counted as a passing guest qualification.

The maintained patch and manifest preserve original/post-image hashes and the
pinned source identity. The checked builder verifies those inputs, rebuilds the
actual WTF archive, preserves the full debug ELF and verifies that strip-debug
keeps identical loaded bytes, permissions, entry and ABI notes. Its negative-JIT
probe requires the specific missing-backend diagnostic. The machine-readable
[qualification](validation/stage3-jsc-arm64-boundary.json) records accepted guest
profiles, generated/object hashes and failures. The SDK/kernel/runtime are
unchanged from the preceding image-protection checkpoint with full host/sanitizer
and13-profile acceptance. New component tests add direct/UEFI guard, linked WTF
filesystem/freeze and IPv4/IPv6 TLS coverage.

Full native Thread/GC services, an engine-sized resource profile, remaining
filesystem/browser integration, native Pi5 SDIO/Wi-Fi and production security/
platform acceptance remain open. No new Fusion/Pi5/laptop qualification or user
binary milestone is claimed. Development version remains0.30.0.
