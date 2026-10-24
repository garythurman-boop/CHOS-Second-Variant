#include <stddef.h>
#include <stdint.h>
#include "chos/fdt.h"

#define FDT_MAGIC 0xd00dfeedu
#define FDT_HEADER_SIZE 40u
#define FDT_MAX_TOTAL_SIZE (16u * 1024u * 1024u)
#define FDT_MAX_DEPTH 16u
#define FDT_RESERVED_SOURCE_MAP 1u
#define FDT_RESERVED_SOURCE_NODE 2u

#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_NOP        4u
#define FDT_END        9u

typedef struct {
    char name[FDT_NODE_NAME_CAPACITY];
    char compatible[FDT_COMPATIBLE_CAPACITY];
    char device_type[16];
    int enabled;
    uint32_t address_cells;
    uint32_t size_cells;
    const uint8_t *reg;
    uint32_t reg_length;
    int under_reserved_memory;
    int reserved_context_valid;
    int has_size;
} fdt_node_t;

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t read_be64(const uint8_t *p) {
    return ((uint64_t)read_be32(p) << 32) | (uint64_t)read_be32(p + 4u);
}

static int range_inside(uint32_t offset, uint32_t length, uint32_t total) {
    return offset <= total && length <= total - offset;
}

static void clear_bytes(void *dest, size_t length) {
    uint8_t *d = (uint8_t *)dest;
    size_t i;
    for (i = 0; i < length; ++i) d[i] = 0;
}

static void copy_text(char *dest, size_t capacity, const char *source,
                      size_t source_length) {
    size_t i;
    if (capacity == 0u) return;
    for (i = 0; i + 1u < capacity && i < source_length && source[i] != '\0'; ++i) {
        dest[i] = source[i];
    }
    dest[i] = '\0';
}

static int text_equal(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int text_starts_with(const char *text, const char *prefix) {
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) return 0;
        ++i;
    }
    return 1;
}

static uint64_t read_cells(const uint8_t *data, uint32_t cells, int *ok) {
    uint64_t value = 0;
    uint32_t i;
    if (cells > 2u) {
        *ok = 0;
        return 0;
    }
    for (i = 0; i < cells; ++i) {
        value = (value << 32) | read_be32(data + (size_t)i * 4u);
    }
    return value;
}

static void copy_first_string(char *dest, size_t capacity,
                              const uint8_t *value, uint32_t length) {
    uint32_t n = 0;
    while (n < length && value[n] != 0u) ++n;
    copy_text(dest, capacity, (const char *)value, n);
}

static void store_memory(fdt_info_t *out, uint64_t base, uint64_t size) {
    size_t n = out->memory_count;
    if (size == 0u) return;
    if (n >= FDT_MAX_MEMORY_REGIONS) return;
    out->memory[n].base = base;
    out->memory[n].size = size;
    out->memory_count = n + 1u;
}

static int store_reserved(fdt_info_t *out, uint64_t base, uint64_t size,
                          uint8_t source) {
    uint64_t end;
    fdt_reserved_region_t *region;
    if (size == 0u) return 1;
    if (size > UINT64_MAX - base || out->reserved_count >= FDT_MAX_RESERVED_REGIONS) {
        return 0;
    }
    end = base + size;
    if (end <= base) return 0;
    (void)end;
    region = &out->reserved[out->reserved_count++];
    region->base = base;
    region->size = size;
    region->source = source;
    return 1;
}

