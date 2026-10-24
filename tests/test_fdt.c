#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/fdt.h"
#include "chos/pmm.h"

#define TEST_FDT_MAGIC 0xd00dfeedu
#define TEST_BEGIN_NODE 1u
#define TEST_END_NODE 2u
#define TEST_PROP 3u
#define TEST_END 9u

struct tree_writer {
    uint8_t structure[512];
    size_t structure_size;
    uint8_t strings[128];
    size_t strings_size;
};

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void be64(uint8_t *p, uint64_t v) {
    be32(p, (uint32_t)(v >> 32));
    be32(p + 4u, (uint32_t)v);
}

static void append32(uint8_t *buffer, size_t *size, uint32_t value) {
    be32(buffer + *size, value);
    *size += 4u;
}

static void append_data(uint8_t *buffer, size_t *size, const uint8_t *data,
                        size_t length) {
    memcpy(buffer + *size, data, length);
    *size += length;
}

static void align4(struct tree_writer *w) {
    while ((w->structure_size & 3u) != 0u) w->structure[w->structure_size++] = 0;
}

static uint32_t name_offset(struct tree_writer *w, const char *name) {
    uint32_t offset = (uint32_t)w->strings_size;
    size_t length = strlen(name) + 1u;
    memcpy(w->strings + w->strings_size, name, length);
    w->strings_size += length;
    return offset;
}

static void begin_node(struct tree_writer *w, const char *name) {
    size_t len = strlen(name) + 1u;
    append32(w->structure, &w->structure_size, TEST_BEGIN_NODE);
    append_data(w->structure, &w->structure_size, (const uint8_t *)name, len);
    align4(w);
}

static void end_node(struct tree_writer *w) {
    append32(w->structure, &w->structure_size, TEST_END_NODE);
}

static void property(struct tree_writer *w, const char *name,
                     const uint8_t *value, uint32_t length) {
    append32(w->structure, &w->structure_size, TEST_PROP);
    append32(w->structure, &w->structure_size, length);
    append32(w->structure, &w->structure_size, name_offset(w, name));
    append_data(w->structure, &w->structure_size, value, length);
    align4(w);
}

static void prop_u32(struct tree_writer *w, const char *name, uint32_t value) {
    uint8_t bytes[4];
    be32(bytes, value);
    property(w, name, bytes, sizeof(bytes));
}

static void prop_string(struct tree_writer *w, const char *name, const char *value) {
    property(w, name, (const uint8_t *)value, (uint32_t)(strlen(value) + 1u));
}

static void prop_reg(struct tree_writer *w, uint32_t base, uint32_t size) {
    uint8_t bytes[16];
    be32(bytes + 0, 0);
    be32(bytes + 4, base);
    be32(bytes + 8, 0);
    be32(bytes + 12, size);
    property(w, "reg", bytes, sizeof(bytes));
}

static size_t make_test_blob(uint8_t *blob, size_t capacity, int empty_reserved_ranges,
                             int include_dynamic_request) {
    struct tree_writer w = { {0}, 0, {0}, 0 };
    const uint8_t empty_value = 0u;
    const uint8_t nonempty_ranges[4] = { 0u, 0u, 0u, 0u };
    size_t struct_offset = 72u;
    size_t strings_offset;
    size_t total;

    begin_node(&w, "");
    prop_u32(&w, "#address-cells", 2u);
    prop_u32(&w, "#size-cells", 2u);
    prop_string(&w, "model", "CHOS test board");
    prop_string(&w, "compatible", "chos,test-board");

    begin_node(&w, "memory@40000000");
    prop_string(&w, "device_type", "memory");
    prop_reg(&w, 0x40000000u, 0x10000000u);
    end_node(&w);

    begin_node(&w, "pl011@9000000");
    prop_string(&w, "compatible", "arm,pl011");
    prop_reg(&w, 0x09000000u, 0x1000u);
    end_node(&w);

    begin_node(&w, "reserved-memory");
    prop_u32(&w, "#address-cells", 2u);
    prop_u32(&w, "#size-cells", 2u);
    property(&w, "ranges", empty_reserved_ranges ? &empty_value : nonempty_ranges,
             empty_reserved_ranges ? 0u : (uint32_t)sizeof(nonempty_ranges));
    begin_node(&w, "dma-pool@42000000");
    prop_reg(&w, 0x42000000u, 0x3000u);
    end_node(&w);
    if (include_dynamic_request) {
        begin_node(&w, "linux,cma");
        {
            uint8_t dynamic_size[8];
            be64(dynamic_size, 0x00400000u);
            property(&w, "size", dynamic_size, sizeof(dynamic_size));
        }
        end_node(&w);
    }
    end_node(&w);

    end_node(&w);
    append32(w.structure, &w.structure_size, TEST_END);

    strings_offset = struct_offset + w.structure_size;
    total = strings_offset + w.strings_size;
    assert(total <= capacity);
    memset(blob, 0, total);
    be32(blob + 0, TEST_FDT_MAGIC);
    be32(blob + 4, (uint32_t)total);
    be32(blob + 8, (uint32_t)struct_offset);
    be32(blob + 12, (uint32_t)strings_offset);
    be32(blob + 16, 40u); /* Memory-reservation map at offset 40. */
    be64(blob + 40u, 0x40100000u);
    be64(blob + 48u, 0x2000u);
    /* Offset 56..71 is the required zero/zero map terminator. */
    be32(blob + 20, 17u);
    be32(blob + 24, 16u);
    be32(blob + 28, 0u);
    be32(blob + 32, (uint32_t)w.strings_size);
    be32(blob + 36, (uint32_t)w.structure_size);
    memcpy(blob + struct_offset, w.structure, w.structure_size);
    memcpy(blob + strings_offset, w.strings, w.strings_size);
    return total;
}

