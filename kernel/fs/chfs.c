#include "chos/chfs.h"
#include "chos/romfs.h"
#include <stddef.h>

#define CHFS_SUPER_CRC_OFFSET 508u
#define CHFS_HEADER_SIZE 16u
#define CHFS_ENTRY_SIZE (CHOS_CHFS_PATH_MAX + 16u)
#define CHFS_ENTRY_OFFSET 16u
#define CHFS_VOLUME_MIN_SECTORS 130u
#define CHFS_V2_VOLUME_MIN_SECTORS 24u
#define CHFS_V1_VOLUME_MIN_SECTORS 8u
#define CHFS_FS_MAGIC "CHFS"
#define CHFS_DIR_MAGIC "CHDR"
static int commit_entries(chos_chfs_t *fs, const chos_chfs_entry_t *entries);
static size_t entry_capacity(const chos_chfs_t *fs);
static int chfs_migrate_v1(chos_chfs_t *fs);
static int chfs_migrate_v2(chos_chfs_t *fs);
/* Mount is single-threaded during storage initialization. Keeping the candidate
 * directory off the early boot/task stack avoids a large nested stack frame. */
static chos_chfs_entry_t chfs_mount_candidate[CHOS_CHFS_MAX_FILES];
/* Storage calls are serialized by storage_lock. Keep the large backup catalogue
 * and directory scratch off the 16 KiB per-task kernel stack; the v2 table and
 * directory sector count otherwise make nested write paths uncomfortably deep. */
static chos_chfs_entry_t chfs_allocator_backup[CHOS_CHFS_MAX_FILES];
/* File operations are serialized by storage_lock; keeping the working catalogue
 * here avoids allocating a second 8 KiB catalogue on a small kernel task stack. */
static chos_chfs_entry_t chfs_operation_entries[CHOS_CHFS_MAX_FILES];
static uint8_t chfs_allocator_raw[CHOS_CHFS_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE];
_Static_assert(CHFS_ENTRY_OFFSET + CHOS_CHFS_MAX_FILES * CHFS_ENTRY_SIZE <= CHOS_CHFS_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE, "CHFS metadata table exceeds directory area");

static void bytes_zero(void *buffer, size_t length) {
    uint8_t *p = (uint8_t *)buffer;
    size_t i;
    for (i = 0u; i < length; ++i) p[i] = 0u;
}
static void bytes_copy(void *dst, const void *src, size_t length) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; size_t i;
    for (i = 0u; i < length; ++i) d[i] = s[i];
}
static int bytes_equal(const void *left, const void *right, size_t length) {
    const uint8_t *a = (const uint8_t *)left; const uint8_t *b = (const uint8_t *)right; size_t i;
    for (i = 0u; i < length; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32); }
static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }

uint32_t chfs_crc32(const void *data, size_t length) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xffffffffu;
    size_t i; unsigned bit;
    if (data == NULL && length != 0u) return 0u;
    for (i = 0u; i < length; ++i) {
        crc ^= p[i];
        for (bit = 0u; bit < 8u; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

static int path_valid(const char *path) {
    size_t n = 0u;
    if (path == NULL || !romfs_path_is_valid(path)) return 0;
    while (path[n] != '\0') { if (++n >= CHOS_CHFS_PATH_MAX) return 0; }
    return n > 1u;
}
static int path_equal(const char *a, const char *b) { return romfs_path_equal(a, b); }

static void encode_entry(uint8_t *p, const chos_chfs_entry_t *e) {
    size_t i; for (i = 0u; i < CHOS_CHFS_PATH_MAX; ++i) p[i] = (uint8_t)e->path[i];
    put32(p + 48u, e->first_sector); put32(p + 52u, e->sector_count);
    put32(p + 56u, e->size_bytes); put32(p + 60u, e->crc32);
}
static void decode_entry(const uint8_t *p, chos_chfs_entry_t *e) {
    size_t i; for (i = 0u; i < CHOS_CHFS_PATH_MAX; ++i) e->path[i] = (char)p[i];
    e->path[CHOS_CHFS_PATH_MAX - 1u] = '\0';
    e->first_sector = get32(p + 48u); e->sector_count = get32(p + 52u);
    e->size_bytes = get32(p + 56u); e->crc32 = get32(p + 60u);
}
static void encode_directory(uint8_t *raw, const chos_chfs_entry_t *entries, uint32_t generation) {
    size_t i;
    bytes_zero(raw, CHOS_CHFS_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE);
    bytes_copy(raw, CHFS_DIR_MAGIC, 4u); put32(raw + 4u, CHOS_CHFS_VERSION); put32(raw + 8u, generation);
    put32(raw + 12u, 0u);
    for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i)
        encode_entry(raw + CHFS_ENTRY_OFFSET + i * CHFS_ENTRY_SIZE, &entries[i]);
    put32(raw + 12u, chfs_crc32(raw, CHOS_CHFS_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE));
}
static int decode_directory(uint8_t *raw, uint32_t *generation, chos_chfs_entry_t *entries) {
    uint32_t expected, found; size_t i;
    if (!bytes_equal(raw, CHFS_DIR_MAGIC, 4u) || get32(raw + 4u) != CHOS_CHFS_VERSION) return 0;
    found = get32(raw + 12u); put32(raw + 12u, 0u);
    expected = chfs_crc32(raw, CHOS_CHFS_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE);
    put32(raw + 12u, found);
    if (expected != found) return 0;
    *generation = get32(raw + 8u);
    for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i)
        decode_entry(raw + CHFS_ENTRY_OFFSET + i * CHFS_ENTRY_SIZE, &entries[i]);
    return 1;
}
static void encode_directory_v2(uint8_t *raw, const chos_chfs_entry_t *entries, uint32_t generation) {
    size_t i;
    bytes_zero(raw, CHOS_CHFS_V2_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE);
    bytes_copy(raw, CHFS_DIR_MAGIC, 4u); put32(raw + 4u, CHOS_CHFS_V2_VERSION); put32(raw + 8u, generation);
    put32(raw + 12u, 0u);
    for (i = 0u; i < CHOS_CHFS_V2_MAX_FILES; ++i)
        encode_entry(raw + CHFS_ENTRY_OFFSET + i * CHFS_ENTRY_SIZE, &entries[i]);
    put32(raw + 12u, chfs_crc32(raw, CHOS_CHFS_V2_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE));
}
static int decode_directory_v2(uint8_t *raw, uint32_t *generation, chos_chfs_entry_t *entries) {
    uint32_t expected, found; size_t i;
    if (!bytes_equal(raw, CHFS_DIR_MAGIC, 4u) || get32(raw + 4u) != CHOS_CHFS_V2_VERSION) return 0;
    found = get32(raw + 12u); put32(raw + 12u, 0u);
    expected = chfs_crc32(raw, CHOS_CHFS_V2_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE);
    put32(raw + 12u, found);
    if (expected != found) return 0;
    *generation = get32(raw + 8u);
    bytes_zero(entries, sizeof(chos_chfs_entry_t) * CHOS_CHFS_MAX_FILES);
    for (i = 0u; i < CHOS_CHFS_V2_MAX_FILES; ++i)
        decode_entry(raw + CHFS_ENTRY_OFFSET + i * CHFS_ENTRY_SIZE, &entries[i]);
    return 1;
}
static void encode_directory_v1(uint8_t *raw, const chos_chfs_entry_t *entries, uint32_t generation) {
    size_t i;
    bytes_zero(raw, CHOS_CHFS_V1_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE);
    bytes_copy(raw, CHFS_DIR_MAGIC, 4u); put32(raw + 4u, CHOS_CHFS_V1_VERSION); put32(raw + 8u, generation);
    put32(raw + 12u, 0u);
    for (i = 0u; i < CHOS_CHFS_V1_MAX_FILES; ++i)
        encode_entry(raw + CHFS_ENTRY_OFFSET + i * CHFS_ENTRY_SIZE, &entries[i]);
    put32(raw + 12u, chfs_crc32(raw, CHOS_CHFS_V1_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE));
}
static int decode_directory_v1(uint8_t *raw, uint32_t *generation, chos_chfs_entry_t *entries) {
    uint32_t expected, found; size_t i;
    if (!bytes_equal(raw, CHFS_DIR_MAGIC, 4u) || get32(raw + 4u) != CHOS_CHFS_V1_VERSION) return 0;
    found = get32(raw + 12u); put32(raw + 12u, 0u);
    expected = chfs_crc32(raw, CHOS_CHFS_V1_DIRECTORY_SECTORS * CHOS_BLOCK_SECTOR_SIZE);
    put32(raw + 12u, found);
    if (expected != found) return 0;
    *generation = get32(raw + 8u);
    bytes_zero(entries, sizeof(chos_chfs_entry_t) * CHOS_CHFS_MAX_FILES);
    for (i = 0u; i < CHOS_CHFS_V1_MAX_FILES; ++i)
        decode_entry(raw + CHFS_ENTRY_OFFSET + i * CHFS_ENTRY_SIZE, &entries[i]);
    return 1;
}

