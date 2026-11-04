# Native JavaScript standards and asynchronous execution checkpoint

The pinned original JavaScriptCore engine executes selected unmodified Test262
scripts, checked WebAssembly modules, asynchronous compilation and CPU watchdog
tests inside CHOS. Both direct and isolated UEFI QEMU paths are exercised. This
is an engine prerequisite: WebCore does not yet render pages, the recovery HTTP
preview remains separate, and Stage 3 remains incomplete at version 0.30.0.

## Inputs and scope

WebKit/WPE 2.54.2 is pinned to
`32717fabcb9dcad9aa00a54ef1f23e41ee64e849`. The full original ICU data and all
535 original compiled engine objects participate: 141 JSC archive members,
53 interpreter/bytecode objects, 173 WTF members and 168 bmalloc members. There
are no unresolved symbols or substituted host JavaScript engines. JIT, DFG, FTL
and sampling remain disabled; the tests execute the checked Wasm interpreter.
Engine mappings remain execute-never outside the admitted executable image.
JavaScript evaluation here uses the one-MiB engine main stack; secondary workers
retain their 128-KiB limit. General JavaScript Web Workers are not qualified by
the asynchronous Wasm compilation fixture. The manually launched diagnostic also
does not implement the future browser's reduced content-process permission policy.

Test262 is pinned independently to
`c990733f8d234dd333bd8a71c0c2eb871249b0e0`. Before execution, eight lexical
JavaScript paths were selected in each of eleven fixed category prefixes:
Array includes/toSorted, Map set, JSON parse, BigInt asIntN, RegExp property
escapes, arrow functions, classes, TypedArray slice, Promise all and Intl
DateTimeFormat. The 88 original scripts and their original required harnesses
are retained byte for byte with their license and SHA-256 manifest in
[the selected-input record](dependencies/test262-selected.json). This is a
small, explicitly selected subset, not complete Test262 conformance or a claim
that all features in these categories work.

The runner respects each script's strict/default flags, executes 172 variants
in fresh actual global contexts, and checks nine negative parse scripts against
the real SyntaxError constructor. Six asynchronous scripts produce twelve
original `$DONE` completions across their two variants. Original assertion
helpers run unchanged. The detach host hook captures and invokes the engine's
actual ArrayBuffer transfer operation; it does not simulate buffer detachment.
Missing completion, duplicate completion, asynchronous failure, malformed
metadata, unsupported flags and changed source hashes cannot produce a pass.

Separate checked Wasm fixtures exercise actual binary module validation,
32-bit arithmetic/wrap, checked memory loads and out-of-bounds RuntimeError,
memory growth with preserved bytes and zero-filled new memory, old-buffer
detachment, table calls/growth and maximum-limit rejection. A genuine
CompileError is required for the invalid module. Wasm exceptions must remain
language exceptions, rather than terminating the EL0 process through a fault.
These are selected operations, not the full upstream Wasm suite.

## Real main loop and script watchdog

The asynchronous fixture dispatches on the actual native WTF main RunLoop,
instantiates Wasm bytes, compiles bytes asynchronously, instantiates the returned
module, and checks the real exported function before its Promise chain completes.
The loop waits through the native event/futex path. A real five-second RunLoop
timer bounds the fixture; it is stopped before context release. There is no
fake Promise scheduler or unbounded host polling loop. The mixed fixture runs
checked Wasm, this asynchronous work and all watchdog cases in one process.

The CPU watchdog uses actual dispatched-thread CPU accounting. Tests require:

- A callback terminating on its first invocation.
- A callback continuing once and terminating on its second invocation.
- Default termination without a callback.
- A callback explicitly installing a new limit before its second invocation.
- A callback clearing the limit, allowing a script to return 42, and leaving
  that context usable for another evaluation.

The first direct and UEFI attempts failed the continuation case: the callback
ran once, returned false, and the infinite script never received its second
deadline. The unchanged GUI remained responsive during that script. A read-only
hardware breakpoint trace established that an already consumed finite CPU
deadline was mistaken for a newly installed callback timer. The branch returned
false without calling `startTimer`.

The CHOS-only upstream patch resets the consumed deadline to `noTimeLimit`
immediately before invoking the callback, after the CPU budget is reached.
An explicit callback reset still installs a finite deadline and remains
detectable; clearing the limit still prevents rearming. The original failing
fixture then passes in both boot modes. Only one of the 535 compiled objects
changes from the previous checkpoint. All 41 patched upstream postimages,
seven native port files and the combined patch remain hash-checked; forward
replay must reproduce the postimages. Original failures and the branch trace
are retained rather than relabeled as passes.

## Launch through the desktop Console

Both the isolated EL0 Console and trusted UEFI Console now accept
`run PATH [ARGS]`. A shared bounded splitter extracts one filesystem path and
passes the remaining argument text to the existing spawn-with-arguments API.
It does not interpret a shell, expand variables, alter process rights or grant
new kernel operations. Empty and oversized paths leave output buffers unchanged.
File Manager launches without arguments retain their existing behavior.

