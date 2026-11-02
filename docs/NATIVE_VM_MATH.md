# Native VM multiply-overflow and binary128 square root/hypotenuse

The native runtime now links the original LLVM19.1.7 signed128 `__muloti4`
implementation and its original `int_mulo_impl.inc`, plus original musl1.2.5
`sqrtl`, `hypotl` and `__math_invalidl`. Existing original square-root tables are
reused. The source verifiers check64 compiler-rt and288 musl files; new musl
imports also match their pinned upstream Git objects. No replacement math stub
or bootstrap ICU substitution is introduced.

The independent host test renames only the original DUT symbols and compares
wrapped signed128 results and overflow flags to the host compiler/libgcc oracle.
It covers600,100 random/boundary multiply cases alongside the existing integer
runtime checks. Full host and configured ASan/UBSan suites pass.

Native expected results come from exact Python integers and integer-square-root
rounding over rational inputs. That square-root oracle first cross-checks10,000
independent binary64 host sqrt cases. Each native vector set contains656 signed
multiply,137 exact binary128 sqrt and245 hypot cases. Hypot has a predefined
one-ULP RNE bound; exact Pythagorean/scaling, zero and infinity cases require exact
bits. Tests include subnormals, extreme scaling, overflow, NaNs and signed zero.
Original libc++ long-double sqrt/hypot overloads use these real routines.

Final direct and isolated UEFI profiles pass. Each executes30 complete vector
sets across three parent lifecycles and27 secondary workers, with zero allocator
or futex waiters and exact cold physical-page recovery. This is19,680 multiply,
4,110 sqrt and7,350 hypot vector cases per guest mode, plus classification/zero
checks. The profile explicitly selects round-to-nearest. Full directed rounding,
exception/errno conformance and all long-double math remain outside this evidence.

All12 additional final UEFI regressions pass: existing binary128, detach, WTF
workers, JSC options, C++20/23, synchronization, ICU, IPv4/IPv6 TLS1.3, typing and
e1000e desktop/platform. The two math profiles bring the total to14 passing
guest profiles. Full host/sanitizer suites also pass. See the
[exact qualification](validation/stage3-native-vm-math.json) for frozen complete
SDK/kernel/media/app/source inputs, original provenance and retained failures.

The real full-ICU VM link now resolves both previous math symbols. It still fails
on actual foreign `Thread::suspend`, `Thread::resume` and `Thread::getRegisters`,
and the unchanged linker rejects its proposed50,663,424-byte image. None of that
VM output is executed, admitted or published. The kernel/address-space, loader,
CHFS, memory/stack/scheduler budgets and GC coordination still need a coherent
engine resource profile and native qualification. See
[NATIVE_JSC_FRAMEWORK_CLOSURE.md](NATIVE_JSC_FRAMEWORK_CLOSURE.md).

```sh
python3 tools/generate_vm_math_vectors.py --check
make test-integer-runtime CC=clang LD=ld.lld
python3 tools/build_native_vm_math.py --native-sdk build/native-sdk --out /tmp/chos-vm-math
python3 tests/qemu_native_vm_math.py --application /tmp/chos-vm-math/cmake/native_vm_math \
  --out /tmp/chos-vm-math-direct
python3 tests/qemu_native_vm_math.py --mode uefi \
  --application /tmp/chos-vm-math/cmake/native_vm_math --out /tmp/chos-vm-math-uefi
```

Use fresh output directories and synchronized native kernels/media. Component
math execution does not establish a running JavaScript VM, JavaScript/WebAssembly,
Intl in the full VM or modern browser. Native Pi5 Wi-Fi, production security and
final Fusion/Pi5/laptop acceptance remain open. Development stays0.30.0; Stage3
and version0.50.0 remain incomplete.

The [native foreign-pause/WTF coordination component](NATIVE_FOREIGN_GC.md) now
supersedes earlier missing-foreign-method diagnoses: the full VM link has no
undefined symbols. Its image remains unadmitted; full VM/GC/browser execution and
the remaining revised Stage3 gates are still open.
