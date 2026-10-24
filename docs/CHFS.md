# CHOS CHFS v3 — persistent filesystem

CHFS is an experimental filesystem over a raw 512-byte-sector block device. The current platform driver targets VirtIO-MMIO block storage on QEMU `virt`. There is no partition table: CHFS occupies the whole image. Current kernels read legacy v1 and v2 volumes and attempt a guarded migration toward v3.

## On-disk layout

The 512-byte superblock at LBA 0 contains `CHFS`, format version, sector size, declared sector count, catalogue-copy LBAs, catalogue-copy size and data-start LBA. Its CRC32 is stored at byte offset 508 and computed with that field zeroed. Image geometry is checked before use.

| Region | CHFS v1 | CHFS v2 | CHFS v3 |
|---|---:|---:|---:|
| Superblock | LBA 0 | LBA 0 | LBA 0 |
| Catalogue A | LBAs 1–3 | Last 16 sectors, first 8-sector copy | LBAs 1–64 |
| Catalogue B | LBAs 4–6 | Last 8 sectors, second 8-sector copy | LBAs 65–128 |
| File data starts | LBA 7 | LBA 7 | LBA 129 |
| Persistent records total | 23 | 63 | 511 |

Each catalogue has a `CHDR` header, wrapping 32-bit generation and CRC32. Fixed 64-byte records follow the 16-byte header. Each record stores a 48-byte NUL-terminated absolute path, first sector, sector count, file size and CRC32. V3 reserves 128 sectors for the redundant catalogues at the beginning of the volume. V2 instead reserves its last 16 sectors; v1 stores metadata at LBAs 1–6.

A directory record uses the `CDIR` tag in the CRC field and owns no data extent. A zero-length file also owns no extent. Paths are case-insensitive absolute ASCII paths; supported component characters are alphanumeric, underscore, dash and dot. The 48-byte path field permits at most 47 visible characters including separators. Explicit and implicit parent directories are supported; `/` is implicit.

## Capacity and file allocation

CHFS v3 supports 511 combined persistent records across the entire volume, not per directory. Regular files use one contiguous extent and are limited to 4 MiB; the EL0 file-write syscall caps one request at 64 KiB. Filesystems can use larger images, but contiguous allocation means that free-space totals do not guarantee a sufficiently large uninterrupted extent. The format still uses a fixed catalogue and is not a general-purpose filesystem.

## Migration and compatibility

Mount validates existing directory generations before attempting migration. A v1 volume first attempts the v1→v2 transition; a successfully migrated or natively v2 volume can then attempt v2→v3. In v2→v3 migration, file extents below LBA 129 are copied to safe space after the new data start, old valid v2 catalogue generations are kept until the relocated paths are committed, both v3 catalogues are written and flushed, and the v3 superblock is written last. If safe contiguous space or I/O success is unavailable, the old volume remains mounted in the previous format when possible.

This is still an experimental in-place migration. Back up disk images before first booting a v3-capable kernel. Older kernels do not understand v3 and must not be used on migrated volumes. Migration fallback and data preservation are covered by host tests, but live QEMU acceptance and broader fault injection remain necessary.

## Commit and recovery model

Updates write the alternate catalogue and flush it before switching the active generation. Mount selects the newest valid catalogue copy. Before allocating an extent, CHFS reserves extents referenced by both the active and valid backup generations to reduce the risk of destroying data reachable through the previous generation.

Alternating checksummed catalogues are an integrity measure, not a full journal. They cannot guarantee recovery from arbitrary device failure, simultaneous catalogue corruption, faulty flushes, every power-loss timing, or contiguous-space exhaustion. CRC32 detects accidental corruption and is not cryptographic integrity protection.

## Host image tool

`tools/mkchfs.py` supports `create`, `list`, `put`, `read`, `mkdir`, `rm` and `rmdir`. It creates v3 images by default and can inspect or modify legacy v1/v2 images while retaining their old layouts. `make disk-image` is destructive; `make disk-image-ready` creates an image only if one is missing. Normal `make clean` does not delete the disk image.
