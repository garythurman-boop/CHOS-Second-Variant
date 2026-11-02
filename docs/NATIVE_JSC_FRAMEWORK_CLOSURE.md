# Native JSC static framework closure and VM link

The original upstream static framework has two required parts: its141-member
JavaScriptCore archive and53 separate object-library members. The latter target
is named `JavaScriptCoreJIT` even with JIT, DFG and FTL disabled: it also owns
bytecode generation, LLInt slow paths and other interpreter code. A successful
archive-only build is not the complete static framework or a linked VM.

All53 additional original members now compile for native CHOS. The diagnostic
collects the exact object target dependencies and cross-checks them against the
original upstream shell link interface, excluding only that shell's main object.
It uses no directory glob, replacement bytecode implementation or symbol stub.
The141 original archive members remain byte-identical to the preceding qualified
checkpoint. JIT/DFG/FTL/sampling remain OFF and WebAssembly remains enabled.

The real public C API probe links context creation/release, JavaScript/JSON,
full original ICU Intl and GC calls. Including the separate objects reduces its
initial undefined-symbol list from367 to five:

- Compiler runtime `__muloti4`.
- Binary128 long-double `hypotl`.
- Actual foreign `Thread::suspend`, `Thread::resume` and `Thread::getRegisters`.

The subsequent [native VM math component](NATIVE_VM_MATH.md) closes the first two
symbols with original upstream routines and native vector qualification. The
current VM link still lacks all three foreign-thread methods.

The initial linker additionally rejects a proposed50,659,328-byte image under
the current4MiB application bound. That image also overlaps the current anonymous
mapping base16MiB above the application base. Increasing one constant cannot
admit it safely: loader/storage limits, address layout, physical-page ownership,
per-process/global memory budgets, stacks and scheduling need one coherent
resource profile with cleanup and pressure tests. Current32KiB worker defaults
also fail the engine's64KiB GC reserve. The generic original ThreadSuspendLocker
already supplies a real global lock; the missing foreign operations must provide
actual stable target contexts, not invented register snapshots or successful
no-ops. Full ICU data is deliberately linked instead of its bootstrap stub.

[Exact build/link evidence](validation/stage3-jsc-framework-closure.json) records
the original object identities and all linker failures. The link failure is a
retained blocker, not a passing VM qualification. No probe output is executed,
admitted or published, and no native arena assertion is weakened.

```sh
python3 tools/probe_jsc_vm_link.py --source /path/to/pinned-webkit \
  --native-sdk build/native-sdk --engine-build /path/to/native-engine/cmake \
  --out /tmp/chos-vm-link
```

Use the prepared upstream build environment, including its Ruby standard-library
paths. The tool returns the actual build/link error status and retains logs,
map, CMake commands, original object queries and hashes. Even a future successful
link is reported as LINKED_NOT_EXECUTED and requires separate resource admission
and native runtime/JavaScript/Intl/WebAssembly tests. Full WebCore/browser,
Pi5 Wi-Fi, production security and platform acceptance remain open. Development
stays0.30.0; this does not complete Stage3 or justify version0.50.0.

The [native foreign-pause/WTF coordination component](NATIVE_FOREIGN_GC.md) now
supersedes earlier missing-foreign-method diagnoses: the full VM link has no
undefined symbols. Its image remains unadmitted; full VM/GC/browser execution and
the remaining revised Stage3 gates are still open.
