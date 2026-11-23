# Native bitset and stack headers

The native libc++ closure now includes the original LLVM 19.1.7 `bitset` and
`stack` headers, plus their two missing private declarations. These are standard
library dependencies of the actual WebCore build. All four files are unchanged
members of the existing pinned LLVM release archive; exact sizes, SHA256 values,
Git blobs and the native preprocessing probe are recorded in the
[import receipt](dependencies/libcxx-containers.json). No container implementation
or native runtime code was substituted.

The fixture tests actual public container operations. An independent byte model
checks bitsets of sizes 0, 1, 63, 64, 65, 129 and 257, covering shifts across word
boundaries, bitwise operations, proxy references, custom string alphabets and
integer conversions that fit their destination. Deque- and vector-backed stacks
exercise LIFO order, growth, emplacement, copies, moves, swaps and comparisons.
Object construction and destruction counters agree, with no live objects left.
The main thread and three real pthread workers complete 25,296 matched object
constructions and destructions per process.

Direct and isolated UEFI QEMU each pass three complete process lifetimes. Each
process returns to zero native heap allocations and mapped bytes before return;
process reaping restores exact cold physical pages, frame commitments, engine
ownership and futex state. Host address, undefined-behavior and leak sanitizer
checks exercise the same fixture with the original headers. Host string members
use the existing real string instantiations, and diagnostic termination uses the
original LLVM source extracted from the same verified archive. This selected
host linkage is not a complete host libc++ port.

The [validation report](validation/stage3-native-stdlib-containers.json) records
exact SDK, source, executable and guest-result hashes. The reproducible
[build driver](../tools/build_native_stdlib_containers.py) verifies the entire
immutable SDK and curated original libc++ inventory before and after building.
It refuses existing output directories and records actual commands and failures.

```sh
source /workspace/chos-tools/activate
export PATH=/workspace/chos-tools/build-python/cmake/data/bin:/workspace/chos-tools/build-python/bin:$PATH
python3 tools/build_native_stdlib_containers.py --sdk /path/to/verified/sdk --out /path/to/fresh/native-build
python3 tests/native_stdlib_containers/qemu.py --application /path/to/fresh/native-build/application/native_stdlib_containers --image-root /path/to/frozen/images --out /path/to/fresh/direct-results
python3 tools/build_native_stdlib_containers.py --host --sanitize --sdk /path/to/verified/sdk --llvm-archive /path/to/libcxx-19.1.7.src.tar.xz --out /path/to/fresh/host-build
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /path/to/fresh/host-build/native_stdlib_containers
```

Repeat the guest command with `--mode uefi` and a new output directory. The
frozen directory must contain `chos.elf`, `uefi-kernel.elf` and `chos.iso` from a
consistent build.

This is selected positive container qualification. Native exceptions, exhaustive
standard-library conformance, fuzzing, WebCore page rendering and modern-browser
acceptance remain separate work. New physical Fusion, Pi 5 and laptop execution
has not run. Stage 3 remains incomplete at version 0.30.0.
