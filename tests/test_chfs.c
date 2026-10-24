#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/blockdev.h"
#include "chos/chfs.h"

#define SECTORS 512u
static uint8_t disk[SECTORS * CHOS_BLOCK_SECTOR_SIZE];
static int reads, writes, fail_write_at = -1;
static int fake_read(chos_block_device_t *d, uint64_t lba, uint32_t count, void *out) {
    (void)d; ++reads;
    if (lba >= SECTORS || count > SECTORS - lba) return 0;
    memcpy(out, disk + lba * CHOS_BLOCK_SECTOR_SIZE, count * CHOS_BLOCK_SECTOR_SIZE); return 1;
}
static int fake_write(chos_block_device_t *d, uint64_t lba, uint32_t count, const void *in) {
    (void)d; ++writes;
    if (fail_write_at > 0 && writes == fail_write_at) return 0;
    if (lba >= SECTORS || count > SECTORS - lba) return 0;
    memcpy(disk + lba * CHOS_BLOCK_SECTOR_SIZE, in, count * CHOS_BLOCK_SECTOR_SIZE); return 1;
}
static int fake_flush(chos_block_device_t *d) { (void)d; return 1; }
static chos_block_device_t device = {"host-test", SECTORS, NULL, fake_read, fake_write, fake_flush, 1};

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static void put_le64(uint8_t *p, uint64_t value) {
    put_le32(p, (uint32_t)value); put_le32(p + 4u, (uint32_t)(value >> 32));
}
static void build_v1_image_with_tail_file(const char *path, const void *payload, size_t size) {
    uint8_t super[CHOS_BLOCK_SECTOR_SIZE];
    uint8_t directory[CHOS_CHFS_V1_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE];
    uint8_t backup[CHOS_CHFS_V1_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE];
    uint8_t data[CHOS_BLOCK_SECTOR_SIZE];
    size_t length = strlen(path);
    assert(length < CHOS_CHFS_PATH_MAX && size <= CHOS_BLOCK_SECTOR_SIZE);
    memset(disk, 0, sizeof(disk));
    memset(super, 0, sizeof(super));
    memcpy(super, "CHFS", 4u);
    put_le32(super + 4u, CHOS_CHFS_V1_VERSION);
    put_le32(super + 8u, CHOS_BLOCK_SECTOR_SIZE);
    put_le64(super + 12u, SECTORS);
    put_le32(super + 20u, CHOS_CHFS_V1_DIR_A_LBA);
    put_le32(super + 24u, CHOS_CHFS_V1_DIR_B_LBA);
    put_le32(super + 28u, CHOS_CHFS_V1_DIRECTORY_SECTORS);
    put_le32(super + 32u, CHOS_CHFS_V1_DATA_LBA);
    put_le32(super + 508u, 0u);
    put_le32(super + 508u, chfs_crc32(super, sizeof(super)));

    memset(directory, 0, sizeof(directory));
    memcpy(directory, "CHDR", 4u);
    put_le32(directory + 4u, CHOS_CHFS_V1_VERSION);
    put_le32(directory + 8u, 1u);
    memcpy(directory + 16u, path, length);
    /* Place the old file where v2 reserves its tail metadata. */
    put_le32(directory + 16u + CHOS_CHFS_PATH_MAX, 500u);
    put_le32(directory + 20u + CHOS_CHFS_PATH_MAX, 1u);
    put_le32(directory + 24u + CHOS_CHFS_PATH_MAX, (uint32_t)size);
    put_le32(directory + 28u + CHOS_CHFS_PATH_MAX, chfs_crc32(payload, size));
    put_le32(directory + 12u, 0u);
    put_le32(directory + 12u, chfs_crc32(directory, sizeof(directory)));

    memset(backup, 0, sizeof(backup));
    memcpy(backup, "CHDR", 4u);
    put_le32(backup + 4u, CHOS_CHFS_V1_VERSION);
    put_le32(backup + 8u, 0u);
    put_le32(backup + 12u, 0u);
    put_le32(backup + 12u, chfs_crc32(backup, sizeof(backup)));

    memset(data, 0, sizeof(data));
    if (size != 0u) memcpy(data, payload, size);
    memcpy(disk, super, sizeof(super));
    memcpy(disk + CHOS_CHFS_V1_DIR_A_LBA * CHOS_BLOCK_SECTOR_SIZE, directory, sizeof(directory));
    memcpy(disk + CHOS_CHFS_V1_DIR_B_LBA * CHOS_BLOCK_SECTOR_SIZE, backup, sizeof(backup));
    memcpy(disk + 500u * CHOS_BLOCK_SECTOR_SIZE, data, sizeof(data));
}


