# Native WTF file handles

The selected real WebKit source now uses `FileHandleCHOS.cpp` instead of its POSIX
file-handle backend. The original common constructor, moves, destructor and class
interface remain compiled. Native identifiers are the actual tagged CHFS/ROMFS
identities, rather than a fabricated Unix inode type. The maintained patch and
manifest pin all changed upstream bytes and the native adapter.

Reads and writes join native transfers of at most256 bytes until the requested
span is consumed, EOF is reached or an actual error occurs. This matters because
upstream append logic interprets a short read as EOF. A later error returns the
completed prefix and sets the real error; the adapter never reports unwritten
bytes as success. Multi-transfer operations on a shared handle require caller
serialization. Seek, truncate, size, identity and conditional flush use the actual
owned native services. Writable data remains private until successful flush or
close. Explicit flush reports stale-source conflicts. Since the upstream destructor
cannot return an error, failed close attempts a diagnostic and explicit abort of
the retained handle; uncommitted data is discarded, never reported as committed.
Persistence callers must require a successful explicit flush before reporting
completion. This is not yet integration of generic engine cache/download helpers.

Private mappings allocate actual native anonymous pages, fill them using positional
reads without changing the descriptor cursor, and apply actual read protection
for read-only views. Private modifications never write back to the file. Mapped
copies remain subject to the native1MiB mapping/2MiB guarded aperture limits;
allocation failures preserve the descriptor and clean up owned pages. Shared
coherent file mappings and write-only mapping protection are unavailable and fail
explicitly. Empty private files produce a valid empty mapping. Unsupported lock
requests consume/abort the adopted handle and fail with ENOTSUP; no lock is
pretended. Proper shared mappings, engine locking and the higher filesystem layer
remain open implementation work.

The component executable links the real common FileHandle source, its real mapped
span lifecycle and the native adapter against the checked SDK/configuration. It
checks32KiB-plus I/O, held snapshots/IDs across replacement, truncation, durable
flush, stale close, move ownership, unsupported locks, actual read-protected copy
refusal, private mapping/cursor behavior, partial writes and protected-buffer reads,
guard-aware exhaustion/recovery and sixty concurrent per-thread file lifecycles.
Three process lifecycles per boot mode must return to the cold physical-page
baseline with no user heap allocations or futex waiters. The unused String/Vector
`readAll` and `appendFileContents` paths are compiled but discarded by function
section collection; they are not executed or claimed qualified by this fixture.

The first guest test incorrectly expected EPERM when writing a read-only handle;
the actual native contract returns EBADF. A later exhaustion fixture initially
forgot the four guard pages around two mappings. Both failed tests and their
exact source/executables are retained, along with the corrected results. During
an initial regression matrix, host checks rebuilt embedded applications, and the
artifact guard rejected six ISO/symbol mismatches before guest startup. Those
are retained failed infrastructure checks. Images were resynchronized and the
same guest profiles rerun with unchanged deadlines.

The full WTF compilation progresses from five failed units to three:
`FileSystem.cpp`, `posix/FileSystemPOSIX.cpp` and `unix/UniStdExtrasUnix.cpp`.
A complete library archive/link, engine Thread/GC and memory profile, JavaScript,
modern rendering, native Pi5 Wi-Fi and production/platform acceptance remain open.
These are software gaps, not solely unavailable hardware. No new Fusion/Pi/laptop
qualification is inferred from QEMU. Stage3 is incomplete; version remains0.30.0.
Exact qualification is recorded in
[the validation manifest](validation/stage3-wtf-file-handles.json).