The GUI integration fixture uses physical emulated keyboard events, verifies
the executable text of the Console actually loaded, types
`run /bin/jscvm.elf wasm`, observes the real native child's output in the visible
Console, checks its normal exit, closes the Console and requires exact cold
physical/commit/engine-lease/futex baselines. Both Console frontends are tested.

An initial GUI attempt failed because an old Console on a disposable CHFS disk
overrode the rebuilt ROMFS copy. A breakpoint captured the old Console passing
the whole command as its filename with an empty argument tail; all queue slots
were free. The integration fixture now installs the current Console into its
own disposable disk and verifies loaded bytes. No kernel admission check was
weakened to work around the stale test artifact.

## Reproduce

Prepare the pinned native SDK and engine following
[the engine port instructions](BROWSER_ENGINE_PORT.md). Do not rebuild the SDK,
engine, kernel or embedded applications while a guest matrix runs. Build the
strict native fixture with the recorded toolchain:

```sh
source /workspace/chos-tools/activate
python3 tools/build_native_jsc_suite.py \
  --source /workspace/chos-reference/webkit-2.54.2 \
  --native-sdk build/native-sdk \
  --engine-build /workspace/chos-context-engine/cmake \
  --out /workspace/chos-native-jsc-suite \
  --cmake /workspace/chos-tools/build-python/cmake/data/bin/cmake \
  --ninja /workspace/chos-tools/build-python/bin/ninja
python3 tools/prepare_native_engine.py \
  --application /workspace/chos-native-jsc-suite/cmake/jsc_native_suite
make CC=clang LD=ld.lld -j4 all uefi-iso install-disk-image \
  engine-test-images build/qemu-smoke-disk.img
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
# Host checks can rebuild embedded inputs: synchronize images before guests.
make CC=clang LD=ld.lld -j4 all uefi-iso install-disk-image engine-test-images
python3 tests/qemu_native_jsc_suite.py --suite test262 --mode direct \
  --runs 3 --out /tmp/chos-jsc-test262-direct
python3 tests/qemu_native_jsc_suite.py --suite mixed --mode uefi \
  --runs 3 --out /tmp/chos-jsc-mixed-uefi
python3 tests/qemu_gui_native_arguments.py --mode uefi \
  --out /tmp/chos-jsc-gui-uefi
```

Run each of `test262`, `wasm`, `watchdog`, `async-wasm` and `mixed` in both boot
modes. Every successful lifecycle must have the exact positive marker, process
exit status zero and exact cold physical pages, committed-frame budget and
engine lease. The final futex waiter count must also be zero. During long-script
tests an actual F1 event must reach the desktop while script work is still active.
The final native matrix passes three lifecycles per mode for all five suites:
30 engine lifecycles and 1,032 selected Test262 variant executions. Each returns
to its exact cold baseline. Twelve physical HID observations during watchdog or
mixed work measure 0.002–0.046 seconds on the recorded QEMU configuration; these
are observations, not a general latency guarantee.

The complete checkpoint has 21 passing guest profiles plus the full host and
ASan/UBSan suites. Normal and engine-media input/Ethernet regressions, TLS over
IPv4 and IPv6, durable installation/disk-only boot/restart and an injected
NVMe-write failure all pass. Shutdown displays its status for 5.114 seconds before
actual power-off in the recorded UEFI test.

Results, input hashes, failed attempts and qualification limits are recorded in
[the checkpoint evidence](validation/stage3-native-jsc-suite.json).

## Fusion diagnostic image and remaining work

The separate diagnostic ISO embeds `/BIN/JSCVM.ELF`; it has **no installer
payload**. Normal installation media remain separate and do not embed this
large engine fixture. The engine file exceeds CHFS's existing four-MiB file
limit and cannot be copied there as an installed application. Use a disposable
or cloned VM for the diagnostic image. If an installed disk boots first, select
the diagnostic CD/DVD once in the VM firmware boot menu. Existing valuable storage
need not change.

In the desktop Console, run each command separately and wait for its positive
completion line:

```text
run /bin/jscvm.elf wasm
run /bin/jscvm.elf async-wasm
run /bin/jscvm.elf watchdog
run /bin/jscvm.elf mixed
run /bin/jscvm.elf test262
```

The last command prints 172 PASS lines and `passed=172 failed=0`; it takes about
two minutes per run in the recorded software-emulated setup. Check clock, mouse
and keyboard while work runs, especially watchdog/mixed. Repeat the commands,
check `tasklist`/`sysinfo`, and separately test `netstatus`, example.com, held
Backspace, blinking carets, resolution and power controls. Capture exact failure
text plus `bootlog`, `drivers`, `pci net` and `netstatus` if behavior differs.
QEMU execution does not establish independent Fusion acceptance for this new
engine. Earlier user-confirmed Fusion Ethernet remains distinct evidence.

Full modern HTML/CSS/layout and WebCore integration, browser tabs/navigation and
storage, site permission policy and complete untrusted-content isolation,
production TLS trust/time/entropy/update policy, native Pi5 SDIO/firmware/radio
authentication, and new Fusion/Pi5/laptop qualification remain mandatory.
Neither this selected suite nor a diagnostic binary establishes those gates.
Version 0.50.0 remains reserved for the fully verified revised Stage 3.