int main(void) {
    uint8_t blob[1024];
    fdt_info_t info;
    (void)make_test_blob(blob, sizeof(blob), 1, 0);

    assert(fdt_parse_blob(blob, &info) == FDT_OK);
    assert(info.valid == 1);
    assert(strcmp(info.model, "CHOS test board") == 0);
    assert(strcmp(info.compatible, "chos,test-board") == 0);
    assert(info.memory_count == 1u);
    assert(info.memory[0].base == 0x40000000u);
    assert(info.memory[0].size == 0x10000000u);
    assert(info.device_count == 1u);
    assert(strcmp(info.devices[0].name, "pl011@9000000") == 0);
    assert(strcmp(info.devices[0].compatible, "arm,pl011") == 0);
    assert(info.devices[0].reg_base == 0x09000000u);
    assert(info.devices[0].reg_size == 0x1000u);
    assert(info.reserved_count == 2u);
    assert(info.reserved[0].base == 0x40100000u);
    assert(info.reserved[0].size == 0x2000u);
    assert(info.reserved[0].source == 1u);
    assert(info.reserved[1].base == 0x42000000u);
    assert(info.reserved[1].size == 0x3000u);
    assert(info.reserved[1].source == 2u);
    assert(info.reserved_dynamic_count == 0u);

    /* Integrated check: parsed FDT reservations are consumed by PMM before
       free pages are exposed. This RAM map is 256 MiB (0x40000000..0x50000000). */
    {
        pmm_stats_t pmm_stats;
        assert(pmm_init(&info, 0x40200000u, 0x40220000u,
                        0x40000000u, info.total_size) == PMM_OK);
        pmm_get_stats(&pmm_stats);
        assert(pmm_stats.reserved_pages == 38u); /* DTB + kernel + 2 + 3 pages. */
        assert(!pmm_free_page(0x40100000u));
        assert(!pmm_free_page(0x42000000u));
    }

    (void)make_test_blob(blob, sizeof(blob), 1, 1);
    assert(fdt_parse_blob(blob, &info) == FDT_OK);
    assert(info.reserved_dynamic_count == 1u);
    assert(pmm_init(&info, 0x40200000u, 0x40220000u,
                    0x40000000u, info.total_size) == PMM_ERR_UNSUPPORTED_RESERVATION);

    (void)make_test_blob(blob, sizeof(blob), 0, 0);
    assert(fdt_parse_blob(blob, &info) == FDT_ERR_UNSUPPORTED_RESERVED_MEMORY_LAYOUT);
    (void)make_test_blob(blob, sizeof(blob), 1, 0);

    be32(blob + 16u, 0xfffffff8u);
    assert(fdt_parse_blob(blob, &info) == FDT_ERR_BAD_RESERVATION_MAP);
    (void)make_test_blob(blob, sizeof(blob), 1, 0);
    be64(blob + 56u, 1u); /* Destroy the reserve-map terminator. */
    be64(blob + 64u, 2u);
    assert(fdt_parse_blob(blob, &info) == FDT_ERR_BAD_RESERVATION_MAP);
    (void)make_test_blob(blob, sizeof(blob), 1, 0);
    blob[0] = 0;
    assert(fdt_parse_blob(blob, &info) == FDT_ERR_BAD_MAGIC);
    puts("FDT parser tests passed");
    return 0;
}