int chfs_entry_is_directory(const chos_chfs_entry_t *entry) {
    return entry != NULL && entry->path[0] != '\0' &&
        entry->first_sector == 0u && entry->sector_count == 0u &&
        entry->size_bytes == 0u && entry->crc32 == CHOS_CHFS_ENTRY_DIRECTORY_TAG;
}

static int entry_is_zero_length_file(const chos_chfs_entry_t *entry) {
    return entry != NULL && entry->path[0] != '\0' && entry->first_sector == 0u &&
        entry->sector_count == 0u && entry->size_bytes == 0u && entry->crc32 == 0u;
}

static int path_prefix_component(const char *prefix, size_t prefix_length, const char *path) {
    size_t i, path_length = 0u;
    if (prefix == NULL || path == NULL) return 0;
    while (path[path_length] != '\0') ++path_length;
    if (path_length <= prefix_length) return 0;
    for (i = 0u; i < prefix_length; ++i) {
        int a = prefix[i], b = path[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return 0;
    }
    return path[prefix_length] == '/';
}

static int entries_valid(const chos_chfs_entry_t *entries, uint64_t total_sectors) {
    size_t i, j;
    for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i) {
        const chos_chfs_entry_t *e = &entries[i];
        uint64_t end = 0u;
        int is_dir, is_empty;
        if (e->path[0] == '\0') continue;
        if (!path_valid(e->path)) return 0;
        is_dir = chfs_entry_is_directory(e);
        is_empty = entry_is_zero_length_file(e);
        if (is_dir || is_empty) {
            if (e->first_sector != 0u || e->sector_count != 0u || e->size_bytes != 0u) return 0;
        } else {
            if (e->sector_count == 0u || e->size_bytes == 0u ||
                e->size_bytes > (uint64_t)e->sector_count * CHOS_BLOCK_SECTOR_SIZE ||
                e->first_sector < CHOS_CHFS_V1_DATA_LBA) return 0;
            end = (uint64_t)e->first_sector + e->sector_count;
            if (end > total_sectors || end < e->first_sector) return 0;
        }
        for (j = 0u; j < i; ++j) {
            const chos_chfs_entry_t *o = &entries[j];
            int o_is_dir, o_is_empty;
            uint64_t oe = 0u;
            if (o->path[0] == '\0') continue;
            if (path_equal(e->path, o->path)) return 0;
            o_is_dir = chfs_entry_is_directory(o);
            o_is_empty = entry_is_zero_length_file(o);
            if (!is_dir && !is_empty && !o_is_dir && !o_is_empty) {
                oe = (uint64_t)o->first_sector + o->sector_count;
                if ((uint64_t)e->first_sector < oe && (uint64_t)o->first_sector < end) return 0;
            }
        }
    }
    /* Reject any file path that is the parent of another stored path. */
    for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i) {
        const chos_chfs_entry_t *e = &entries[i];
        size_t length;
        if (e->path[0] == '\0' || chfs_entry_is_directory(e)) continue;
        length = 0u; while (e->path[length] != '\0') ++length;
        for (j = 0u; j < CHOS_CHFS_MAX_FILES; ++j) {
            const chos_chfs_entry_t *o = &entries[j];
            if (i == j || o->path[0] == '\0') continue;
            if (path_prefix_component(e->path, length, o->path)) return 0;
        }
    }
    return 1;
}

