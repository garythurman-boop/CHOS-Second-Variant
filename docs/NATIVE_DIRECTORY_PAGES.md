# Complete native directory pages

`chos_directory_page` (syscall77) reads the merged CHFS/ROMFS namespace through a
640-byte version1 record. It shares the existing LIST_DIR20 permission and requires
a registered process. Positive capability bits1..62 remain unchanged. The old
LIST_FILES/LIST_DIR ABI remains compatible, including its global64-entry bound;
this new interface does not use that truncated catalogue.

Initialize version, exact size and START, with other fields zero. Each successful
call replaces the entire record with at most eight sorted direct children, the
total count, the next index, a runtime consistency token and an END flag. Pass the
returned record back unchanged to continue. The total can cover all511 physical
records plus compiled ROMFS sources. Directories sort before files, then names
sort case-insensitively. Records contain full paths. Explicit persistent records
override embedded defaults and implied directory entries. Empty physical and
implicit/ROMFS directories are recognized; an actual file queried as a directory
is refused. No invented inode, timestamp or filesystem device ID is needed.

The service copies the complete actual CHFS catalogue and its ledger counters
under the storage lock. It then merges the immutable ROMFS namespace and builds
an owned temporary view. The temporary work area is about69KiB on the kernel heap,
not the small task stack. It is freed on every success/error path. No disk I/O or
persistent cursor is involved. Pages contain zeroed unused records and reserved
fields. Errors, stale tokens, malformed requests and protected/cross-page outputs
leave the caller's record unchanged. User output validation/copy uses the existing
address-space lock; no partially copied page is accepted.

The token records the real mount/revision counters and mounted/write-authority
state. A mutation, remount or authority change between pages returns ESTALE;
callers must discard partial results and restart. Even unrelated file changes
conservatively invalidate a scan. Tokens are observations, not capabilities,
process-owned handles or persistent boot identifiers. They can be reused for
another directory under the same namespace observation; callers must retain their
own path association. This is restartable enumeration, not a guarantee of progress
under continuous writes. Invalid/unavailable ledger state fails explicitly.

Host qualification feeds511-entry catalogues in30 seeded permutations with an
independent expected ordering, duplicates and source-priority cases. The actual
CHFS/storage/paging fixture additionally fills all511 physical records, checks
a512-child merged root and a201-child root, full-page
continuation, ROMFS/persistent overrides, allocation refusal, zero disk callbacks,
remount invalidation and write-uncertainty authority changes. It models only the
scratch allocator and two fixed ROMFS source inputs; filesystem/storage/paging code
is the real implementation. Native EL0 checks create175 unrelated files before a
different175-file directory and ten subdirectories, then validate all185 children,
empty directories, real ROMFS merge, permission refusal, changed tokens, invalid
indices and protected buffers. Three concurrent enumeration workers and three
process lifecycles must restore cold physical pages with zero heap/futex records.

The first native probe passes but exposed repeated canonical-path validation in
the duplicate-comparison loop. Already-validated names now use a direct comparison;
the seeded namespace tests retain the same semantics. Maximum measured page-call
times include preemption/descheduling and are observations, not a blocking-time
bound. The final fifteen guest profiles and full host/sanitizer suites pass, including
the additional full-capacity normal/sanitizer fixture. Ethernet/TLS4/TLS6, typing,
installation, disk-only reboot, persistence, reset, shutdown and injected-write
refusal retain regression coverage. Direct/UEFI maximum observed page round trips
were43.941ms/37.901ms in this shared-cloud sample. These include scheduling delays.
Exact final results and retained build/fixture investigations are in
[the validation record](validation/stage3-native-directory-pages.json).

This is a filesystem prerequisite for the real engine. The higher-level native
WTF filesystem, complete library/link, engine memory/Thread/GC, JavaScript and
modern rendering remain open, alongside native Pi5 wireless and production/
platform gates. CHFS path/size limits and whole-file I/O locking still apply.
New Fusion/Pi/laptop qualification is NOT RUN. Version remains0.30.0.
