#include "chos/gpt.h"
#include <stddef.h>

#define GPT_HEADER_MIN_SIZE 92u
#define GPT_SIGNATURE "EFI PART"
#define GPT_MBR_SIGNATURE0 0x55u
#define GPT_MBR_SIGNATURE1 0xaau
#define GPT_HEADER_CRC_OFFSET 16u
#define GPT_HEADER_CRC_SIZE 4u
#define GPT_MAX_ENTRIES_BYTES CHOS_GPT_ARRAY_MAX_BYTES

const uint8_t chos_gpt_chos_data_type_guid[16] = {
    0xac,0xc3,0x2a,0x0f,0xf2,0x2a,0x10,0x4e,0xa8,0xb3,0x4d,0x41,0xc0,0xa0,0x26,0x0e
};
const uint8_t chos_gpt_efi_system_type_guid[16] = {
    0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b
};

/* One-sector scratch and a capped entry-array buffer keep stack use bounded.
 * The GPT parser is only used during single-threaded storage discovery. */
static uint8_t gpt_header_sector[CHOS_BLOCK_SECTOR_SIZE];
static uint8_t gpt_entries_scratch[GPT_MAX_ENTRIES_BYTES];

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}
static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t length) {
    size_t i; unsigned bit;
    for (i = 0; i < length; ++i) {
        crc ^= p[i];
        for (bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    return crc;
}
static uint32_t crc32_bytes(const uint8_t *p, size_t length) {
    return ~crc32_update(UINT32_C(0xffffffff), p, length);
}
static int all_zero(const uint8_t *p, size_t n) {
    size_t i; for (i = 0u; i < n; ++i) if (p[i] != 0u) return 0; return 1;
}
int chos_gpt_guid_equal(const uint8_t left[16], const uint8_t right[16]) {
    size_t i;
    if (left == NULL || right == NULL) return 0;
    for (i = 0u; i < 16u; ++i) if (left[i] != right[i]) return 0;
    return 1;
}
static int ranges_overlap(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) {
    return a0 <= b1 && b0 <= a1;
}
static int entry_array_read(chos_block_device_t *disk, uint64_t first_lba,
                            uint32_t entries_bytes, uint8_t *out) {
    uint32_t copied = 0u;
    uint8_t sector[CHOS_BLOCK_SECTOR_SIZE];
    while (copied < entries_bytes) {
        uint64_t lba_delta = copied / CHOS_BLOCK_SECTOR_SIZE;
        uint32_t in_sector = copied % CHOS_BLOCK_SECTOR_SIZE;
        uint32_t take = CHOS_BLOCK_SECTOR_SIZE - in_sector;
        if (take > entries_bytes - copied) take = entries_bytes - copied;
        if (first_lba > UINT64_MAX - lba_delta ||
            !block_read(disk, first_lba + lba_delta, 1u, sector)) return 0;
        {
            uint32_t i;
            for (i = 0u; i < take; ++i) out[copied + i] = sector[in_sector + i];
        }
        copied += take;
    }
    return 1;
}
static void copy_name(char out[CHOS_GPT_NAME_MAX], const uint8_t *entry) {
    size_t i;
    for (i = 0u; i < CHOS_GPT_NAME_MAX - 1u; ++i) {
        uint16_t ch = get16(entry + 56u + i * 2u);
        if (ch == 0u) break;
        out[i] = (ch >= 32u && ch <= 126u) ? (char)ch : '?';
    }
    out[i] = '\0';
}
static void decode_partition(chos_gpt_partition_t *out, const uint8_t *entry) {
    size_t i;
    for (i = 0u; i < 16u; ++i) { out->type_guid[i] = entry[i]; out->unique_guid[i] = entry[16u + i]; }
    out->first_lba = get64(entry + 32u);
    out->last_lba = get64(entry + 40u);
    out->attributes = get64(entry + 48u);
    copy_name(out->name, entry);
}
chos_gpt_error_t chos_gpt_read(chos_block_device_t *disk,
                               chos_gpt_partition_t *partitions,
                               size_t capacity,
                               size_t *count_out) {
    uint8_t mbr[CHOS_BLOCK_SECTOR_SIZE];
    uint8_t header_copy[CHOS_BLOCK_SECTOR_SIZE];
    uint64_t current_lba, backup_lba, first_usable, last_usable, entries_lba;
    uint32_t header_size, header_crc, entries_crc, entry_count, entry_size, array_bytes;
    uint32_t calculated_crc, i;
    size_t stored = 0u;
    chos_gpt_error_t result = CHOS_GPT_OK;
    if (count_out != NULL) *count_out = 0u;
    if (disk == NULL || !disk->ready || disk->read == NULL || disk->sector_count < 3u ||
        (capacity != 0u && partitions == NULL)) return CHOS_GPT_ERR_ARGUMENT;
    if (!block_read(disk, 0u, 1u, mbr)) return CHOS_GPT_ERR_IO;
    if (mbr[510] != GPT_MBR_SIGNATURE0 || mbr[511] != GPT_MBR_SIGNATURE1 ||
        mbr[450] != 0xeeu || get32(mbr + 454u) != 1u) return CHOS_GPT_ERR_MBR;
    if (!block_read(disk, 1u, 1u, gpt_header_sector)) return CHOS_GPT_ERR_IO;
    if (gpt_header_sector[0] != 'E' || gpt_header_sector[1] != 'F' ||
        gpt_header_sector[2] != 'I' || gpt_header_sector[3] != ' ' ||
        gpt_header_sector[4] != 'P' || gpt_header_sector[5] != 'A' ||
        gpt_header_sector[6] != 'R' || gpt_header_sector[7] != 'T') return CHOS_GPT_ERR_SIGNATURE;
    header_size = get32(gpt_header_sector + 12u);
    if (header_size < GPT_HEADER_MIN_SIZE || header_size > CHOS_BLOCK_SECTOR_SIZE) return CHOS_GPT_ERR_HEADER;
    header_crc = get32(gpt_header_sector + GPT_HEADER_CRC_OFFSET);
    for (i = 0u; i < CHOS_BLOCK_SECTOR_SIZE; ++i) header_copy[i] = gpt_header_sector[i];
    put32(header_copy + GPT_HEADER_CRC_OFFSET, 0u);
    calculated_crc = crc32_bytes(header_copy, header_size);
    if (calculated_crc != header_crc) return CHOS_GPT_ERR_HEADER_CRC;
    current_lba = get64(gpt_header_sector + 24u);
    backup_lba = get64(gpt_header_sector + 32u);
    first_usable = get64(gpt_header_sector + 40u);
    last_usable = get64(gpt_header_sector + 48u);
    entries_lba = get64(gpt_header_sector + 72u);
    entry_count = get32(gpt_header_sector + 80u);
    entry_size = get32(gpt_header_sector + 84u);
    entries_crc = get32(gpt_header_sector + 88u);
    if (current_lba != 1u || backup_lba >= disk->sector_count || backup_lba == current_lba ||
        first_usable > last_usable || last_usable >= disk->sector_count ||
        entry_count == 0u || entry_count > CHOS_GPT_MAX_PARTITIONS ||
        entry_size < CHOS_GPT_ENTRY_MIN_SIZE || entry_size > CHOS_GPT_ENTRY_MAX_SIZE ||
        (entry_size & 7u) != 0u || entry_count > UINT32_MAX / entry_size) return CHOS_GPT_ERR_HEADER;
    array_bytes = entry_count * entry_size;
    {
        uint64_t entry_sectors = ((uint64_t)array_bytes + CHOS_BLOCK_SECTOR_SIZE - 1u) /
                                 CHOS_BLOCK_SECTOR_SIZE;
        if (array_bytes == 0u || array_bytes > GPT_MAX_ENTRIES_BYTES || entries_lba < 2u ||
            entries_lba >= disk->sector_count || entry_sectors > disk->sector_count - entries_lba ||
            entries_lba + entry_sectors > first_usable || entries_lba + entry_sectors > backup_lba)
            return CHOS_GPT_ERR_ENTRIES;
    }
    if (!entry_array_read(disk, entries_lba, array_bytes, gpt_entries_scratch)) return CHOS_GPT_ERR_IO;
    calculated_crc = crc32_bytes(gpt_entries_scratch, array_bytes);
    if (calculated_crc != entries_crc) return CHOS_GPT_ERR_ENTRIES_CRC;
    for (i = 0u; i < entry_count; ++i) {
        const uint8_t *entry = gpt_entries_scratch + (size_t)i * entry_size;
        chos_gpt_partition_t decoded;
        size_t j;
        if (all_zero(entry, 16u)) continue;
        decode_partition(&decoded, entry);
        if (decoded.first_lba > decoded.last_lba || decoded.first_lba < first_usable ||
            decoded.last_lba > last_usable || decoded.last_lba >= disk->sector_count) {
            result = CHOS_GPT_ERR_PARTITION_RANGE; goto done;
        }
        for (j = 0u; j < stored; ++j) {
            if (ranges_overlap(decoded.first_lba, decoded.last_lba,
                               partitions[j].first_lba, partitions[j].last_lba)) {
                result = CHOS_GPT_ERR_OVERLAP; goto done;
            }
        }
        if (stored < capacity) partitions[stored++] = decoded;
        else { result = CHOS_GPT_ERR_CAPACITY; goto done; }
    }
done:
    if (count_out != NULL) *count_out = stored;
    return result;
}
const char *chos_gpt_error_string(chos_gpt_error_t error) {
    switch (error) {
        case CHOS_GPT_OK: return "ok";
        case CHOS_GPT_ERR_ARGUMENT: return "invalid argument or disk";
        case CHOS_GPT_ERR_IO: return "block read failed";
        case CHOS_GPT_ERR_MBR: return "protective MBR signature missing";
        case CHOS_GPT_ERR_SIGNATURE: return "GPT signature missing";
        case CHOS_GPT_ERR_HEADER: return "GPT header fields invalid";
        case CHOS_GPT_ERR_HEADER_CRC: return "GPT header CRC mismatch";
        case CHOS_GPT_ERR_ENTRIES: return "GPT entry array bounds invalid";
        case CHOS_GPT_ERR_ENTRIES_CRC: return "GPT entry array CRC mismatch";
        case CHOS_GPT_ERR_PARTITION_RANGE: return "partition outside usable range";
        case CHOS_GPT_ERR_OVERLAP: return "partition ranges overlap";
        case CHOS_GPT_ERR_CAPACITY: return "output partition capacity exceeded";
        default: return "unknown GPT error";
    }
}