static int entries_v3_valid(const chos_chfs_entry_t *entries, uint64_t total_sectors) {
    size_t i;
    if (!entries_valid(entries, total_sectors)) return 0;
    for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i) {
        const chos_chfs_entry_t *e = &entries[i];
        if (e->path[0] == '\0' || chfs_entry_is_directory(e) || entry_is_zero_length_file(e)) continue;
        if (e->first_sector < CHOS_CHFS_V3_DATA_LBA) return 0;
    }
    return 1;
}
static int make_superblock_version(uint8_t *raw, uint32_t version, uint64_t sectors,
                                   uint32_t directory_a_lba, uint32_t directory_b_lba,
                                   uint32_t directory_sectors, uint32_t data_start_lba) {
    if (sectors < CHFS_V1_VOLUME_MIN_SECTORS || sectors > UINT32_MAX) return 0;
    bytes_zero(raw, CHOS_BLOCK_SECTOR_SIZE);
    bytes_copy(raw, CHFS_FS_MAGIC, 4u); put32(raw + 4u, version);
    put32(raw + 8u, CHOS_BLOCK_SECTOR_SIZE); put64(raw + 12u, sectors);
    put32(raw + 20u, directory_a_lba); put32(raw + 24u, directory_b_lba);
    put32(raw + 28u, directory_sectors); put32(raw + 32u, data_start_lba);
    put32(raw + CHFS_SUPER_CRC_OFFSET, 0u);
    put32(raw + CHFS_SUPER_CRC_OFFSET, chfs_crc32(raw, CHOS_BLOCK_SECTOR_SIZE));
    return 1;
}
static int make_superblock(uint8_t *raw, uint64_t sectors, uint32_t directory_a_lba,
                           uint32_t directory_b_lba, uint32_t directory_sectors,
                           uint32_t data_start_lba) {
    return make_superblock_version(raw, CHOS_CHFS_VERSION, sectors, directory_a_lba,
                                   directory_b_lba, directory_sectors, data_start_lba);
}
static int superblock_crc_valid(uint8_t *raw) {
    uint32_t expected, found;
    found = get32(raw + CHFS_SUPER_CRC_OFFSET);
    put32(raw + CHFS_SUPER_CRC_OFFSET, 0u);
    expected = chfs_crc32(raw, CHOS_BLOCK_SECTOR_SIZE);
    put32(raw + CHFS_SUPER_CRC_OFFSET, found);
    return expected == found;
}
static int check_superblock_v1(uint8_t *raw, uint64_t device_sectors, uint64_t *sectors) {
    uint64_t declared;
    if (!bytes_equal(raw, CHFS_FS_MAGIC, 4u) || get32(raw + 4u) != CHOS_CHFS_V1_VERSION ||
        get32(raw + 8u) != CHOS_BLOCK_SECTOR_SIZE || get32(raw + 20u) != CHOS_CHFS_V1_DIR_A_LBA ||
        get32(raw + 24u) != CHOS_CHFS_V1_DIR_B_LBA ||
        get32(raw + 28u) != CHOS_CHFS_V1_DIRECTORY_SECTORS || get32(raw + 32u) != CHOS_CHFS_V1_DATA_LBA)
        return 0;
    declared = get64(raw + 12u);
    if (!superblock_crc_valid(raw) || declared < CHFS_V1_VOLUME_MIN_SECTORS ||
        declared > device_sectors || declared > UINT32_MAX) return 0;
    *sectors = declared;
    return 1;
}
static int check_superblock_v3(uint8_t *raw, uint64_t device_sectors, uint64_t *sectors,
                               uint32_t *directory_a_lba, uint32_t *directory_b_lba,
                               uint32_t *data_start_lba) {
    uint64_t declared;
    uint32_t a, b, directory_sectors, data_start;
    if (!bytes_equal(raw, CHFS_FS_MAGIC, 4u) || get32(raw + 4u) != CHOS_CHFS_VERSION ||
        get32(raw + 8u) != CHOS_BLOCK_SECTOR_SIZE) return 0;
    declared = get64(raw + 12u);
    a = get32(raw + 20u); b = get32(raw + 24u);
    directory_sectors = get32(raw + 28u); data_start = get32(raw + 32u);
    if (!superblock_crc_valid(raw) || declared < CHFS_VOLUME_MIN_SECTORS ||
        declared > device_sectors || declared > UINT32_MAX ||
        directory_sectors != CHOS_CHFS_DIRECTORY_SECTORS ||
        a != CHOS_CHFS_V3_DIR_A_LBA || b != CHOS_CHFS_V3_DIR_B_LBA ||
        data_start != CHOS_CHFS_V3_DATA_LBA || a != 1u || b + directory_sectors != data_start ||
        (uint64_t)b + directory_sectors > declared) return 0;
    *sectors = declared;
    *directory_a_lba = a;
    *directory_b_lba = b;
    *data_start_lba = data_start;
    return 1;
}

static int check_superblock_v2(uint8_t *raw, uint64_t device_sectors, uint64_t *sectors,
                               uint32_t *directory_a_lba, uint32_t *directory_b_lba,
                               uint32_t *data_start_lba) {
    uint64_t declared;
    uint32_t a, b, directory_sectors, data_start;
    if (!bytes_equal(raw, CHFS_FS_MAGIC, 4u) || get32(raw + 4u) != CHOS_CHFS_V2_VERSION ||
        get32(raw + 8u) != CHOS_BLOCK_SECTOR_SIZE) return 0;
    declared = get64(raw + 12u);
    a = get32(raw + 20u); b = get32(raw + 24u);
    directory_sectors = get32(raw + 28u); data_start = get32(raw + 32u);
    if (!superblock_crc_valid(raw) || declared < CHFS_V2_VOLUME_MIN_SECTORS ||
        declared > device_sectors || declared > UINT32_MAX ||
        directory_sectors != CHOS_CHFS_V2_DIRECTORY_SECTORS ||
        a != declared - (uint64_t)(2u * CHOS_CHFS_V2_DIRECTORY_SECTORS) ||
        b != declared - CHOS_CHFS_V2_DIRECTORY_SECTORS || data_start != CHOS_CHFS_V1_DATA_LBA ||
        (uint64_t)b + directory_sectors > declared) return 0;
    *sectors = declared; *directory_a_lba = a; *directory_b_lba = b; *data_start_lba = data_start;
    return 1;
}

int chfs_format(chos_block_device_t *device) {
    uint8_t super[CHOS_BLOCK_SECTOR_SIZE];
    uint8_t *directory = chfs_allocator_raw;
    static chos_chfs_entry_t empty[CHOS_CHFS_MAX_FILES];
    uint32_t directory_a_lba, directory_b_lba;
    size_t i;
    if (device == NULL || !device->ready || device->sector_count < CHFS_VOLUME_MIN_SECTORS ||
        device->sector_count > UINT32_MAX) return 0;
    directory_a_lba = CHOS_CHFS_V3_DIR_A_LBA;
    directory_b_lba = CHOS_CHFS_V3_DIR_B_LBA;
    for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i) bytes_zero(&empty[i], sizeof(empty[i]));
    if (!make_superblock(super, device->sector_count, directory_a_lba, directory_b_lba,
                         CHOS_CHFS_DIRECTORY_SECTORS, CHOS_CHFS_V3_DATA_LBA)) return 0;
    encode_directory(directory, empty, 1u);
    if (!block_write(device, directory_a_lba, CHOS_CHFS_DIRECTORY_SECTORS, directory)) return 0;
    encode_directory(directory, empty, 0u);
    if (!block_write(device, directory_b_lba, CHOS_CHFS_DIRECTORY_SECTORS, directory) ||
        !block_flush(device) || !block_write(device, 0u, 1u, super) || !block_flush(device)) return 0;
    return 1;
}