static fdt_error_t process_node(fdt_info_t *out, const fdt_node_t *node,
                                const fdt_node_t *parent) {
    uint32_t address_cells = parent != NULL ? parent->address_cells : 2u;
    uint32_t size_cells = parent != NULL ? parent->size_cells : 1u;
    uint32_t tuple_cells = address_cells + size_cells;
    uint32_t tuple_bytes;
    uint32_t offset;
    int is_memory = text_equal(node->device_type, "memory") ||
                    text_equal(node->name, "memory") ||
                    text_starts_with(node->name, "memory@");
    int is_reserved = node->under_reserved_memory;

    if (!node->enabled) return FDT_OK;
    if (is_reserved && (node->reg != NULL || node->has_size) &&
        !node->reserved_context_valid) {
        /* Child `reg` values are physical only when reserved-memory has the
           required empty `ranges` property. Do not guess bus translations. */
        return FDT_ERR_UNSUPPORTED_RESERVED_MEMORY_LAYOUT;
    }
    if (is_reserved && node->reg == NULL && node->has_size) {
        /* Dynamic reservations have no physical base until a bootloader or
           kernel allocator places them. Keep count so PMM can fail closed. */
        ++out->reserved_dynamic_count;
        return FDT_OK;
    }
    if (node->reg == NULL) return FDT_OK;
    if (tuple_cells == 0u || tuple_cells > 4u || address_cells > 2u ||
        size_cells > 2u) {
        return (is_memory || is_reserved) ? FDT_ERR_BAD_STRUCTURE : FDT_OK;
    }
    tuple_bytes = tuple_cells * 4u;
    if (tuple_bytes == 0u || node->reg_length < tuple_bytes ||
        (node->reg_length % tuple_bytes) != 0u) {
        return (is_memory || is_reserved) ? FDT_ERR_BAD_STRUCTURE : FDT_OK;
    }

    for (offset = 0u; offset < node->reg_length; offset += tuple_bytes) {
        int ok = 1;
        uint64_t base = read_cells(node->reg + offset, address_cells, &ok);
        uint64_t size = read_cells(node->reg + offset + address_cells * 4u,
                                   size_cells, &ok);
        if (!ok || (size != 0u && size > UINT64_MAX - base)) {
            return (is_memory || is_reserved) ? FDT_ERR_BAD_STRUCTURE : FDT_OK;
        }
        if (is_memory) {
            store_memory(out, base, size);
        } else if (is_reserved) {
            if (!store_reserved(out, base, size, FDT_RESERVED_SOURCE_NODE)) {
                return FDT_ERR_TOO_MANY_RESERVATIONS;
            }
        } else if (offset == 0u && node->compatible[0] != '\0') {
            if (out->device_count >= FDT_MAX_DEVICES) {
                out->devices_truncated = 1;
                break;
            }
            {
                fdt_device_t *device = &out->devices[out->device_count++];
                copy_text(device->name, sizeof(device->name), node->name,
                          sizeof(node->name));
                copy_text(device->compatible, sizeof(device->compatible),
                          node->compatible, sizeof(node->compatible));
                device->has_reg = 1;
                device->reg_base = base;
                device->reg_size = size;
            }
        }
    }
    return FDT_OK;
}

static fdt_error_t parse_reservation_map(const uint8_t *bytes, uint32_t total_size,
                                        uint32_t reserve_offset,
                                        uint32_t structure_offset,
                                        fdt_info_t *out) {
    uint32_t position = reserve_offset;

    /* FDT places the big-endian 64-bit reservation pairs before the structure
       block. Requiring a bounded terminator avoids reading arbitrary memory. */
    if (reserve_offset < FDT_HEADER_SIZE || (reserve_offset & 7u) != 0u ||
        reserve_offset >= structure_offset || structure_offset > total_size) {
        return FDT_ERR_BAD_RESERVATION_MAP;
    }
    while (position <= structure_offset && structure_offset - position >= 16u &&
           range_inside(position, 16u, total_size)) {
        uint64_t base = read_be64(bytes + position);
        uint64_t size = read_be64(bytes + position + 8u);
        position += 16u;
        if (base == 0u && size == 0u) return FDT_OK;
        if (size != 0u && size > UINT64_MAX - base) {
            return FDT_ERR_BAD_RESERVATION_MAP;
        }
        if (size != 0u && !store_reserved(out, base, size, FDT_RESERVED_SOURCE_MAP)) {
            return FDT_ERR_TOO_MANY_RESERVATIONS;
        }
    }
    return FDT_ERR_BAD_RESERVATION_MAP;
}

static int find_string_end(const uint8_t *strings, uint32_t strings_size,
                           uint32_t name_offset, const char **name) {
    uint32_t i;
    if (name_offset >= strings_size) return 0;
    for (i = name_offset; i < strings_size; ++i) {
        if (strings[i] == 0u) {
            *name = (const char *)(strings + name_offset);
            return 1;
        }
    }
    return 0;
}

