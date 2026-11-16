# Native SQLite memory profile

The [native SQLite qualification](../../docs/NATIVE_SQLITE.md) records the exact
native archive/application hashes, source audit, final direct/UEFI/missing-entropy
results and retained failures. The selected memory core passes; persistent VFS
and browser storage remain unimplemented.

This builds SQLite 3.54.1's original amalgamation with an explicit CHOS
`SQLITE_OS_OTHER` backend. Source SHA3-256 matched the official HTTPS download
page; every original file is inventoried. Original public-domain notices remain
in the source. No signature authentication is claimed where none was supplied.

The normal OS_OTHER mutex default would be a no-op implementation. The small,
explicit patch selects CHOS's actual pthread backend as SQLite's default instead.
Fast and recursive mutexes call real pthread operations; static mutexes use real
static initializers. Dynamic mutexes own their allocations. Owner/depth checks
and a real atomic memory barrier support the selected synchronization contract.
The stock SQL parser, pager, query engine and memory allocation code are used.

The VFS is deliberately memory-only. SQLite's actual in-memory database and
memory temporary-store paths work; persistent open/access/delete operations
return explicit SQLite errors. No disk file, advisory lock, shared memory or
successful write is fabricated. This does not implement CHFS SQLite persistence,
WAL filesystem support, browser cookies/history/cache or a complete browser VFS.
Loadable extensions are disabled. Real SQL transactions and rollback operate on
in-memory databases; they do not establish crash-durable filesystem transactions.

Entropy comes from the real OS entropy service. Initialization refuses missing
entropy. A later entropy-service failure aborts the application rather than
allowing SQLite's VFS interface to fall back to invented seed bytes. Host fixtures
use Linux getrandom. Time callbacks use the actual guest wall clock; the native
localtime_r path follows CHOS's existing UTC policy. Trusted production time and
browser timezone integration remain separate work. Native sleep rounds upward
to the existing 100 Hz scheduler ticks.

The profile retains SQLite's thread-safe API, column metadata and JSON support,
with explicit input/column/variable limits. It is selected core qualification,
not the complete upstream conformance suite or production SQL/browser security.

## Build and test

Use the prepared Clang/LLD/CMake/Ninja environment and a fresh output directory.
The native SDK must include real strspn/strcspn support (SDK6 or a newer export).

```sh
source /workspace/chos-tools/activate
export PATH=/workspace/chos-tools/build-python/bin:$PATH
python3 tools/verify_native_sqlite_sources.py
python3 tools/build_native_sqlite.py --host --sanitize --out build/host-sqlite
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/host-sqlite/application/native_sqlite
python3 tools/build_native_sqlite.py --native-sdk /path/to/native-sdk --out build/native-sqlite
python3 tests/native_sqlite/qemu.py --application build/native-sqlite/application/native_sqlite --image-root /path/to/frozen-images --mode direct --out build/sqlite-direct
python3 tests/native_sqlite/qemu.py --application build/native-sqlite/application/native_sqlite --image-root /path/to/frozen-images --mode uefi --out build/sqlite-uefi
python3 tests/native_sqlite/qemu.py --application build/native-sqlite/application/native_sqlite --image-root /path/to/frozen-images --mode direct --missing-entropy --out build/sqlite-no-entropy
```

The installed prefix provides the actual static `libsqlite3.a`, SQLite headers and
`sqlite3.pc`. CMake's normal FindSQLite3 can discover that target prefix. It does
not install a pretend full operating-system backend or browser integration.