int chfs_mount(chos_chfs_t *fs, chos_block_device_t *device) {
    /* Reuse this sector buffer for the superblock and then each directory copy. */
    uint8_t *raw = chfs_allocator_raw;
    uint64_t sectors = 0u;
    uint32_t ga = 0u, gb = 0u, a_lba = 0u, b_lba = 0u, data_start = CHOS_CHFS_DATA_LBA;
    uint32_t version;
    int va, vb;
    if (fs == NULL || device == NULL || !device->ready) return 0;
    bytes_zero(fs, sizeof(*fs));
    if (!block_read(device, 0u, 1u, raw) || !bytes_equal(raw, CHFS_FS_MAGIC, 4u)) return 0;
    version = get32(raw + 4u);
    if (version == CHOS_CHFS_VERSION) {
        if (!check_superblock_v3(raw, device->sector_count, &sectors, &a_lba, &b_lba, &data_start) ||
            !block_read(device, a_lba, CHOS_CHFS_DIRECTORY_SECTORS, raw)) return 0;
        va = decode_directory(raw, &ga, fs->entries) &&
             entries_v3_valid(fs->entries, sectors);
        if (!block_read(device, b_lba, CHOS_CHFS_DIRECTORY_SECTORS, raw)) return 0;
        vb = decode_directory(raw, &gb, chfs_mount_candidate) &&
             entries_v3_valid(chfs_mount_candidate, sectors);
        if (!va && !vb) return 0;
        fs->device = device; fs->total_sectors = sectors; fs->mounted = 1;
        fs->directory_a_lba = a_lba; fs->directory_b_lba = b_lba;
        fs->directory_sectors = CHOS_CHFS_DIRECTORY_SECTORS;
        fs->data_start_lba = data_start; fs->data_end_lba = (uint32_t)sectors; fs->version = CHOS_CHFS_VERSION;
        if (vb && (!va || (int32_t)(gb - ga) > 0)) {
            bytes_copy(fs->entries, chfs_mount_candidate, sizeof(fs->entries));
            fs->generation = gb; fs->active_directory_lba = b_lba;
        } else {
            fs->generation = ga; fs->active_directory_lba = a_lba;
        }
        return 1;
    }
    if (version == CHOS_CHFS_V2_VERSION) {
        if (!check_superblock_v2(raw, device->sector_count, &sectors, &a_lba, &b_lba, &data_start) ||
            !block_read(device, a_lba, CHOS_CHFS_V2_DIRECTORY_SECTORS, raw)) return 0;
        va = decode_directory_v2(raw, &ga, fs->entries) &&
             entries_valid(fs->entries, sectors - 2u * CHOS_CHFS_V2_DIRECTORY_SECTORS);
        if (!block_read(device, b_lba, CHOS_CHFS_V2_DIRECTORY_SECTORS, raw)) return 0;
        vb = decode_directory_v2(raw, &gb, chfs_mount_candidate) &&
             entries_valid(chfs_mount_candidate, sectors - 2u * CHOS_CHFS_V2_DIRECTORY_SECTORS);
        if (!va && !vb) return 0;
        fs->device = device; fs->total_sectors = sectors; fs->mounted = 1;
        fs->directory_a_lba = a_lba; fs->directory_b_lba = b_lba;
        fs->directory_sectors = CHOS_CHFS_V2_DIRECTORY_SECTORS;
        fs->data_start_lba = data_start; fs->data_end_lba = a_lba; fs->version = CHOS_CHFS_V2_VERSION;
        if (vb && (!va || (int32_t)(gb - ga) > 0)) {
            bytes_copy(fs->entries, chfs_mount_candidate, sizeof(fs->entries));
            fs->generation = gb; fs->active_directory_lba = b_lba;
        } else {
            fs->generation = ga; fs->active_directory_lba = a_lba;
        }
        /* V2 remains usable if there is insufficient safe space to grow metadata. */
        (void)chfs_migrate_v2(fs);
        return 1;
    }
    if (version == CHOS_CHFS_V1_VERSION) {
        if (!check_superblock_v1(raw, device->sector_count, &sectors) ||
            !block_read(device, CHOS_CHFS_V1_DIR_A_LBA, CHOS_CHFS_V1_DIRECTORY_SECTORS, raw)) return 0;
        va = decode_directory_v1(raw, &ga, fs->entries) && entries_valid(fs->entries, sectors);
        if (!block_read(device, CHOS_CHFS_V1_DIR_B_LBA, CHOS_CHFS_V1_DIRECTORY_SECTORS, raw)) return 0;
        vb = decode_directory_v1(raw, &gb, chfs_mount_candidate) && entries_valid(chfs_mount_candidate, sectors);
        if (!va && !vb) return 0;
        fs->device = device; fs->total_sectors = sectors; fs->mounted = 1;
        fs->directory_a_lba = CHOS_CHFS_V1_DIR_A_LBA; fs->directory_b_lba = CHOS_CHFS_V1_DIR_B_LBA;
        fs->directory_sectors = CHOS_CHFS_V1_DIRECTORY_SECTORS;
        fs->data_start_lba = CHOS_CHFS_V1_DATA_LBA; fs->data_end_lba = (uint32_t)sectors;
        fs->version = CHOS_CHFS_V1_VERSION;
        if (vb && (!va || (int32_t)(gb - ga) > 0)) {
            bytes_copy(fs->entries, chfs_mount_candidate, sizeof(fs->entries));
            fs->generation = gb; fs->active_directory_lba = CHOS_CHFS_V1_DIR_B_LBA;
        } else {
            fs->generation = ga; fs->active_directory_lba = CHOS_CHFS_V1_DIR_A_LBA;
        }
        /* Upgrade only after the old filesystem is mounted and verified. If the
         * disk lacks safe space for relocation, keep the v1 volume usable. */
        if (chfs_migrate_v1(fs) && fs->version == CHOS_CHFS_V2_VERSION)
            (void)chfs_migrate_v2(fs);
        return 1;
    }
    return 0;
}

int chfs_validate(chos_chfs_t *fs) {
    uint8_t super[CHOS_BLOCK_SECTOR_SIZE];
    uint64_t sectors = 0u;
    if (fs == NULL || !fs->mounted || !block_read(fs->device, 0u, 1u, super)) return 0;
    if (fs->version == CHOS_CHFS_V1_VERSION) {
        return check_superblock_v1(super, fs->device->sector_count, &sectors) &&
            sectors == fs->total_sectors && entries_valid(fs->entries, fs->total_sectors);
    }
    if (fs->version == CHOS_CHFS_V2_VERSION) {
        uint32_t a = 0u, b = 0u, data_start = 0u;
        return check_superblock_v2(super, fs->device->sector_count, &sectors, &a, &b, &data_start) &&
            sectors == fs->total_sectors && a == fs->directory_a_lba && b == fs->directory_b_lba &&
            data_start == fs->data_start_lba && entries_valid(fs->entries, fs->data_end_lba);
    }
    {
        uint32_t a = 0u, b = 0u, data_start = 0u;
        return check_superblock_v3(super, fs->device->sector_count, &sectors, &a, &b, &data_start) &&
            sectors == fs->total_sectors && a == fs->directory_a_lba && b == fs->directory_b_lba &&
            data_start == fs->data_start_lba && entries_v3_valid(fs->entries, fs->data_end_lba);
    }
}

