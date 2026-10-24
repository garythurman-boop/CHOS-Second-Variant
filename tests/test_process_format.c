#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chos/process.h"

#define FILE_CAPACITY (CHOS_EXEC_V2_HEADER_SIZE + CHOS_EXEC_MAX_IMAGE_SIZE)

static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int main(void) {
    uint8_t *file = (uint8_t *)calloc(1u, FILE_CAPACITY);
    chos_exec_image_info_t info;
    size_t valid_length = CHOS_EXEC_HEADER_SIZE + 16u;
    assert(file != NULL);
    assert(process_image_write_header(file, FILE_CAPACITY, 16u, 4u, 1u));
    file[CHOS_EXEC_HEADER_SIZE + 4u] = 0x1f; /* payload itself is opaque to format validator */
    assert(process_image_validate(file, valid_length, &info));
    assert(info.image_size == 16u && info.entry_offset == 4u && info.stack_pages == 1u);
    assert(info.header_size == CHOS_EXEC_HEADER_SIZE && info.image_offset == CHOS_EXEC_HEADER_SIZE);

    assert(!process_image_validate(NULL, valid_length, &info));
    assert(!process_image_validate(file, CHOS_EXEC_HEADER_SIZE - 1u, &info));
    assert(!process_image_validate(file, valid_length - 1u, &info));
    assert(!process_image_validate(file, valid_length + 1u, &info));

    file[0] = 'X';
    assert(!process_image_validate(file, valid_length, &info));
    file[0] = 'C';

    put_le16(file + 4u, 2u);
    assert(!process_image_validate(file, valid_length, &info));
    put_le16(file + 4u, CHOS_EXEC_VERSION);
    put_le16(file + 6u, CHOS_EXEC_HEADER_SIZE + 1u);
    assert(!process_image_validate(file, valid_length, &info));
    put_le16(file + 6u, CHOS_EXEC_HEADER_SIZE);

    put_le32(file + 8u, 0u);
    assert(!process_image_validate(file, valid_length, &info));
    put_le32(file + 8u, CHOS_EXEC_MAX_IMAGE_SIZE + 1u);
    assert(!process_image_validate(file, valid_length, &info));
    put_le32(file + 8u, 16u);

    put_le32(file + 12u, 16u);
    assert(!process_image_validate(file, valid_length, &info));
    put_le32(file + 12u, 2u);
    assert(!process_image_validate(file, valid_length, &info));
    put_le32(file + 12u, 4u);

    put_le16(file + 16u, 0u);
    assert(!process_image_validate(file, valid_length, &info));
    put_le16(file + 16u, CHOS_EXEC_MAX_STACK_PAGES);
    assert(process_image_validate(file, valid_length, &info));
    assert(info.stack_pages == CHOS_EXEC_MAX_STACK_PAGES);
    put_le16(file + 16u, CHOS_EXEC_MAX_STACK_PAGES + 1u);
    assert(!process_image_validate(file, valid_length, &info));
    put_le16(file + 16u, 1u);

    put_le16(file + 18u, 1u);
    assert(!process_image_validate(file, valid_length, &info));
    put_le16(file + 18u, 0u);
    put_le32(file + 20u, 1u);
    assert(!process_image_validate(file, valid_length, &info));
    put_le32(file + 20u, 0u);
    assert(process_image_validate(file, valid_length, &info));

    assert(!process_image_write_header(file, CHOS_EXEC_HEADER_SIZE - 1u, 16u, 0u, 1u));
    assert(!process_image_write_header(file, FILE_CAPACITY, 16u, 1u, 1u));
    assert(!process_image_write_header(file, FILE_CAPACITY, 8u, 8u, 1u));
    assert(!process_image_write_header(file, FILE_CAPACITY, 16u, 0u, 0u));
    assert(!process_image_write_header(file, FILE_CAPACITY, CHOS_EXEC_MAX_IMAGE_SIZE + 1u, 0u, 1u));

    /* CHCX v2 has separate RX/R/RW pages and zero-filled BSS. */
    {
        const uint32_t image_size = 8192u + 8u;
        const size_t v2_length = CHOS_EXEC_V2_HEADER_SIZE + image_size;
        assert(process_image_write_v2_header(file, FILE_CAPACITY, image_size, 0u, 2u,
                                             16u, 4096u, 16u, 8192u, 8u,
                                             8200u, 120u, 12288u));
        assert(process_image_validate(file, v2_length, &info));
        assert(info.version == CHOS_EXEC_VERSION_SEGMENTS);
        assert(info.image_size == image_size && info.entry_offset == 0u);
        assert(info.code_size == 16u && info.rodata_offset == 4096u && info.rodata_size == 16u);
        assert(info.data_offset == 8192u && info.data_size == 8u);
        assert(info.bss_offset == 8200u && info.bss_size == 120u && info.virtual_size == 12288u);
        assert(info.image_offset == CHOS_EXEC_V2_HEADER_SIZE && info.stack_pages == 2u);
        assert(!process_image_validate(file, v2_length - 1u, &info));
        assert(!process_image_write_v2_header(file, FILE_CAPACITY, image_size, 16u, 2u,
                                             16u, 4096u, 16u, 8192u, 8u,
                                             8200u, 120u, 12288u));
        assert(!process_image_write_v2_header(file, FILE_CAPACITY, image_size, 0u, 2u,
                                             16u, 2048u, 16u, 8192u, 8u,
                                             8200u, 120u, 12288u));
        file[29] = 0u; /* Corrupt rodata_offset in the v2 header. */
        assert(!process_image_validate(file, v2_length, &info));
    }

    free(file);
    puts("process image format tests passed");
    return 0;
}