static void set_v1_record(uint8_t *directory, size_t index, const char *path,
                          uint32_t first, uint32_t sectors, uint32_t size, uint32_t crc) {
    uint8_t *record = directory + 16u + index * 64u;
    size_t i, path_length = strlen(path);
    assert(path_length < CHOS_CHFS_PATH_MAX);
    memset(record, 0, 64u);
    for (i = 0u; i < path_length; ++i) record[i] = (uint8_t)path[i];
    put_le32(record + 48u, first);
    put_le32(record + 52u, sectors);
    put_le32(record + 56u, size);
    put_le32(record + 60u, crc);
}

static void build_v2_image_with_front_file(const char *path, const void *payload, size_t size) {
    uint8_t super[CHOS_BLOCK_SECTOR_SIZE];
    uint8_t directory[CHOS_CHFS_V2_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE];
    uint8_t backup[CHOS_CHFS_V2_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE];
    uint32_t a = SECTORS - 2u * CHOS_CHFS_V2_DIRECTORY_SECTORS;
    uint32_t b = SECTORS - CHOS_CHFS_V2_DIRECTORY_SECTORS;
    size_t path_len = strlen(path);
    assert(path_len < CHOS_CHFS_PATH_MAX && size <= CHOS_BLOCK_SECTOR_SIZE);
    memset(disk, 0, sizeof(disk));
    memset(super, 0, sizeof(super));
    memcpy(super, "CHFS", 4u);
    put_le32(super + 4u, CHOS_CHFS_V2_VERSION);
    put_le32(super + 8u, CHOS_BLOCK_SECTOR_SIZE);
    put_le64(super + 12u, SECTORS);
    put_le32(super + 20u, a); put_le32(super + 24u, b);
    put_le32(super + 28u, CHOS_CHFS_V2_DIRECTORY_SECTORS);
    put_le32(super + 32u, CHOS_CHFS_V1_DATA_LBA);
    put_le32(super + 508u, chfs_crc32(super, sizeof(super)));

    memset(directory, 0, sizeof(directory));
    memcpy(directory, "CHDR", 4u);
    put_le32(directory + 4u, CHOS_CHFS_V2_VERSION);
    put_le32(directory + 8u, 1u);
    memcpy(directory + 16u, path, path_len);
    put_le32(directory + 16u + CHOS_CHFS_PATH_MAX, CHOS_CHFS_V1_DATA_LBA);
    put_le32(directory + 20u + CHOS_CHFS_PATH_MAX, 1u);
    put_le32(directory + 24u + CHOS_CHFS_PATH_MAX, (uint32_t)size);
    put_le32(directory + 28u + CHOS_CHFS_PATH_MAX, chfs_crc32(payload, size));
    put_le32(directory + 12u, chfs_crc32(directory, sizeof(directory)));

    memset(backup, 0, sizeof(backup));
    memcpy(backup, "CHDR", 4u);
    put_le32(backup + 4u, CHOS_CHFS_V2_VERSION);
    put_le32(backup + 8u, 0u);
    put_le32(backup + 12u, chfs_crc32(backup, sizeof(backup)));
    memcpy(disk, super, sizeof(super));
    memcpy(disk + (uint64_t)a * CHOS_BLOCK_SECTOR_SIZE, directory, sizeof(directory));
    memcpy(disk + (uint64_t)b * CHOS_BLOCK_SECTOR_SIZE, backup, sizeof(backup));
    memcpy(disk + (uint64_t)CHOS_CHFS_V1_DATA_LBA * CHOS_BLOCK_SECTOR_SIZE, payload, size);
}