int chfs_lookup(chos_chfs_t *fs, const char *path, chos_chfs_entry_t *out) {
    size_t i, capacity;
    if (fs == NULL || !fs->mounted || out == NULL || !path_valid(path)) return 0;
    capacity = entry_capacity(fs);
    for (i = 0u; i < capacity; ++i) {
        const chos_chfs_entry_t *entry = &fs->entries[i];
        if (entry->path[0] != '\0' && path_equal(entry->path, path) &&
            !chfs_entry_is_directory(entry)) {
            bytes_copy(out, entry, sizeof(*out)); return 1;
        }
    }
    return 0;
}

int chfs_is_directory(chos_chfs_t *fs, const char *path) {
    size_t i, length, capacity;
    if (fs == NULL || !fs->mounted || path == NULL) return 0;
    if (path[0] == '/' && path[1] == '\0') return 1;
    if (!path_valid(path)) return 0;
    capacity = entry_capacity(fs);
    for (i = 0u; i < capacity; ++i) {
        const chos_chfs_entry_t *entry = &fs->entries[i];
        if (entry->path[0] == '\0') continue;
        if (path_equal(entry->path, path) && chfs_entry_is_directory(entry)) return 1;
        length = 0u; while (path[length] != '\0') ++length;
        if (path_prefix_component(path, length, entry->path)) return 1; /* legacy implicit directory */
    }
    return 0;
}

static int path_parent(const char *path, char parent[CHOS_CHFS_PATH_MAX]) {
    size_t length = 0u, slash = 0u, i;
    int found = 0;
    while (path[length] != '\0') { if (path[length] == '/') { slash = length; found = 1; } ++length; }
    if (!found) return 0;
    if (slash == 0u) { parent[0] = '/'; parent[1] = '\0'; return 1; }
    for (i = 0u; i < slash; ++i) parent[i] = path[i];
    parent[slash] = '\0';
    return 1;
}

static size_t entry_capacity(const chos_chfs_t *fs) {
    if (fs == NULL) return 0u;
    if (fs->version == CHOS_CHFS_V1_VERSION) return CHOS_CHFS_V1_MAX_FILES;
    if (fs->version == CHOS_CHFS_V2_VERSION) return CHOS_CHFS_V2_MAX_FILES;
    return CHOS_CHFS_MAX_FILES;
}

int chfs_make_directory(chos_chfs_t *fs, const char *path) {
    chos_chfs_entry_t *next = chfs_operation_entries;
    char parent[CHOS_CHFS_PATH_MAX];
    size_t i, capacity, free_index;
    if (fs == NULL || !fs->mounted || !path_valid(path) || !path_parent(path, parent)) return 0;
    capacity = entry_capacity(fs); free_index = capacity;
    for (i = 0u; i < capacity; ++i) {
        if (fs->entries[i].path[0] == '\0') { if (free_index == capacity) free_index = i; }
        else if (path_equal(fs->entries[i].path, path)) return chfs_entry_is_directory(&fs->entries[i]);
    }
    if (!chfs_is_directory(fs, parent) || free_index == capacity) return 0;
    bytes_copy(next, fs->entries, sizeof(chfs_operation_entries));
    bytes_zero(&next[free_index], sizeof(next[free_index]));
    for (i = 0u; path[i] != '\0'; ++i) next[free_index].path[i] = path[i];
    next[free_index].crc32 = CHOS_CHFS_ENTRY_DIRECTORY_TAG;
    if (!entries_valid(next, fs->data_end_lba)) return 0;
    return commit_entries(fs, next);
}

int chfs_remove_directory(chos_chfs_t *fs, const char *path) {
    chos_chfs_entry_t *next = chfs_operation_entries;
    size_t i, length, capacity;
    if (fs == NULL || !fs->mounted || !path_valid(path)) return 0;
    capacity = entry_capacity(fs);
    length = 0u; while (path[length] != '\0') ++length;
    for (i = 0u; i < capacity; ++i) {
        if (fs->entries[i].path[0] != '\0' && path_prefix_component(path, length, fs->entries[i].path)) return 0;
    }
    bytes_copy(next, fs->entries, sizeof(chfs_operation_entries));
    for (i = 0u; i < capacity; ++i) {
        if (next[i].path[0] != '\0' && path_equal(next[i].path, path) && chfs_entry_is_directory(&next[i])) {
            bytes_zero(&next[i], sizeof(next[i]));
            return commit_entries(fs, next);
        }
    }
    return 0;
}

int chfs_sync(chos_chfs_t *fs) {
    return fs != NULL && fs->mounted && block_flush(fs->device);
}

int chfs_read_file(chos_chfs_t *fs, const char *path, void *buffer, size_t capacity, size_t *bytes_read) {
    chos_chfs_entry_t e; uint8_t sector[CHOS_BLOCK_SECTOR_SIZE]; uint32_t remain, idx = 0u, crc; size_t copied = 0u;
    uint8_t *out = (uint8_t *)buffer;
    if (bytes_read != NULL) *bytes_read = 0u;
    if (fs == NULL || !fs->mounted || !chfs_lookup(fs, path, &e) || capacity < e.size_bytes ||
        (e.size_bytes != 0u && buffer == NULL)) return 0;
    if (e.size_bytes == 0u) {
        if (bytes_read != NULL) *bytes_read = 0u;
        return e.crc32 == chfs_crc32(NULL, 0u);
    }
    remain = e.size_bytes;
    while (remain != 0u) {
        size_t n = remain > CHOS_BLOCK_SECTOR_SIZE ? CHOS_BLOCK_SECTOR_SIZE : remain;
        if (!block_read(fs->device, (uint64_t)e.first_sector + idx, 1u, sector)) return 0;
        bytes_copy(out + copied, sector, n); copied += n; remain -= (uint32_t)n; ++idx;
    }
    crc = chfs_crc32(buffer, e.size_bytes);
    if (crc != e.crc32) return 0;
    if (bytes_read != NULL) *bytes_read = e.size_bytes;
    return 1;
}
static int extent_free(const chos_chfs_entry_t *entries, size_t entry_count,
                       uint32_t start, uint32_t count) {
    size_t i; uint64_t end = (uint64_t)start + count;
    for (i = 0u; i < entry_count; ++i) {
        const chos_chfs_entry_t *e = &entries[i]; uint64_t ee;
        if (e->path[0] == '\0') continue;
        ee = (uint64_t)e->first_sector + e->sector_count;
        if ((uint64_t)start < ee && (uint64_t)e->first_sector < end) return 0;
    }
    return 1;
}
/* Protect extents referenced by the inactive metadata generation until the next
 * directory commit succeeds. Reusing one prematurely would leave a valid backup
 * directory pointing at data already overwritten by an uncommitted write. */
