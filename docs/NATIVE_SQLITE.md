# Native SQLite memory core

SQLite 3.54.1 now builds as a real AArch64 CHOS static library, and the selected
in-memory SQL, pthread synchronization and cleanup tests pass in direct and
ACPI-only UEFI QEMU guests. This supplies a dependency prerequisite for roadmap
step 2. WebCore integration, persistent browser storage and revised Stage 3
completion remain open.

## Source and platform contract

[The dependency manifest](dependencies/native-sqlite.json) inventories the four
original amalgamation files. The official HTTPS archive's SHA3-256 matched the
official download page:

`92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`

No signature authentication is claimed. Original public-domain notices remain.
The builder copies those originals into its output directory and applies the
[explicit mutex patch](../ports/sqlite/native-mutex.patch) with zero fuzz; it does
not edit the vendored files. Both qualified build copies were checked byte for
byte against a fresh application of that patch.

The `SQLITE_OS_OTHER` profile uses actual CHOS pthread fast, recursive and static
mutexes as SQLite's default backend. It retains `SQLITE_THREADSAFE=1`, a real
atomic memory barrier, the upstream SQL parser/query engine and allocator, column
metadata and JSON support. It does not select a no-op mutex backend. The native
SDK includes the real `strspn`/`strcspn` runtime functions required by SQLite;
`HAVE_LOCALTIME_R` selects the existing reentrant UTC time conversion.

The platform VFS explicitly supports SQLite's in-memory database and temporary
storage paths. Persistent file open, access and delete operations return SQLite
errors. Disk VFS locking, shared-memory/WAL support, crash durability and browser
cookies/history/cache persistence are unimplemented. Loadable extensions are
disabled at compile time.

Initialization requires the actual OS entropy service and returns `SQLITE_IOERR`
when it is absent. A later entropy failure terminates the application because
SQLite's randomness callback cannot return an error; no fallback seed is
invented. Time callbacks use the guest wall clock, and native sleeps round up to
100 Hz scheduler ticks. Trusted production time and browser timezone policy
remain separate work. Input limits are 8 MiB values, 1 MiB SQL text, 512 columns
and 1,024 bound variables.

## Verified artifact and checks

The qualified SDK6 manifest SHA-256 is
`d58d59cd7f671120f207161c88e65fe884b4dd0a419109294a95176de091f210`.
The target prefix is
`/workspace/chos-evidence/native-sqlite-20261112/target1/prefix`; it installs the
actual `libsqlite3.a`, public headers and `sqlite3.pc` for dependency discovery.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Native `libsqlite3.a` | 2,178,426 | `68c2eb67ee2b57a7c2d50e7d2494350c28b9df74aee23437843d58de4fcdd7bb` |
| Native test ELF | 1,509,096 | `1510d0da5531f03e2a07720afa76dabda256ab2f2a9d8cdd939b83dc83da4337` |

The current build inputs, source manifest, installed files, built source copies
and SDK inventory match the qualified artifacts. The ELF is AArch64 with no
undefined symbols. The build-only report deliberately says
`LINKED_NOT_EXECUTED`; execution evidence is recorded separately in
[the validation summary](validation/stage3-native-sqlite.json) and
[the detailed qualification report](../tests/native_sqlite/qualification.json).

The final direct and UEFI profiles each ran three application lifetimes. Each
lifetime exercised real SQL create/insert/query, commit and rollback, 200 bound
rows, UTF-8 text, blobs, floating-point values, origin metadata, JSON, date
functions, invalid SQL and constraint errors. Three workers each ran an
independent database and 300 updates on one serialized shared connection; the
counter reached exactly 900. Recursive mutex contention returned `SQLITE_BUSY`
to a competing thread. A real hard heap quota caused `SQLITE_NOMEM`, and the
query succeeded after restoring the quota. File open/attach and extension
loading were refused.

All six ordinary executions shut down with zero SQLite/native heap allocations
and restored the exact cold physical-page, engine-owner, committed-frame and
futex baselines. A separate direct guest without RNG ran three lifetimes and
verified initialization `SQLITE_IOERR` plus the same cleanup. The missing-entropy
profile does not run the ordinary SQL/concurrency tests. The host reference also
passes AddressSanitizer, UndefinedBehaviorSanitizer and leak detection.

Earlier SDK5 compile gaps, the stale-source build guard, an incorrect allocation
quota fixture and its resulting failure-path leak, and final sandbox restrictions
are retained with diagnoses in the reports. The final entropy report was fixed
to list only the checks its branch executes. Guest-constructor errors now also
produce a failed result report. No Fusion, Pi 5 or laptop execution, upstream
full conformance suite, production security or browser storage claim follows
from these selected QEMU and host checks.

## Reproduction

Use fresh output directories so previous evidence is preserved. On the prepared
cloud machine, enable the toolchain and CMake/Ninja environment first:

```sh
source /workspace/chos-tools/activate
export PATH=/workspace/chos-tools/build-python/bin:$PATH
python3 tools/verify_native_sqlite_sources.py
python3 tools/build_native_sqlite.py --host --sanitize --out build/host-sqlite
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/host-sqlite/application/native_sqlite
python3 tools/build_native_sqlite.py --native-sdk /path/to/sdk --out build/native-sqlite
python3 tests/native_sqlite/qemu.py --application build/native-sqlite/application/native_sqlite --image-root /path/to/frozen-images --mode direct --out build/sqlite-direct
python3 tests/native_sqlite/qemu.py --application build/native-sqlite/application/native_sqlite --image-root /path/to/frozen-images --mode uefi --out build/sqlite-uefi
python3 tests/native_sqlite/qemu.py --application build/native-sqlite/application/native_sqlite --image-root /path/to/frozen-images --mode direct --missing-entropy --out build/sqlite-no-entropy
```

The image directory supplies `chos.elf`, `uefi-kernel.elf` and `chos.iso` from one
verified kernel/image set. The runner snapshots them and creates only disposable
disk and firmware copies. QMP sockets and LeakSanitizer thread scanning require
an execution environment that permits those operations; the final reruns used
the same fixtures outside the restrictive sandbox.
