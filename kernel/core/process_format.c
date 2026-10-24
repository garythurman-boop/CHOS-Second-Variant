#include <stddef.h>
#include <stdint.h>
#include "chos/process.h"

#define CHOS_EXEC_MAGIC_0 ((uint8_t)'C')
#define CHOS_EXEC_MAGIC_1 ((uint8_t)'H')
#define CHOS_EXEC_MAGIC_2 ((uint8_t)'C')
#define CHOS_EXEC_MAGIC_3 ((uint8_t)'X')
#define CHOS_EXEC_PAGE_SIZE 4096u

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static void write_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static uint32_t align_up_u32(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}
static int add_u32(uint32_t a, uint32_t b, uint32_t *out) {
    if (UINT32_MAX - a < b) return 0;
    *out = a + b;
    return 1;
}
static void clear_info(chos_exec_image_info_t *out) {
    out->version = 0u; out->image_size = 0u; out->entry_offset = 0u;
    out->stack_pages = 0u; out->header_size = 0u; out->code_size = 0u;
    out->rodata_offset = 0u; out->rodata_size = 0u; out->data_offset = 0u;
    out->data_size = 0u; out->bss_offset = 0u; out->bss_size = 0u;
    out->virtual_size = 0u; out->image_offset = 0u;
}

int process_image_validate(const void *file_bytes, size_t file_size,
                           chos_exec_image_info_t *out) {
    const uint8_t *bytes = (const uint8_t *)file_bytes;
    uint16_t version, header_size, stack_pages, flags;
    uint32_t image_size, entry_offset, reserved;
    size_t total_size;

    if (out != NULL) clear_info(out);
    if (bytes == NULL || out == NULL || file_size < CHOS_EXEC_V1_HEADER_SIZE) return 0;
    if (bytes[0] != CHOS_EXEC_MAGIC_0 || bytes[1] != CHOS_EXEC_MAGIC_1 ||
        bytes[2] != CHOS_EXEC_MAGIC_2 || bytes[3] != CHOS_EXEC_MAGIC_3) return 0;
    version = read_le16(bytes + 4u);
    header_size = read_le16(bytes + 6u);
    image_size = read_le32(bytes + 8u);
    entry_offset = read_le32(bytes + 12u);
    stack_pages = read_le16(bytes + 16u);
    flags = read_le16(bytes + 18u);
    reserved = read_le32(bytes + 20u);
    if (flags != 0u || reserved != 0u) return 0;
    if (image_size == 0u || image_size > CHOS_EXEC_MAX_IMAGE_SIZE) return 0;
    if (stack_pages < CHOS_EXEC_MIN_STACK_PAGES || stack_pages > CHOS_EXEC_MAX_STACK_PAGES) return 0;

    if (version == CHOS_EXEC_VERSION) {
        if (header_size != CHOS_EXEC_V1_HEADER_SIZE || entry_offset >= image_size ||
            (entry_offset & 3u) != 0u || image_size - entry_offset < 4u) return 0;
        total_size = (size_t)header_size + (size_t)image_size;
        if (file_size != total_size) return 0;
        out->version = version;
        out->image_size = image_size;
        out->entry_offset = entry_offset;
        out->stack_pages = stack_pages;
        out->header_size = header_size;
        out->code_size = image_size;
        out->virtual_size = align_up_u32(image_size, CHOS_EXEC_PAGE_SIZE);
        out->rodata_offset = out->virtual_size;
        out->data_offset = out->virtual_size;
        out->bss_offset = out->virtual_size;
        out->image_offset = header_size;
        return 1;
    }

    if (version == CHOS_EXEC_VERSION_SEGMENTS) {
        uint32_t code_size, rodata_offset, rodata_size, data_offset, data_size;
        uint32_t bss_offset, bss_size, virtual_size, initialized_end, bss_end;
        if (header_size != CHOS_EXEC_V2_HEADER_SIZE || file_size < header_size) return 0;
        code_size = read_le32(bytes + 24u);
        rodata_offset = read_le32(bytes + 28u);
        rodata_size = read_le32(bytes + 32u);
        data_offset = read_le32(bytes + 36u);
        data_size = read_le32(bytes + 40u);
        bss_offset = read_le32(bytes + 44u);
        bss_size = read_le32(bytes + 48u);
        virtual_size = read_le32(bytes + 52u);
        if (virtual_size == 0u || virtual_size > CHOS_EXEC_MAX_IMAGE_SIZE ||
            code_size > virtual_size || rodata_offset > virtual_size ||
            rodata_size > virtual_size || data_offset > virtual_size ||
            data_size > virtual_size || bss_offset > virtual_size || bss_size > virtual_size) return 0;
        if (code_size < 4u || entry_offset >= code_size ||
            (entry_offset & 3u) != 0u || code_size - entry_offset < 4u) return 0;
        if (rodata_offset != align_up_u32(code_size, CHOS_EXEC_PAGE_SIZE)) return 0;
        if (!add_u32(rodata_offset, rodata_size, &initialized_end) ||
            initialized_end > virtual_size ||
            data_offset != align_up_u32(initialized_end, CHOS_EXEC_PAGE_SIZE)) return 0;
        if (!add_u32(data_offset, data_size, &initialized_end) ||
            initialized_end > virtual_size || bss_offset != initialized_end) return 0;
        if (!add_u32(bss_offset, bss_size, &bss_end) || bss_end > virtual_size ||
            virtual_size != align_up_u32(bss_end, CHOS_EXEC_PAGE_SIZE)) return 0;
        if (image_size != initialized_end || image_size > virtual_size ||
            image_size < data_offset) return 0;
        total_size = (size_t)header_size + (size_t)image_size;
        if (file_size != total_size) return 0;
        out->version = version;
        out->image_size = image_size;
        out->entry_offset = entry_offset;
        out->stack_pages = stack_pages;
        out->header_size = header_size;
        out->code_size = code_size;
        out->rodata_offset = rodata_offset;
        out->rodata_size = rodata_size;
        out->data_offset = data_offset;
        out->data_size = data_size;
        out->bss_offset = bss_offset;
        out->bss_size = bss_size;
        out->virtual_size = virtual_size;
        out->image_offset = header_size;
        return 1;
    }
    return 0;
}