static int allocate_extent_preserving_backup_from(const chos_chfs_t *fs,
                                                   const chos_chfs_entry_t *entries,
                                                   size_t entry_count, uint32_t first_candidate,
                                                   uint32_t count, uint32_t *start) {
    uint8_t *raw = chfs_allocator_raw;
    chos_chfs_entry_t *backup = chfs_allocator_backup;
    uint32_t generation = 0u, backup_lba;
    size_t backup_count = 0u;
    uint64_t candidate;
    if (fs == NULL || count == 0u || start == NULL) return count == 0u;
    backup_lba = fs->active_directory_lba == fs->directory_a_lba ?
        fs->directory_b_lba : fs->directory_a_lba;
    if (block_read(fs->device, backup_lba, fs->directory_sectors, raw)) {
        if (fs->version == CHOS_CHFS_V1_VERSION) {
            if (decode_directory_v1(raw, &generation, backup) && entries_valid(backup, fs->total_sectors))
                backup_count = CHOS_CHFS_V1_MAX_FILES;
        } else if (fs->version == CHOS_CHFS_V2_VERSION) {
            if (decode_directory_v2(raw, &generation, backup) && entries_valid(backup, fs->data_end_lba))
                backup_count = CHOS_CHFS_V2_MAX_FILES;
        } else if (decode_directory(raw, &generation, backup) && entries_valid(backup, fs->data_end_lba)) {
            backup_count = CHOS_CHFS_MAX_FILES;
        }
    }
    if (first_candidate < fs->data_start_lba) first_candidate = fs->data_start_lba;
    for (candidate = first_candidate; candidate + count <= fs->data_end_lba; ++candidate) {
        if (!extent_free(entries, entry_count, (uint32_t)candidate, count)) continue;
        if (backup_count != 0u && !extent_free(backup, backup_count, (uint32_t)candidate, count)) continue;
        *start = (uint32_t)candidate;
        return 1;
    }
    return 0;
}
static int allocate_extent_preserving_backup(const chos_chfs_t *fs,
                                             const chos_chfs_entry_t *entries,
                                             size_t entry_count, uint32_t count,
                                             uint32_t *start) {
    return allocate_extent_preserving_backup_from(fs, entries, entry_count,
                                                  fs != NULL ? fs->data_start_lba : 0u,
                                                  count, start);
}
static int commit_entries(chos_chfs_t *fs, const chos_chfs_entry_t *entries) {
    uint8_t *raw = chfs_allocator_raw;
    uint32_t target = fs->active_directory_lba == fs->directory_a_lba ?
        fs->directory_b_lba : fs->directory_a_lba;
    uint32_t generation = fs->generation + 1u;
    if (fs->version == CHOS_CHFS_V1_VERSION) encode_directory_v1(raw, entries, generation);
    else if (fs->version == CHOS_CHFS_V2_VERSION) encode_directory_v2(raw, entries, generation);
    else encode_directory(raw, entries, generation);
    if (!block_write(fs->device, target, fs->directory_sectors, raw) || !block_flush(fs->device)) return 0;
    bytes_copy(fs->entries, entries, sizeof(fs->entries));
    fs->generation = generation; fs->active_directory_lba = target;
    return 1;
}

static int allocate_migration_extent(const chos_chfs_t *fs, uint32_t limit,
                                     uint32_t count, uint32_t *start) {
    uint64_t candidate;
    size_t capacity = entry_capacity(fs);
    for (candidate = fs->data_start_lba; candidate + count <= limit; ++candidate) {
        if (extent_free(fs->entries, capacity, (uint32_t)candidate, count)) {
            *start = (uint32_t)candidate;
            return 1;
        }
    }
    return 0;
}

/* Upgrade v1 to the stable v2 tail layout first. Keeping this migration separate
 * lets v2 metadata remain valid while the newer front-metadata layout is built. */
static int chfs_migrate_v1(chos_chfs_t *fs) {
    uint8_t sector[CHOS_BLOCK_SECTOR_SIZE];
    uint8_t *directory = chfs_allocator_raw;
    uint32_t tail_a, tail_b;
    size_t i;
    if (fs == NULL || fs->version != CHOS_CHFS_V1_VERSION ||
        fs->total_sectors < CHFS_V2_VOLUME_MIN_SECTORS) return 0;
    tail_a = (uint32_t)fs->total_sectors - 2u * CHOS_CHFS_V2_DIRECTORY_SECTORS;
    tail_b = (uint32_t)fs->total_sectors - CHOS_CHFS_V2_DIRECTORY_SECTORS;
    if (tail_a <= fs->data_start_lba) return 0;

    /* Normalize both legacy metadata copies to the selected, valid generation. */
    if (!commit_entries(fs, fs->entries) || !commit_entries(fs, fs->entries)) return 0;

    for (i = 0u; i < CHOS_CHFS_V1_MAX_FILES; ++i) {
        const chos_chfs_entry_t *entry = &fs->entries[i];
        uint64_t end;
        uint32_t destination, sector_index, old_start;
        if (entry->path[0] == '\0' || chfs_entry_is_directory(entry) || entry->sector_count == 0u) continue;
        end = (uint64_t)entry->first_sector + entry->sector_count;
        if (end <= tail_a) continue;
        old_start = entry->first_sector;
        if (!allocate_migration_extent(fs, tail_a, entry->sector_count, &destination)) return 0;
        for (sector_index = 0u; sector_index < entry->sector_count; ++sector_index) {
            if (!block_read(fs->device, (uint64_t)old_start + sector_index, 1u, sector) ||
                !block_write(fs->device, (uint64_t)destination + sector_index, 1u, sector)) return 0;
        }
        if (!block_flush(fs->device)) return 0;
        fs->entries[i].first_sector = destination;
        if (!entries_valid(fs->entries, fs->total_sectors)) {
            fs->entries[i].first_sector = old_start;
            return 0;
        }
        if (!commit_entries(fs, fs->entries)) {
            fs->entries[i].first_sector = old_start;
            return 0;
        }
        /* A second durable v1 commit also removes the old extent from the backup
         * generation before the metadata tail is reserved by v2. */
        if (!commit_entries(fs, fs->entries)) return 0;
    }

    for (i = 0u; i < CHOS_CHFS_V1_MAX_FILES; ++i) {
        const chos_chfs_entry_t *entry = &fs->entries[i];
        if (entry->path[0] != '\0' && entry->sector_count != 0u &&
            (uint64_t)entry->first_sector + entry->sector_count > tail_a) return 0;
    }

    /* Keep v1 directories at LBAs 1 and 4 intact. Write and flush both new v2
     * copies first; reuse the scratch buffer for the superblock, and switch only
     * after the v2 directory copies are durable. */
    encode_directory_v2(directory, fs->entries, fs->generation + 1u);
    if (!block_write(fs->device, tail_a, CHOS_CHFS_V2_DIRECTORY_SECTORS, directory)) return 0;
    encode_directory_v2(directory, fs->entries, fs->generation);
    if (!block_write(fs->device, tail_b, CHOS_CHFS_V2_DIRECTORY_SECTORS, directory) || !block_flush(fs->device) ||
        !make_superblock_version(directory, CHOS_CHFS_V2_VERSION, fs->total_sectors, tail_a, tail_b,
                                 CHOS_CHFS_V2_DIRECTORY_SECTORS, CHOS_CHFS_V1_DATA_LBA) ||
        !block_write(fs->device, 0u, 1u, directory) || !block_flush(fs->device)) return 0;

    fs->version = CHOS_CHFS_V2_VERSION;
    fs->directory_a_lba = tail_a; fs->directory_b_lba = tail_b;
    fs->directory_sectors = CHOS_CHFS_V2_DIRECTORY_SECTORS;
    fs->data_end_lba = tail_a;
    fs->active_directory_lba = tail_a;
    ++fs->generation;
    return 1;
}

