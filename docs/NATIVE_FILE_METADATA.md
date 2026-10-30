# Native file metadata and held identities

The native file service exposes a 64-byte version1 metadata record through
`chos_file_metadata` (syscall75) and `chos_path_metadata` (76). Both share READ9;
path queries also require OPEN8 and a registered process. Positive capability
bits1..62 and old file operations remain compatible. Every output is validated
before copying. Errors leave the caller's output unchanged; descriptor queries
reject full-width values that would alias a valid descriptor after truncation.

The record reports actual type, length when available, view rights, working-copy
and dirty flags, source kind and an optional 24-byte identity. CHFS identities
come from its locked runtime mount/object ledger. ROMFS file identities use the
actual compiled namespace ordinal, with a separate source tag. Implicit physical
and ROMFS directories report their type but explicitly lack an identity and file
size. No inode, timestamp, UID, device number or kernel pointer is fabricated.
These read-only identifiers convey no authority and are not persistent across
boots. Mount generations distinguish runtime remounts; delete/recreate changes
object identity, while rename preserves identity and changes source revision.

Descriptor metadata describes the held view. Read-only opens capture bytes and
optional identity/revision under the same filesystem lock, including the actual
CRC-checked read. Later rewrite, rename or unlink cannot relabel that snapshot.
If the runtime identity ledger is unavailable, ordinary read-only data remains
readable and the identity-valid flag stays clear. Writable handles report their
private length/dirty state and the last committed source revision; a successful
conditional flush advances that revision. An unflushed edit does not invent a
new disk revision. Path metadata instead describes the namespace at lookup time;
it does not claim to CRC-read file contents or replace the conditional commit
lease. Path writability reflects current filesystem authority; descriptor
read-only flags describe that handle's rights.

Qualification uses the actual host CHFS/storage implementation and native EL0
applications. Three process lifecycles in each boot mode verify identities,
rename/unlink/recreate, unchanged protected outputs, capability/ownership refusal
and cold physical-page equality. Three hundred opened read snapshots are checked
against fifty concurrent committed writes and an independent value/revision map.
The final fourteen-profile matrix also passes positional/buffered reads, writable
handles, isolated ELF, ICU, IPv4/IPv6 TLS, typing, both platform boot modes and
installation, disk-only reboot, reset and power-off. Full host and sanitizer
suites pass. The installer completed in73.350s and actual shutdown in5.121s in
this shared-cloud run; these are observations, not performance guarantees.

See [machine-readable evidence](validation/stage3-native-file-metadata.json).
This source milestone supplies engine filesystem prerequisites; complete WTF/JSC
linking, JavaScript, modern rendering, engine-sized memory/GC, production security
policy and native Pi5 Wi-Fi remain open. New Fusion/Pi/laptop qualification is
NOT RUN. Version remains0.30.0; no completion build is published.
