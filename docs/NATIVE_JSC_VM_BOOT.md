# Separate native VM boot fixture

CHOS now packages the real original-ICU JavaScriptCore executable in a separate
diagnostic kernel and boots that larger kernel through AAVMF. This closes a
packaging dependency. It does **not** establish a working VM: the first direct
and UEFI attempts enter the executable, then abort with status 134 during
`JSGlobalContextCreate`, before JavaScript, Intl or collection runs. The positive
VM test correctly reports failure. These failures remain retained.

The diagnostic `/BIN/JSCVM.ELF` is immutable ROMFS data copied by the real native
loader into private pages. Normal kernels link an empty object and retain their
original eleven embedded files. Explicit diagnostic link targets replace that
object; there is no global mode flag to leave stale mixed objects. The image
does not launch automatically.

`prepare_native_engine.py` applies the actual kernel executable policy before
and after stripping symbols. Every PT_LOAD and PT_NOTE payload, address,
permission, alignment and memory size must remain identical. A managed manifest
records both hashes. Restaging identical bytes retains the binary timestamp;
unowned or unexpectedly modified output refuses replacement. The guest test
independently compares the staged executable with the bytes actually embedded
in its snapshotted kernel and checks the ISO against that exact kernel.

The UEFI loader accepts a kernel file of at most 64 MiB, retaining its existing
256 MiB relocated-image limit. Auxiliary preferences and installer payloads
keep their separate 16 MiB file cap. The ISO builder accepts explicit 32/64/128
MiB FAT16 ESP capacities; the normal default remains 32 MiB. The diagnostic ISO
uses 64 MiB and omits the installer payload, which would exceed its existing
bound. It is not an installable distribution or a published user-test milestone.
Ordinary CHFS file limits are unchanged.

The first diagnostic UEFI kernel is 57,770,320 bytes with a 107,966,464-byte
relocated span. Its 339 relative relocations pass all four host placements.
Actual direct and UEFI boot reach the scheduled runtime and executable entry.
The ordinary image separately retains the working installer and boot path.

The failure has a concrete diagnosis. A debugger hardware breakpoint at native
`abort` captured the original chain through `StructureMemoryManager`, JSC
initialization and `JSGlobalContextCreate`. The failed assertion is line 117 of
the pinned `heap/StructureAlignedMemoryAllocator.cpp`: no aligned Structure heap
could be reserved. The upstream default starts at 4 GiB and tries eight sizes;
even the last attempted 32 MiB reservation asks the POSIX adapter for 64 MiB
including alignment slack. CHOS's native `mmap` caps a mapping at 1 MiB, so all
attempts refuse. The captured 16 MiB value is the value after the final halving,
not another attempt. This is a memory-service dependency, not a diagnosed Fusion
network or firmware fault.

The next dependency is bounded aligned virtual reservation with explicit commit
and decommit. Reserving an address range should not eagerly allocate all its
physical pages. Physical work must stay in short bounded operations, preserving
frame budgets, guards, execute-never data, rollback and cancellation safety. The
native OSAllocator must perform actual commit/decommit rather than assuming
demand paging. Lowering assertions or merely enlarging an IRQ-masked allocation
loop would not close this requirement.

To reproduce with an already qualified SDK and pinned native engine build:

```sh
python3 tools/probe_jsc_vm_link.py --source PATH_TO_PINNED_WEBKIT \
  --native-sdk build/native-sdk --engine-build PATH_TO_NATIVE_ENGINE_BUILD \
  --application-profile engine --out /tmp/chos-vm-link
python3 tools/prepare_native_engine.py --application /tmp/chos-vm-link/cmake/jsc_vm_probe
make CC=clang LD=ld.lld -j4 engine-test-images
python3 tests/qemu_jsc_vm.py --mode uefi --out /tmp/chos-vm-guest
```

The final command currently fails at the dependency above; a successful link,
image build or boot is not VM execution qualification. Modern browser rendering,
native wireless, production security and new Fusion/physical qualification remain
open. Version stays 0.30.0; 0.50.0 requires all revised Stage 3 gates.