/* Fill every v2-eligible data sector in a legacy image, while leaving one file
 * in the tail that v2 must reserve. The migration must refuse to switch formats
 * if there is no safe sector in which to relocate that file. */
static void build_v1_image_without_migration_space(void) {
    uint8_t super[CHOS_BLOCK_SECTOR_SIZE];
    uint8_t directory[CHOS_CHFS_V1_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE];
    uint8_t backup[CHOS_CHFS_V1_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE];
    static uint8_t large_data[468u * CHOS_BLOCK_SECTOR_SIZE];
    const char tail[] = "tail data stays safe";
    size_t i;

    memset(disk, 0, sizeof(disk));
    memset(super, 0, sizeof(super));
    memcpy(super, "CHFS", 4u);
    put_le32(super + 4u, CHOS_CHFS_V1_VERSION);
    put_le32(super + 8u, CHOS_BLOCK_SECTOR_SIZE);
    put_le64(super + 12u, SECTORS);
    put_le32(super + 20u, CHOS_CHFS_V1_DIR_A_LBA);
    put_le32(super + 24u, CHOS_CHFS_V1_DIR_B_LBA);
    put_le32(super + 28u, CHOS_CHFS_V1_DIRECTORY_SECTORS);
    put_le32(super + 32u, CHOS_CHFS_V1_DATA_LBA);
    put_le32(super + 508u, chfs_crc32(super, sizeof(super)));

    memset(large_data, 'B', sizeof(large_data));
    memcpy(disk + 7u * CHOS_BLOCK_SECTOR_SIZE, large_data, sizeof(large_data));
    memset(directory, 0, sizeof(directory));
    memcpy(directory, "CHDR", 4u);
    put_le32(directory + 4u, CHOS_CHFS_V1_VERSION);
    put_le32(directory + 8u, 1u);
    set_v1_record(directory, 0u, "/BIG.BIN", 7u, 468u, (uint32_t)sizeof(large_data),
                  chfs_crc32(large_data, sizeof(large_data)));

    /* LBA 475..495: single-sector records close all v2-eligible data. */
    for (i = 0u; i < 21u; ++i) {
        char path[16];
        uint8_t marker = (uint8_t)('A' + (int)(i % 26u));
        (void)snprintf(path, sizeof(path), "/S%02u.BIN", (unsigned)i);
        disk[(475u + i) * CHOS_BLOCK_SECTOR_SIZE] = marker;
        set_v1_record(directory, i + 1u, path, (uint32_t)(475u + i), 1u, 1u,
                      chfs_crc32(&marker, sizeof(marker)));
    }
    set_v1_record(directory, 22u, "/TAIL.TXT", 500u, 1u, (uint32_t)sizeof(tail),
                  chfs_crc32(tail, sizeof(tail)));
    memcpy(disk + 500u * CHOS_BLOCK_SECTOR_SIZE, tail, sizeof(tail));
    put_le32(directory + 12u, 0u);
    put_le32(directory + 12u, chfs_crc32(directory, sizeof(directory)));

    memset(backup, 0, sizeof(backup));
    memcpy(backup, "CHDR", 4u);
    put_le32(backup + 4u, CHOS_CHFS_V1_VERSION);
    put_le32(backup + 8u, 0u);
    put_le32(backup + 12u, chfs_crc32(backup, sizeof(backup)));
    memcpy(disk, super, sizeof(super));
    memcpy(disk + CHOS_CHFS_V1_DIR_A_LBA * CHOS_BLOCK_SECTOR_SIZE, directory, sizeof(directory));
    memcpy(disk + CHOS_CHFS_V1_DIR_B_LBA * CHOS_BLOCK_SECTOR_SIZE, backup, sizeof(backup));
}