fdt_error_t fdt_parse_blob(const void *blob, fdt_info_t *out) {
    const uint8_t *bytes = (const uint8_t *)blob;
    const uint8_t *structure;
    const uint8_t *strings;
    fdt_node_t stack[FDT_MAX_DEPTH];
    uint32_t total_size, struct_offset, strings_offset, strings_size, struct_size;
    uint32_t reserve_offset;
    uint32_t position = 0;
    int depth = -1;
    int saw_root = 0;
    int saw_end = 0;
    size_t i;

    if (blob == NULL || out == NULL) return FDT_ERR_NULL_ARGUMENT;
    clear_bytes(out, sizeof(*out));
    if (read_be32(bytes) != FDT_MAGIC) {
        out->error = FDT_ERR_BAD_MAGIC;
        return out->error;
    }

    total_size = read_be32(bytes + 4u);
    struct_offset = read_be32(bytes + 8u);
    strings_offset = read_be32(bytes + 12u);
    reserve_offset = read_be32(bytes + 16u);
    strings_size = read_be32(bytes + 32u);
    struct_size = read_be32(bytes + 36u);
    if (total_size < FDT_HEADER_SIZE || total_size > FDT_MAX_TOTAL_SIZE ||
        !range_inside(struct_offset, struct_size, total_size) ||
        !range_inside(strings_offset, strings_size, total_size) ||
        struct_offset < FDT_HEADER_SIZE || strings_offset < FDT_HEADER_SIZE ||
        (struct_offset & 3u) != 0u || struct_size < 4u) {
        out->error = FDT_ERR_BAD_HEADER;
        return out->error;
    }

    out->total_size = total_size;
    {
        fdt_error_t reserve_result = parse_reservation_map(bytes, total_size,
                                                            reserve_offset,
                                                            struct_offset, out);
        if (reserve_result != FDT_OK) {
            out->error = reserve_result;
            return out->error;
        }
    }
    structure = bytes + struct_offset;
    strings = bytes + strings_offset;
    clear_bytes(stack, sizeof(stack));

    while (position + 4u <= struct_size) {
        uint32_t token = read_be32(structure + position);
        position += 4u;

        if (token == FDT_BEGIN_NODE) {
            uint32_t name_start = position;
            uint32_t name_length;
            if (depth + 1 >= (int)FDT_MAX_DEPTH) {
                out->error = FDT_ERR_TOO_DEEP;
                return out->error;
            }
            while (position < struct_size && structure[position] != 0u) ++position;
            if (position >= struct_size) {
                out->error = FDT_ERR_BAD_STRUCTURE;
                return out->error;
            }
            name_length = position - name_start;
            ++depth;
            clear_bytes(&stack[depth], sizeof(stack[depth]));
            stack[depth].enabled = 1;
            stack[depth].address_cells = depth > 0 ? stack[depth - 1].address_cells : 2u;
            stack[depth].size_cells = depth > 0 ? stack[depth - 1].size_cells : 1u;
            stack[depth].under_reserved_memory =
                depth > 0 && (stack[depth - 1].under_reserved_memory ||
                (depth == 2 && text_equal(stack[depth - 1].name, "reserved-memory")));
            stack[depth].reserved_context_valid = 0;
            if (depth > 0 && stack[depth - 1].reserved_context_valid) {
                stack[depth].reserved_context_valid = 1;
            }
            if (depth == 2 && text_equal(stack[depth - 1].name, "reserved-memory")) {
                stack[depth].reserved_context_valid = stack[depth - 1].reserved_context_valid;
            }
            copy_text(stack[depth].name, sizeof(stack[depth].name),
                      (const char *)(structure + name_start), name_length);
            if (depth == 0) saw_root = 1;
            position = (position + 4u) & ~3u;
        } else if (token == FDT_END_NODE) {
            const fdt_node_t *parent = depth > 0 ? &stack[depth - 1] : NULL;
            if (depth < 0) {
                out->error = FDT_ERR_BAD_STRUCTURE;
                return out->error;
            }
            {
                fdt_error_t node_result = process_node(out, &stack[depth], parent);
                if (node_result != FDT_OK) {
                    out->error = node_result;
                    return out->error;
                }
            }
            --depth;
        } else if (token == FDT_PROP) {
            uint32_t length, name_offset;
            const char *name;
            const uint8_t *value;
            fdt_node_t *node;
            if (depth < 0 || position + 8u > struct_size) {
                out->error = FDT_ERR_BAD_STRUCTURE;
                return out->error;
            }
            length = read_be32(structure + position);
            name_offset = read_be32(structure + position + 4u);
            position += 8u;
            if (length > struct_size - position ||
                !find_string_end(strings, strings_size, name_offset, &name)) {
                out->error = FDT_ERR_BAD_STRUCTURE;
                return out->error;
            }
            value = structure + position;
            node = &stack[depth];

            if (text_equal(name, "#address-cells") && length == 4u) {
                node->address_cells = read_be32(value);
            } else if (text_equal(name, "#size-cells") && length == 4u) {
                node->size_cells = read_be32(value);
            } else if (text_equal(name, "compatible") && length > 0u) {
                copy_first_string(node->compatible, sizeof(node->compatible), value, length);
                if (depth == 0) copy_first_string(out->compatible, sizeof(out->compatible), value, length);
            } else if (text_equal(name, "device_type") && length > 0u) {
                copy_first_string(node->device_type, sizeof(node->device_type), value, length);
            } else if (text_equal(name, "reg")) {
                node->reg = value;
                node->reg_length = length;
            } else if (text_equal(name, "size")) {
                node->has_size = 1;
            } else if (text_equal(node->name, "reserved-memory") &&
                       text_equal(name, "ranges")) {
                node->reserved_context_valid = (length == 0u);
            } else if (depth == 0 && text_equal(name, "model") && length > 0u) {
                copy_first_string(out->model, sizeof(out->model), value, length);
            } else if (text_equal(node->name, "chosen") && text_equal(name, "bootargs") && length > 0u) {
                copy_first_string(out->bootargs, sizeof(out->bootargs), value, length);
            } else if (text_equal(node->name, "chosen") && text_equal(name, "stdout-path") && length > 0u) {
                copy_first_string(out->stdout_path, sizeof(out->stdout_path), value, length);
            } else if (text_equal(name, "status") && length > 0u) {
                char status[16];
                copy_first_string(status, sizeof(status), value, length);
                if (text_equal(status, "disabled") || text_equal(status, "fail") ||
                    text_equal(status, "failed")) node->enabled = 0;
            }
            position = (position + length + 3u) & ~3u;
            if (position > struct_size) {
                out->error = FDT_ERR_BAD_STRUCTURE;
                return out->error;
            }
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            saw_end = 1;
            break;
        } else {
            out->error = FDT_ERR_BAD_STRUCTURE;
            return out->error;
        }
    }

    if (!saw_root || !saw_end || depth != -1) {
        out->error = FDT_ERR_BAD_STRUCTURE;
        return out->error;
    }
    /* Defensive check: counted memory regions should have nonzero lengths. */
    for (i = 0; i < out->memory_count; ++i) {
        if (out->memory[i].size == 0u) {
            out->error = FDT_ERR_BAD_STRUCTURE;
            return out->error;
        }
    }
    out->valid = 1;
    out->error = FDT_OK;
    return FDT_OK;
}

const char *fdt_error_string(fdt_error_t error) {
    switch (error) {
        case FDT_OK: return "OK";
        case FDT_ERR_NULL_ARGUMENT: return "null argument";
        case FDT_ERR_BAD_MAGIC: return "bad FDT magic";
        case FDT_ERR_BAD_HEADER: return "invalid FDT header or bounds";
        case FDT_ERR_BAD_STRUCTURE: return "malformed FDT structure";
        case FDT_ERR_BAD_RESERVATION_MAP: return "malformed FDT memory-reservation map";
        case FDT_ERR_TOO_MANY_RESERVATIONS: return "too many FDT memory reservations";
        case FDT_ERR_UNSUPPORTED_RESERVED_MEMORY_LAYOUT: return "unsupported /reserved-memory ranges translation";
        case FDT_ERR_TOO_DEEP: return "FDT node nesting too deep";
        default: return "unknown FDT parse error";
    }
}
