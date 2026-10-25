# Building a CHOS C application (0.30.0)

The normal Makefile pipeline compiles `userland/examples/cdemo.c` for freestanding
AArch64, links it into page-separated code, read-only data, writable data and BSS,
extracts the flat payload, and wraps it as a CHCX v2 executable. The generated image
is embedded as `/BIN/CDEMO.CHX` in the kernel's read-only ROMFS.

```sh
make userland-image
python3 tools/mkchcx.py --verify build/userland/cdemo.chcx
```

The C startup routine enters `main(argc, argv)`. Use `spawn /BIN/CDEMO.CHX
argument-ok` followed by `runqueue` or start the `runtime` shell and use `spawn`.
The C header is `userland/libchos/include/chos/user.h`; it exposes syscall wrappers
and basic memory/string helpers. A separate bounded native allocation/string
library now lives in `userland/libc`; see
[NATIVE_C_RUNTIME.md](../../docs/NATIVE_C_RUNTIME.md) for actual EL0 evidence and
remaining runtime limits. This is not a complete libc or C++ standard library.

For a new application, use `userland/user.ld` as a starting point and retain its
page-separated sections and exported `__chos_*` segment symbols. Link for a flat,
position-independent layout at VMA zero. Package v2 from linker symbols using:

```sh
nm --defined-only --numeric-sort app.elf > app.sym
llvm-objcopy -O binary app.elf app.bin
python3 tools/mkchcx.py --v2-symbols app.sym app.bin app.chcx
python3 tools/mkchcx.py --verify app.chcx
```

Current CHCX limits include a 64 KiB maximum virtual image, at most eight stack pages,
up to eight arguments, whitespace-only shell tokenization, no environment variables,
no relocations at load time, and no dynamically linked applications. The kernel loader
keeps code RX, read-only data R/XN, and writable data/BSS RW/XN.

Revised Stage 3 now has bounded anonymous private RW/XN page mappings through
`chos_map_memory`/`chos_unmap_memory`. See [USER_MEMORY.md](../../docs/USER_MEMORY.md)
for limits, ownership, guard/execute protections and real EL0 tests. These primitives
do not yet provide POSIX mmap or a complete libc/C++ runtime. The allocation
library builds on these mappings; its kernel quotas and single-thread limit
remain explicit.