int main(void) {
    chos_chfs_t fs, remounted; chos_chfs_entry_t entry; chos_chfs_stats_t stats;
    const char payload[] = "persistent data across remount";
    const char replacement[] = "new generation with a longer persistent value";
    char output[128]; size_t n = 0u;

    memset(disk, 0, sizeof(disk)); reads = writes = 0; fail_write_at = -1;
    assert(block_read(&device, 0, 0, output) == 0);
    assert(block_read(&device, SECTORS, 1, output) == 0);
    assert(chfs_format(&device));
    assert(chfs_mount(&fs, &device));
    assert(chfs_validate(&fs));
    assert(!chfs_lookup(&fs, "/NOPE.TXT", &entry));
    assert(chfs_is_directory(&fs, "/"));
    assert(chfs_make_directory(&fs, "/DOCS"));
    assert(chfs_is_directory(&fs, "/DOCS"));
    assert(chfs_make_directory(&fs, "/DOCS"));
    assert(chfs_write_file(&fs, "/DOCS/NOTE.TXT", payload, sizeof(payload)));
    assert(chfs_lookup(&fs, "/docs/note.txt", &entry));
    assert(entry.size_bytes == sizeof(payload));
    memset(output, 0, sizeof(output));
    assert(chfs_read_file(&fs, "/DOCS/NOTE.TXT", output, sizeof(output), &n));
    assert(n == sizeof(payload) && memcmp(output, payload, n) == 0);

    assert(chfs_mount(&remounted, &device));
    assert(chfs_read_file(&remounted, "/docs/note.txt", output, sizeof(output), &n));
    assert(memcmp(output, payload, n) == 0);
    assert(chfs_write_file(&remounted, "/DOCS/NOTE.TXT", replacement, sizeof(replacement)));
    assert(chfs_mount(&fs, &device));
    assert(chfs_read_file(&fs, "/DOCS/NOTE.TXT", output, sizeof(output), &n));
    assert(n == sizeof(replacement) && memcmp(output, replacement, n) == 0);

    chfs_get_stats(&fs, &stats);
    assert(stats.file_count == 1 && stats.directory_count == 1 && stats.generation >= 3 && stats.used_sectors >= 1);
    assert(chfs_remove_file(&fs, "/DOCS/NOTE.TXT"));
    assert(chfs_mount(&remounted, &device));
    assert(!chfs_lookup(&remounted, "/DOCS/NOTE.TXT", &entry));
    assert(chfs_remove_directory(&remounted, "/DOCS"));
    assert(!chfs_is_directory(&remounted, "/DOCS"));

    assert(chfs_write_file(&remounted, "/EMPTY.TXT", NULL, 0u));
    assert(chfs_lookup(&remounted, "/EMPTY.TXT", &entry) && entry.size_bytes == 0u);
    assert(chfs_read_file(&remounted, "/EMPTY.TXT", NULL, 0u, &n) && n == 0u);
    assert(chfs_remove_file(&remounted, "/EMPTY.TXT"));
    assert(chfs_write_file(&remounted, "/DATA.BIN", payload, sizeof(payload)));
    assert(chfs_lookup(&remounted, "/DATA.BIN", &entry));
    disk[(size_t)entry.first_sector * CHOS_BLOCK_SECTOR_SIZE] ^= 0x55u;
    assert(!chfs_read_file(&remounted, "/DATA.BIN", output, sizeof(output), &n));

    /* Rename commits metadata only and preserves the original data extent. */
    assert(chfs_write_file(&remounted, "/TO-RENAME.TXT", payload, sizeof(payload)));
    assert(chfs_rename_file(&remounted, "/TO-RENAME.TXT", "/RENAMED.TXT"));
    assert(chfs_rename_file(&remounted, "/renamed.txt", "/RENAMED.TXT"));
    assert(!chfs_lookup(&remounted, "/TO-RENAME.TXT", &entry));
    assert(chfs_read_file(&remounted, "/RENAMED.TXT", output, sizeof(output), &n));
    assert(n == sizeof(payload) && memcmp(output, payload, n) == 0);

    /* Moving a directory rewrites the entire subtree without moving file data. */
    assert(chfs_format(&device));
    assert(chfs_mount(&fs, &device));
    assert(chfs_make_directory(&fs, "/DESKTOP"));
    assert(chfs_make_directory(&fs, "/DESKTOP/SUB"));
    assert(chfs_make_directory(&fs, "/TRASH"));
    assert(chfs_make_directory(&fs, "/DOCUMENTS"));
    assert(chfs_write_file(&fs, "/DESKTOP/SUB/NOTE.TXT", payload, sizeof(payload)));
    assert(chfs_rename_directory(&fs, "/DESKTOP/SUB", "/TRASH/SUB"));
    assert(!chfs_lookup(&fs, "/DESKTOP/SUB/NOTE.TXT", &entry));
    assert(chfs_read_file(&fs, "/TRASH/SUB/NOTE.TXT", output, sizeof(output), &n));
    assert(n == sizeof(payload) && memcmp(output, payload, n) == 0);
    /* File Explorer-style cross-folder moves keep file contents intact. */
    assert(chfs_rename_file(&fs, "/TRASH/SUB/NOTE.TXT", "/DOCUMENTS/NOTE.TXT"));
    assert(!chfs_lookup(&fs, "/TRASH/SUB/NOTE.TXT", &entry));
    assert(chfs_read_file(&fs, "/DOCUMENTS/NOTE.TXT", output, sizeof(output), &n));
    assert(n == sizeof(payload) && memcmp(output, payload, n) == 0);
    assert(chfs_write_file(&fs, "/TRASH/KEEP.TXT", replacement, sizeof(replacement)));
    assert(!chfs_rename_file(&fs, "/TRASH/KEEP.TXT", "/DOCUMENTS/NOTE.TXT"));
    assert(chfs_lookup(&fs, "/TRASH/KEEP.TXT", &entry)); /* collision preserves source */
    assert(!chfs_rename_directory(&fs, "/TRASH/SUB", "/TRASH/SUB/CHILD"));
    assert(!chfs_rename_directory(&fs, "/TRASH/SUB", "/DESKTOP"));
    assert(chfs_validate(&fs));

    /* Protect old data extents referenced by the backup directory generation.
     * A failed metadata commit followed by corruption of the active copy must
     * still mount the older generation and read its original payload. */
    assert(chfs_format(&device));
    assert(chfs_mount(&fs, &device));
    assert(chfs_write_file(&fs, "/ROLLBACK.TXT", payload, sizeof(payload)));
    assert(chfs_lookup(&fs, "/ROLLBACK.TXT", &entry));
    {
        uint32_t old_sector = entry.first_sector;
        assert(chfs_write_file(&fs, "/ROLLBACK.TXT", replacement, sizeof(replacement)));
        assert(chfs_lookup(&fs, "/ROLLBACK.TXT", &entry));
        assert(entry.first_sector != old_sector);
        fail_write_at = writes + 2; /* data succeeds; alternate metadata write fails */
        assert(!chfs_write_file(&fs, "/UNCOMMITTED.TXT", "new data", 8u));
        fail_write_at = -1;
        disk[fs.directory_a_lba * CHOS_BLOCK_SECTOR_SIZE + 12u] ^= 0x80u;
        assert(chfs_mount(&remounted, &device));
        assert(chfs_lookup(&remounted, "/ROLLBACK.TXT", &entry));
        assert(entry.first_sector == old_sector);
        assert(chfs_read_file(&remounted, "/ROLLBACK.TXT", output, sizeof(output), &n));
        assert(n == sizeof(payload) && memcmp(output, payload, n) == 0);
        assert(!chfs_lookup(&remounted, "/UNCOMMITTED.TXT", &entry));
    }

    /* A torn newest directory leaves the previous directory copy mountable. */
    assert(chfs_format(&device));
    assert(chfs_mount(&fs, &device));
    assert(chfs_write_file(&fs, "/OLD.TXT", payload, sizeof(payload)));
    assert(chfs_write_file(&fs, "/NEW.TXT", replacement, sizeof(replacement)));
    disk[fs.directory_a_lba * CHOS_BLOCK_SECTOR_SIZE + 12u] ^= 0x80u;
    assert(chfs_mount(&remounted, &device));
    assert(chfs_lookup(&remounted, "/OLD.TXT", &entry));
    assert(chfs_validate(&remounted));


    /* V1 images migrate through v2 into v3. The legacy file overlaps the v2 tail
     * reservation and then the v3 front metadata, so both migrations must copy it. */
    {
        unsigned directory_index;
        build_v1_image_with_tail_file("/TAIL.TXT", payload, sizeof(payload));
        reads = writes = 0; fail_write_at = -1;
        assert(chfs_mount(&fs, &device));
        assert(fs.version == CHOS_CHFS_VERSION);
        assert(fs.directory_a_lba == CHOS_CHFS_V3_DIR_A_LBA);
        assert(chfs_lookup(&fs, "/TAIL.TXT", &entry));
        assert(entry.first_sector < fs.data_end_lba);
        memset(output, 0, sizeof(output));
        assert(chfs_read_file(&fs, "/TAIL.TXT", output, sizeof(output), &n));
        assert(n == sizeof(payload) && memcmp(output, payload, n) == 0);
        for (directory_index = 0u; directory_index < 510u; ++directory_index) {
            char path[CHOS_CHFS_PATH_MAX];
            (void)snprintf(path, sizeof(path), "/D%03u", directory_index);
            assert(chfs_make_directory(&fs, path));
        }
        assert(chfs_validate(&fs));
        assert(chfs_mount(&remounted, &device));
        assert(remounted.version == CHOS_CHFS_VERSION);
        assert(chfs_lookup(&remounted, "/TAIL.TXT", &entry));
        for (directory_index = 0u; directory_index < 510u; ++directory_index) {
            char path[CHOS_CHFS_PATH_MAX];
            (void)snprintf(path, sizeof(path), "/D%03u", directory_index);
            assert(chfs_is_directory(&remounted, path));
        }
    }

    /* V2-to-v3 migration relocates data from the newly reserved front metadata
     * area, preserving the old tail metadata until v3 commits its superblock. */
    build_v2_image_with_front_file("/EARLY.TXT", payload, sizeof(payload));
    reads = writes = 0; fail_write_at = -1;
    assert(chfs_mount(&fs, &device));
    assert(fs.version == CHOS_CHFS_VERSION);
    assert(fs.directory_a_lba == CHOS_CHFS_V3_DIR_A_LBA);
    assert(chfs_lookup(&fs, "/EARLY.TXT", &entry));
    assert(entry.first_sector >= CHOS_CHFS_V3_DATA_LBA);
    memset(output, 0, sizeof(output));
    assert(chfs_read_file(&fs, "/EARLY.TXT", output, sizeof(output), &n));
    assert(n == sizeof(payload) && memcmp(output, payload, n) == 0);
    assert(chfs_mount(&remounted, &device));
    assert(chfs_validate(&remounted));
    assert(chfs_read_file(&remounted, "/EARLY.TXT", output, sizeof(output), &n));
    assert(n == sizeof(payload) && memcmp(output, payload, n) == 0);

    /* If the legacy image has no free lower extent for a tail file, migration
     * must leave it mounted as v1 without formatting or losing the file. */
    build_v1_image_without_migration_space();
    reads = writes = 0; fail_write_at = -1;
    assert(chfs_mount(&fs, &device));
    assert(fs.version == CHOS_CHFS_V1_VERSION);
    assert(chfs_lookup(&fs, "/TAIL.TXT", &entry));
    memset(output, 0, sizeof(output));
    assert(chfs_read_file(&fs, "/TAIL.TXT", output, sizeof(output), &n));
    assert(n == sizeof("tail data stays safe"));
    assert(memcmp(output, "tail data stays safe", n) == 0);
    assert(chfs_mount(&remounted, &device));
    assert(remounted.version == CHOS_CHFS_V1_VERSION);
    assert(chfs_read_file(&remounted, "/TAIL.TXT", output, sizeof(output), &n));
    assert(memcmp(output, "tail data stays safe", n) == 0);

    printf("CHFS: format/mount, v1 migration/fallback, 511-entry v3 catalogue, bounds, checksums, directories, cross-directory moves, collision safety, empty files, replacement, rename, backup extent safety, remount, remove and recovery passed\n");
    return 0;
}
