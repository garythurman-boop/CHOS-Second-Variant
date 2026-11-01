# Native JSC archive and checked WebAssembly policy

The actual pinned JavaScriptCore target now builds its native static archive,
with141 object members. This closes compilation of the selected JSCOnly source
closure. It does not establish a complete VM executable, JavaScript execution,
WebAssembly execution, modern rendering or browser completion.

The preceding build failed because the WebAssembly fault handler expected
instruction/instance lookup and resumable instruction-pointer rewriting. CHOS
currently has no user signal registration or resumable fault-context service.
The native port therefore selects upstream's existing explicit bounds-checking
memory path. Every option normalization forces useWasmFaultSignalHandler and
useWasmFastMemory off. WebAssembly, its in-place interpreter and the JavaScript
LLInt remain enabled; the existing no-JIT profile remains enforced.

Native preparation/activation checks the effective option and refuses a directly
forced unsupported fault mode. It does not register a handler or return fabricated
fault-recovery success. Native null-reference fault-elision allowance is always
zero, preserving explicit checks. Other platforms retain their original handler.
Actual memory allocation, interpreter execution and out-of-bounds Wasm behavior
still require VM runtime qualification; compiling this path is insufficient.

The component executable links the actual full JSC archive, WTF and native
libraries. It executes original options initialization and numeric/Boolean
parsing, invalid-input rejection and option coherence checks. Customization and
subsequent verified option updates cannot enable fault/fast memory or JIT. Real
prepare/activate methods accept the checked policy. Nine native worker lifecycles
per process repeat initialization/read checks and return to the initialized heap
baseline. Two children bypass normalization by directly forcing fault mode;
prepare and activate must independently abort134 while the parent remains usable.
Three complete process lifecycles must return to the cold physical-page baseline
with no futex waiters. Direct and isolated UEFI QEMU profiles pass.

The fixture links bootstrap ICU only because this options/platform component
does not execute Intl or consume full data. This is not the full-data engine
runtime or a substitute for it. Configuration metadata may remain owned until
process exit; the test requires stable initialized allocations and complete
process cleanup. It does not misreport those live globals as zero allocations.

The reproducible builder verifies upstream originals, checked postimages,
native adapter hashes, SDK and actual configuration. It builds the JSC target,
links the component and verifies that debug stripping preserves entry, loaded
segments and ABI notes. The qualified executable matches the tested executable
byte for byte. The SDK, both kernels, ISO, WTF and bmalloc archives also match
the preceding thirteen-profile host/sanitizer/guest regression baseline exactly.
See [hashes, evidence and retained fixture failure](validation/stage3-jsc-checked-wasm.json).

```sh
python3 tools/build_jsc_options_wasm.py --source /path/to/pinned-webkit --native-sdk build/native-sdk --engine-build /path/to/native-engine-build --out /tmp/jsc-options
python3 tests/qemu_jsc_options_wasm.py --application /tmp/jsc-options/cmake/jsc_options_wasm.elf --out /tmp/jsc-options-direct
python3 tests/qemu_jsc_options_wasm.py --mode uefi --application /tmp/jsc-options/cmake/jsc_options_wasm.elf --out /tmp/jsc-options-uefi
```

Use fresh output directories and synchronized kernel/ISO symbols. Full native
Thread lifecycle, foreign GC coordination, engine-sized admitted resources,
remaining browser services, production security, native Pi5 Wi-Fi and independent
platform acceptance remain open. The development version remains0.30.0; no new
Fusion/Pi5/laptop, browser or Stage3/version0.50.0 qualification is claimed.

The [complete static framework closure and VM link probe](NATIVE_JSC_FRAMEWORK_CLOSURE.md)
now includes the original53-member object subtarget, separate from the141-member
archive described above. All compile; a real full-ICU VM link still fails with
five unresolved symbols and an unadmitted image. Archive compilation is not VM
link/execution or complete engine qualification.