/* V2 stored metadata in the last 16 sectors and permitted data from LBA 7.
 * V3 reserves LBAs 1..128 for two 64-sector catalogues and starts data at 129.
 * Existing V2 metadata remains untouched until relocated files and both V3
 * catalogues are durable; the V3 superblock is the final commit. If contiguous
 * free space is insufficient, the volume stays mounted as V2. */
static int chfs_migrate_v2(chos_chfs_t *fs) {
    uint8_t sector[CHOS_BLOCK_SECTOR_SIZE];
    uint8_t *directory = chfs_allocator_raw;
    size_t i;
    uint32_t new_data_start = CHOS_CHFS_V3_DATA_LBA;
    if (fs == NULL || fs->version != CHOS_CHFS_V2_VERSION ||
        fs->total_sectors <= new_data_start) return 0;

    if (!commit_entries(fs, fs->entries) || !commit_entries(fs, fs->entries)) return 0;

    for (i = 0u; i < CHOS_CHFS_V2_MAX_FILES; ++i) {
        const chos_chfs_entry_t *entry = &fs->entries[i];
        uint32_t old_start, destination, sector_index;
        if (entry->path[0] == '\0' || chfs_entry_is_directory(entry) || entry->sector_count == 0u ||
            entry->first_sector >= new_data_start) continue;
        old_start = entry->first_sector;
        if (!allocate_extent_preserving_backup_from(fs, fs->entries, CHOS_CHFS_V2_MAX_FILES,
                                                    new_data_start, entry->sector_count, &destination)) return 0;
        for (sector_index = 0u; sector_index < entry->sector_count; ++sector_index) {
            if (!block_read(fs->device, (uint64_t)old_start + sector_index, 1u, sector) ||
                !block_write(fs->device, (uint64_t)destination + sector_index, 1u, sector)) return 0;
        }
        if (!block_flush(fs->device)) return 0;
        fs->entries[i].first_sector = destination;
        if (!entries_valid(fs->entries, fs->data_end_lba)) {
            fs->entries[i].first_sector = old_start;
            return 0;
        }
        if (!commit_entries(fs, fs->entries)) {
            fs->entries[i].first_sector = old_start;
            return 0;
        }
        /* If the second metadata commit fails, keep the in-memory catalogue
         * aligned with the first durable generation, which already points at
         * the copied data. The old extent remains intact for recovery. */
        if (!commit_entries(fs, fs->entries)) return 0;
    }
    for (i = 0u; i < CHOS_CHFS_V2_MAX_FILES; ++i) {
        const chos_chfs_entry_t *entry = &fs->entries[i];
        if (entry->path[0] != '\0' && entry->sector_count != 0u && entry->first_sector < new_data_start)
            return 0;
    }

    encode_directory(directory, fs->entries, fs->generation + 1u);
    if (!block_write(fs->device, CHOS_CHFS_V3_DIR_A_LBA, CHOS_CHFS_DIRECTORY_SECTORS, directory)) return 0;
    encode_directory(directory, fs->entries, fs->generation);
    if (!block_write(fs->device, CHOS_CHFS_V3_DIR_B_LBA, CHOS_CHFS_DIRECTORY_SECTORS, directory) ||
        !block_flush(fs->device) ||
        !make_superblock(directory, fs->total_sectors, CHOS_CHFS_V3_DIR_A_LBA, CHOS_CHFS_V3_DIR_B_LBA,
                         CHOS_CHFS_DIRECTORY_SECTORS, new_data_start) ||
        !block_write(fs->device, 0u, 1u, directory) || !block_flush(fs->device)) return 0;

    fs->version = CHOS_CHFS_VERSION;
    fs->directory_a_lba = CHOS_CHFS_V3_DIR_A_LBA;
    fs->directory_b_lba = CHOS_CHFS_V3_DIR_B_LBA;
    fs->directory_sectors = CHOS_CHFS_DIRECTORY_SECTORS;
    fs->data_start_lba = new_data_start;
    fs->data_end_lba = (uint32_t)fs->total_sectors;
    fs->active_directory_lba = CHOS_CHFS_V3_DIR_A_LBA;
    ++fs->generation;
    return 1;
}

int chfs_write_file(chos_chfs_t *fs, const char *path, const void *data, size_t size) {
    chos_chfs_entry_t *next = chfs_operation_entries;
    char parent[CHOS_CHFS_PATH_MAX];
    uint32_t sectors, start = 0u, remain, idx = 0u;
    size_t i, capacity, free_index, offset = 0u; uint8_t sector[CHOS_BLOCK_SECTOR_SIZE];
    const uint8_t *input = (const uint8_t *)data;
    if (fs == NULL || !fs->mounted || !path_valid(path) || (data == NULL && size != 0u) ||
        size > CHOS_CHFS_FILE_MAX_BYTES || size > UINT32_MAX || !path_parent(path, parent) ||
        !chfs_is_directory(fs, parent)) return 0;
    capacity = entry_capacity(fs); free_index = capacity;
    bytes_copy(next, fs->entries, sizeof(chfs_operation_entries));
    for (i = 0u; i < capacity; ++i) {
        if (next[i].path[0] == '\0') { if (free_index == capacity) free_index = i; }
        else if (path_equal(next[i].path, path)) {
            if (chfs_entry_is_directory(&next[i])) return 0;
            free_index = i;
        }
    }
    if (free_index == capacity) return 0;
    bytes_zero(&next[free_index], sizeof(next[free_index]));
    sectors = (uint32_t)((size + CHOS_BLOCK_SECTOR_SIZE - 1u) / CHOS_BLOCK_SECTOR_SIZE);
    if (sectors != 0u && !allocate_extent_preserving_backup(fs, fs->entries, capacity, sectors, &start)) return 0;
    remain = (uint32_t)size;
    while (remain != 0u) {
        size_t n = remain > CHOS_BLOCK_SECTOR_SIZE ? CHOS_BLOCK_SECTOR_SIZE : remain;
        bytes_zero(sector, sizeof(sector)); bytes_copy(sector, input + offset, n);
        if (!block_write(fs->device, (uint64_t)start + idx, 1u, sector)) return 0;
        offset += n; remain -= (uint32_t)n; ++idx;
    }
    if (!block_flush(fs->device)) return 0;
    bytes_zero(&next[free_index], sizeof(next[free_index]));
    for (i = 0u; path[i] != '\0'; ++i) next[free_index].path[i] = path[i];
    next[free_index].first_sector = start; next[free_index].sector_count = sectors;
    next[free_index].size_bytes = (uint32_t)size; next[free_index].crc32 = chfs_crc32(data, size);
    if (!entries_valid(next, fs->data_end_lba)) return 0;
    return commit_entries(fs, next);
}
int chfs_remove_file(chos_chfs_t *fs, const char *path) {
    chos_chfs_entry_t *next = chfs_operation_entries; size_t i, capacity;
    if (fs == NULL || !fs->mounted || !path_valid(path)) return 0;
    capacity = entry_capacity(fs);
    bytes_copy(next, fs->entries, sizeof(chfs_operation_entries));
    for (i = 0u; i < capacity; ++i) if (next[i].path[0] != '\0' && path_equal(next[i].path, path) &&
        !chfs_entry_is_directory(&next[i])) {
        bytes_zero(&next[i], sizeof(next[i])); return commit_entries(fs, next);
    }
    return 0;
}