int process_image_write_header(void *destination, size_t capacity,
                               uint32_t image_size, uint32_t entry_offset,
                               uint16_t stack_pages) {
    uint8_t *bytes = (uint8_t *)destination;
    if (bytes == NULL || capacity < CHOS_EXEC_V1_HEADER_SIZE || image_size == 0u ||
        image_size > CHOS_EXEC_MAX_IMAGE_SIZE || entry_offset >= image_size ||
        (entry_offset & 3u) != 0u || image_size - entry_offset < 4u ||
        stack_pages < CHOS_EXEC_MIN_STACK_PAGES || stack_pages > CHOS_EXEC_MAX_STACK_PAGES) return 0;
    bytes[0] = CHOS_EXEC_MAGIC_0; bytes[1] = CHOS_EXEC_MAGIC_1;
    bytes[2] = CHOS_EXEC_MAGIC_2; bytes[3] = CHOS_EXEC_MAGIC_3;
    write_le16(bytes + 4u, CHOS_EXEC_VERSION);
    write_le16(bytes + 6u, CHOS_EXEC_V1_HEADER_SIZE);
    write_le32(bytes + 8u, image_size); write_le32(bytes + 12u, entry_offset);
    write_le16(bytes + 16u, stack_pages); write_le16(bytes + 18u, 0u);
    write_le32(bytes + 20u, 0u);
    return 1;
}

int process_image_write_v2_header(void *destination, size_t capacity,
                                  uint32_t image_size, uint32_t entry_offset,
                                  uint16_t stack_pages, uint32_t code_size,
                                  uint32_t rodata_offset, uint32_t rodata_size,
                                  uint32_t data_offset, uint32_t data_size,
                                  uint32_t bss_offset, uint32_t bss_size,
                                  uint32_t virtual_size) {
    uint8_t *bytes = (uint8_t *)destination;
    uint32_t expected_data, expected_bss, expected_virtual;
    if (bytes == NULL || capacity < CHOS_EXEC_V2_HEADER_SIZE || image_size == 0u ||
        image_size > CHOS_EXEC_MAX_IMAGE_SIZE || code_size > CHOS_EXEC_MAX_IMAGE_SIZE ||
        rodata_offset > CHOS_EXEC_MAX_IMAGE_SIZE || rodata_size > CHOS_EXEC_MAX_IMAGE_SIZE ||
        data_offset > CHOS_EXEC_MAX_IMAGE_SIZE || data_size > CHOS_EXEC_MAX_IMAGE_SIZE ||
        bss_offset > CHOS_EXEC_MAX_IMAGE_SIZE || bss_size > CHOS_EXEC_MAX_IMAGE_SIZE ||
        virtual_size == 0u || virtual_size > CHOS_EXEC_MAX_IMAGE_SIZE || code_size < 4u ||
        entry_offset >= code_size || (entry_offset & 3u) != 0u || code_size - entry_offset < 4u ||
        stack_pages < CHOS_EXEC_MIN_STACK_PAGES || stack_pages > CHOS_EXEC_MAX_STACK_PAGES ||
        rodata_offset != align_up_u32(code_size, CHOS_EXEC_PAGE_SIZE) ||
        !add_u32(rodata_offset, rodata_size, &expected_data) ||
        data_offset != align_up_u32(expected_data, CHOS_EXEC_PAGE_SIZE) ||
        !add_u32(data_offset, data_size, &expected_bss) || bss_offset != expected_bss ||
        !add_u32(bss_offset, bss_size, &expected_virtual) ||
        virtual_size != align_up_u32(expected_virtual, CHOS_EXEC_PAGE_SIZE) ||
        virtual_size == 0u || virtual_size > CHOS_EXEC_MAX_IMAGE_SIZE ||
        image_size != expected_bss || image_size < data_offset || image_size > virtual_size) return 0;
    bytes[0] = CHOS_EXEC_MAGIC_0; bytes[1] = CHOS_EXEC_MAGIC_1;
    bytes[2] = CHOS_EXEC_MAGIC_2; bytes[3] = CHOS_EXEC_MAGIC_3;
    write_le16(bytes + 4u, CHOS_EXEC_VERSION_SEGMENTS);
    write_le16(bytes + 6u, CHOS_EXEC_V2_HEADER_SIZE);
    write_le32(bytes + 8u, image_size); write_le32(bytes + 12u, entry_offset);
    write_le16(bytes + 16u, stack_pages); write_le16(bytes + 18u, 0u); write_le32(bytes + 20u, 0u);
    write_le32(bytes + 24u, code_size); write_le32(bytes + 28u, rodata_offset);
    write_le32(bytes + 32u, rodata_size); write_le32(bytes + 36u, data_offset);
    write_le32(bytes + 40u, data_size); write_le32(bytes + 44u, bss_offset);
    write_le32(bytes + 48u, bss_size); write_le32(bytes + 52u, virtual_size);
    return 1;
}
