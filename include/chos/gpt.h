#ifndef CHOS_GPT_H
#define CHOS_GPT_H

#include <stddef.h>
#include <stdint.h>
#include "chos/blockdev.h"

#define CHOS_GPT_MAX_PARTITIONS 128u
#define CHOS_GPT_NAME_MAX 37u
#define CHOS_GPT_ENTRY_MIN_SIZE 128u
#define CHOS_GPT_ENTRY_MAX_SIZE 512u
#define CHOS_GPT_ARRAY_MAX_BYTES (CHOS_GPT_MAX_PARTITIONS * CHOS_GPT_ENTRY_MAX_SIZE)

/* Type GUID bytes are stored in GPT's on-disk byte order (little-endian for
 * the first three UUID fields). This is the CHOS data partition type.
 * UUID text: 0f2ac3ac-2af2-4e10-a8b3-4d41c0a0260e */
extern const uint8_t chos_gpt_chos_data_type_guid[16];
extern const uint8_t chos_gpt_efi_system_type_guid[16];

typedef struct {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    char name[CHOS_GPT_NAME_MAX];
} chos_gpt_partition_t;

typedef enum {
    CHOS_GPT_OK = 0,
    CHOS_GPT_ERR_ARGUMENT,
    CHOS_GPT_ERR_IO,
    CHOS_GPT_ERR_MBR,
    CHOS_GPT_ERR_SIGNATURE,
    CHOS_GPT_ERR_HEADER,
    CHOS_GPT_ERR_HEADER_CRC,
    CHOS_GPT_ERR_ENTRIES,
    CHOS_GPT_ERR_ENTRIES_CRC,
    CHOS_GPT_ERR_PARTITION_RANGE,
    CHOS_GPT_ERR_OVERLAP,
    CHOS_GPT_ERR_CAPACITY
} chos_gpt_error_t;

/* Read-only GPT parser; never changes the disk. The entry-array workspace is
 * static, bounded, and intentionally non-reentrant for early boot use. Output
 * capacity limits the number of partitions returned; invalid unreturned
 * entries are still checked for basic range validity. */
chos_gpt_error_t chos_gpt_read(chos_block_device_t *disk,
                               chos_gpt_partition_t *partitions,
                               size_t capacity,
                               size_t *count_out);
const char *chos_gpt_error_string(chos_gpt_error_t error);
int chos_gpt_guid_equal(const uint8_t left[16], const uint8_t right[16]);

#endif