int chfs_rename_file(chos_chfs_t *fs, const char *old_path, const char *new_path) {
    chos_chfs_entry_t *next = chfs_operation_entries;
    char parent[CHOS_CHFS_PATH_MAX];
    size_t i, capacity, old_index;
    if (fs == NULL || !fs->mounted || !path_valid(old_path) || !path_valid(new_path) ||
        !path_parent(new_path, parent) || !chfs_is_directory(fs, parent)) return 0;
    capacity = entry_capacity(fs); old_index = capacity;
    for (i = 0u; i < capacity; ++i) {
        if (fs->entries[i].path[0] == '\0') continue;
        if (path_equal(fs->entries[i].path, old_path)) {
            if (chfs_entry_is_directory(&fs->entries[i])) return 0;
            old_index = i;
            break;
        }
    }
    if (old_index == capacity) return 0;
    if (path_equal(old_path, new_path)) return 1;
    if (chfs_is_directory(fs, new_path)) return 0;
    for (i = 0u; i < capacity; ++i)
        if (fs->entries[i].path[0] != '\0' && path_equal(fs->entries[i].path, new_path)) return 0;
    bytes_copy(next, fs->entries, sizeof(chfs_operation_entries));
    bytes_zero(next[old_index].path, sizeof(next[old_index].path));
    for (i = 0u; new_path[i] != '\0'; ++i) next[old_index].path[i] = new_path[i];
    if (!entries_valid(next, fs->data_end_lba)) return 0;
    return commit_entries(fs, next);
}

int chfs_rename_directory(chos_chfs_t *fs, const char *old_path, const char *new_path) {
    chos_chfs_entry_t *next = chfs_operation_entries;
    char parent[CHOS_CHFS_PATH_MAX];
    size_t old_length = 0u, new_length = 0u, i, j, capacity;
    int found_source = 0;
    if (fs == NULL || !fs->mounted || !path_valid(old_path) || !path_valid(new_path) ||
        path_equal(old_path, "/") || !path_parent(new_path, parent) ||
        !chfs_is_directory(fs, parent)) return 0;
    while (old_path[old_length] != '\0') ++old_length;
    while (new_path[new_length] != '\0') ++new_length;
    if (path_equal(old_path, new_path)) return chfs_is_directory(fs, old_path);
    capacity = entry_capacity(fs);
    /* A directory may not be moved into itself or one of its descendants. */
    if (path_prefix_component(old_path, old_length, new_path)) return 0;
    for (i = 0u; i < capacity; ++i) {
        const chos_chfs_entry_t *entry = &fs->entries[i];
        if (entry->path[0] == '\0') continue;
        if (path_equal(entry->path, old_path)) {
            if (!chfs_entry_is_directory(entry)) return 0;
            found_source = 1;
        }
        if (path_equal(entry->path, new_path) || path_prefix_component(new_path, new_length, entry->path)) {
            /* Existing destination names are allowed only if they belong to the
             * source subtree itself (the no-op case was handled above). */
            if (!path_prefix_component(old_path, old_length, entry->path) &&
                !path_equal(entry->path, old_path)) return 0;
        }
    }
    if (!found_source) return 0;
    bytes_copy(next, fs->entries, sizeof(chfs_operation_entries));
    for (i = 0u; i < capacity; ++i) {
        chos_chfs_entry_t *entry = &next[i];
        size_t entry_length = 0u;
        if (entry->path[0] == '\0') continue;
        while (entry->path[entry_length] != '\0') ++entry_length;
        if (!path_equal(entry->path, old_path) && !path_prefix_component(old_path, old_length, entry->path)) continue;
        if (entry_length < old_length || new_length + (entry_length - old_length) >= CHOS_CHFS_PATH_MAX) return 0;
        {
            char replaced[CHOS_CHFS_PATH_MAX];
            for (j = 0u; j < new_length; ++j) replaced[j] = new_path[j];
            for (j = old_length; j < entry_length; ++j) replaced[new_length + j - old_length] = entry->path[j];
            replaced[new_length + entry_length - old_length] = '\0';
            for (j = 0u; j < CHOS_CHFS_PATH_MAX; ++j) entry->path[j] = replaced[j];
        }
    }
    if (!entries_valid(next, fs->data_end_lba)) return 0;
    return commit_entries(fs, next);
}

int chfs_get_at(chos_chfs_t *fs, size_t index, chos_chfs_entry_t *out) {
    size_t i, seen = 0u, capacity;
    if (fs == NULL || !fs->mounted || out == NULL) return 0;
    capacity = entry_capacity(fs);
    for (i = 0u; i < capacity; ++i) if (fs->entries[i].path[0] != '\0') {
        if (seen++ == index) { bytes_copy(out, &fs->entries[i], sizeof(*out)); return 1; }
    }
    return 0;
}
void chfs_get_stats(chos_chfs_t *fs, chos_chfs_stats_t *out) {
    size_t i, capacity; uint64_t used = 0u;
    if (fs == NULL || out == NULL) return;
    bytes_zero(out, sizeof(*out)); if (!fs->mounted) return;
    out->generation = fs->generation;
    out->capacity_bytes = fs->total_sectors * CHOS_BLOCK_SECTOR_SIZE;
    capacity = entry_capacity(fs);
    for (i = 0u; i < capacity; ++i) {
        if (fs->entries[i].path[0] == '\0') continue;
        if (chfs_entry_is_directory(&fs->entries[i])) ++out->directory_count;
        else ++out->file_count;
        used += fs->entries[i].sector_count;
    }
    out->used_sectors = (size_t)used;
    out->free_sectors = (size_t)((fs->data_end_lba - fs->data_start_lba) > used ?
        (fs->data_end_lba - fs->data_start_lba) - used : 0u);
}
