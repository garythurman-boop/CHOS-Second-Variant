#include "chos/usb_hid.h"

#define HID_MAIN_INPUT 8u
#define HID_MAIN_OUTPUT 9u
#define HID_MAIN_COLLECTION 10u
#define HID_MAIN_FEATURE 11u
#define HID_MAIN_END_COLLECTION 12u
#define HID_GLOBAL_USAGE_PAGE 0u
#define HID_GLOBAL_LOGICAL_MIN 1u
#define HID_GLOBAL_LOGICAL_MAX 2u
#define HID_GLOBAL_REPORT_SIZE 7u
#define HID_GLOBAL_REPORT_ID 8u
#define HID_GLOBAL_REPORT_COUNT 9u
#define HID_GLOBAL_PUSH 10u
#define HID_GLOBAL_POP 11u
#define HID_LOCAL_USAGE 0u
#define HID_LOCAL_USAGE_MIN 1u
#define HID_LOCAL_USAGE_MAX 2u
#define HID_ITEM_LONG 0xfeu
#define HID_COLLECTION_MAX_DEPTH 16u
#define HID_LOCAL_MAX_USAGES 32u

typedef struct {
    uint32_t usage_page;
    int64_t logical_min;
    int64_t logical_max;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
} hid_global_t;

typedef struct {
    uint16_t usages[HID_LOCAL_MAX_USAGES];
    uint8_t usage_count;
    uint16_t usage_min;
    uint16_t usage_max;
    uint8_t has_min;
    uint8_t has_max;
} hid_local_t;

static uint32_t item_unsigned(const uint8_t *p, uint8_t size) {
    uint32_t value = 0u;
    uint8_t i;
    for (i = 0u; i < size; ++i) value |= (uint32_t)p[i] << (8u * i);
    return value;
}

static int32_t item_signed(uint32_t value, uint8_t size) {
    uint8_t bits = (uint8_t)(size * 8u);
    if (bits == 0u) return 0;
    if (bits < 32u && (value & (UINT32_C(1) << (bits - 1u))) != 0u)
        value |= ~((UINT32_C(1) << bits) - 1u);
    return (int32_t)value;
}

static void local_clear(hid_local_t *local) {
    local->usage_count = 0u;
    local->usage_min = 0u;
    local->usage_max = 0u;
    local->has_min = 0u;
    local->has_max = 0u;
}

static uint16_t local_usage_for(const hid_local_t *local, uint32_t index) {
    if (index < local->usage_count) return local->usages[index];
    if (local->has_min && local->has_max && local->usage_max >= local->usage_min) {
        uint32_t usage = (uint32_t)local->usage_min + index;
        if (usage <= local->usage_max) return (uint16_t)usage;
    }
    return 0u;
}

static int append_field(chos_hid_report_layout_t *layout, const hid_global_t *global,
                        const hid_local_t *local, uint32_t bit_offset,
                        uint8_t bit_size, uint8_t count, uint8_t flags,
                        uint32_t usage_index, int array_field) {
    chos_hid_field_t *field;
    uint16_t usage = local_usage_for(local, usage_index);
    if (layout->field_count >= CHOS_HID_MAX_FIELDS) return 0;
    if (bit_offset > UINT16_MAX || bit_size == 0u || bit_size > 32u || count == 0u) return 0;
    field = &layout->fields[layout->field_count++];
    field->report_id = global->report_id;
    field->usage_page = (uint16_t)(global->usage_page & 0xffffu);
    field->usage = usage;
    field->usage_min = array_field && local->has_min ? local->usage_min : usage;
    field->usage_max = array_field && local->has_max ? local->usage_max : usage;
    field->bit_offset = (uint16_t)bit_offset;
    field->bit_size = bit_size;
    field->count = count;
    field->flags = flags;
    field->logical_min=global->logical_min;field->logical_max=global->logical_max;
    if (global->usage_page == 0x07u) layout->has_keyboard = 1u;
    if (global->usage_page == 0x09u) layout->has_pointer = 1u;
    if (global->usage_page == 0x01u) {
        if(usage==0x30 || usage==0x31)layout->has_pointer=1u;
        if (usage == 0x01u || (local->has_min && local->usage_min == 0x01u)) layout->has_pointer = 1u;
        if (usage == 0x02u || (local->has_min && local->usage_min == 0x02u)) layout->has_mouse = 1u;
        if (usage == 0x38u || (local->has_min && local->usage_min <= 0x38u && local->has_max >= 0x38u)) layout->has_wheel = 1u;
    }
    return 1;
}

static int parse_main_fields(chos_hid_report_layout_t *layout, const hid_global_t *global,
                             const hid_local_t *local, uint32_t item_flags, uint8_t main_tag) {
    uint32_t *bit_count;
    uint32_t bits, i;
    uint8_t flags = 0u;
    int variable = (item_flags & 0x02u) != 0u;
    int constant = (item_flags & 0x01u) != 0u;
    int relative = (item_flags & 0x04u) != 0u;
    int signed_value = global->logical_min < 0;
    if (main_tag == HID_MAIN_INPUT) bit_count = &layout->input_bits[global->report_id];
    else if (main_tag == HID_MAIN_OUTPUT) bit_count = &layout->output_bits[global->report_id];
    else bit_count = &layout->feature_bits[global->report_id];
    if (global->report_size == 0u || global->report_size > 32u ||
        global->report_count == 0u || global->report_count > 255u)
        return 0;
    if (global->report_count > (UINT32_MAX - *bit_count) / global->report_size) return 0;
    bits = global->report_size * global->report_count;
    if (*bit_count > 65535u || bits > 65535u - *bit_count) return 0;
    if (constant) flags |= CHOS_HID_FIELD_CONSTANT;
    if (variable) flags |= CHOS_HID_FIELD_VARIABLE;
    if (relative) flags |= CHOS_HID_FIELD_RELATIVE;
    if (signed_value) flags |= CHOS_HID_FIELD_SIGNED;
    if (main_tag == HID_MAIN_INPUT && !constant) {
        if (variable) {
            for (i = 0u; i < global->report_count; ++i) {
                if (!append_field(layout, global, local,
                                  *bit_count + i * global->report_size,
                                  (uint8_t)global->report_size, 1u, flags, i, 0)) return 0;
            }
        } else {
            if (global->report_count > UINT8_MAX ||
                !append_field(layout, global, local, *bit_count,
                              (uint8_t)global->report_size,
                              (uint8_t)global->report_count, flags, 0u, 1)) return 0;
        }
    }
    *bit_count += bits;
    return 1;
}

chos_hid_error_t chos_hid_parse_report_descriptor(const uint8_t *descriptor,
                                                   size_t descriptor_size,
                                                   chos_hid_report_layout_t *layout) {
    hid_global_t global, global_stack[CHOS_HID_GLOBAL_STACK];
    hid_local_t local;
    size_t offset = 0u;
    uint8_t stack_depth = 0u, collection_depth = 0u;
    int saw_input = 0;
    if (descriptor == NULL || layout == NULL || descriptor_size == 0u) return CHOS_HID_ERR_ARGUMENT;
    {
        uint8_t *p = (uint8_t *)layout;
        size_t n;
        for (n = 0u; n < sizeof(*layout); ++n) p[n] = 0u;
    }
    global.usage_page = 0u; global.logical_min = 0; global.logical_max = 0;
    global.report_size = 0u; global.report_count = 0u; global.report_id = 0u;
    local_clear(&local);
    while (offset < descriptor_size) {
        uint8_t prefix = descriptor[offset++];
        uint8_t size_code, item_size, item_type, item_tag;
        uint32_t value;
        if (prefix == HID_ITEM_LONG) {
            uint8_t long_size;
            if (offset + 2u > descriptor_size) return CHOS_HID_ERR_TRUNCATED;
            long_size = descriptor[offset];
            offset += 2u; /* long-item size and tag */
            if ((size_t)long_size > descriptor_size - offset) return CHOS_HID_ERR_LONG_ITEM;
            offset += long_size;
            continue;
        }
        size_code = (uint8_t)(prefix & 0x03u);
        item_size = size_code == 3u ? 4u : size_code;
        item_type = (uint8_t)((prefix >> 2u) & 0x03u);
        item_tag = (uint8_t)(prefix >> 4u);
        if ((size_t)item_size > descriptor_size - offset) return CHOS_HID_ERR_TRUNCATED;
        value = item_unsigned(descriptor + offset, item_size);
        offset += item_size;
        if (item_type == 1u) { /* Global */
            switch (item_tag) {
                case HID_GLOBAL_USAGE_PAGE: global.usage_page = value; break;
                case HID_GLOBAL_LOGICAL_MIN: global.logical_min = item_signed(value, item_size); break;
                case HID_GLOBAL_LOGICAL_MAX:
                    global.logical_max = global.logical_min<0 ? (int64_t)item_signed(value,item_size) : (int64_t)value; break;
                case HID_GLOBAL_REPORT_SIZE: global.report_size = value; break;
                case HID_GLOBAL_REPORT_COUNT: global.report_count = value; break;
                case HID_GLOBAL_REPORT_ID:
                    if (value == 0u || value > 255u) return CHOS_HID_ERR_REPORT_ID;
                    global.report_id = (uint8_t)value; layout->uses_report_ids = 1u; break;
                case HID_GLOBAL_PUSH:
                    if (stack_depth >= CHOS_HID_GLOBAL_STACK) return CHOS_HID_ERR_GLOBAL_STACK;
                    global_stack[stack_depth++] = global; break;
                case HID_GLOBAL_POP:
                    if (stack_depth == 0u) return CHOS_HID_ERR_GLOBAL_STACK;
                    global = global_stack[--stack_depth]; break;
                default: break; /* Physical, Unit, Unit Exponent and reserved globals */
            }
        } else if (item_type == 2u) { /* Local */
            uint16_t usage = (uint16_t)(value & 0xffffu);
            switch (item_tag) {
                case HID_LOCAL_USAGE:
                    if (local.usage_count >= HID_LOCAL_MAX_USAGES) return CHOS_HID_ERR_LOCAL_USAGE;
                    local.usages[local.usage_count++] = usage; break;
                case HID_LOCAL_USAGE_MIN: local.usage_min = usage; local.has_min = 1u; break;
                case HID_LOCAL_USAGE_MAX:
                    local.usage_max = usage; local.has_max = 1u;
                    if (local.usage_max < local.usage_min) return CHOS_HID_ERR_LOCAL_USAGE;
                    break;
                default: break;
            }
        } else if (item_type == 0u) { /* Main */
            if (item_tag == HID_MAIN_INPUT || item_tag == HID_MAIN_OUTPUT || item_tag == HID_MAIN_FEATURE) {
                if (!parse_main_fields(layout, &global, &local, value, item_tag)) {
                    if (global.report_size == 0u || global.report_size > 32u ||
                        global.report_count == 0u || global.report_count > 255u) return CHOS_HID_ERR_REPORT_SIZE;
                    if (layout->field_count >= CHOS_HID_MAX_FIELDS) return CHOS_HID_ERR_TOO_MANY_FIELDS;
                    return CHOS_HID_ERR_REPORT_OVERFLOW;
                }
                if (item_tag == HID_MAIN_INPUT) saw_input = 1;
                local_clear(&local);
            } else if (item_tag == HID_MAIN_COLLECTION) {
                uint16_t usage = local_usage_for(&local, 0u);
                if (collection_depth >= HID_COLLECTION_MAX_DEPTH) return CHOS_HID_ERR_GLOBAL_STACK;
                ++collection_depth;
                if (global.usage_page == 0x01u && usage == 0x06u) layout->has_keyboard = 1u;
                if (global.usage_page == 0x01u && usage == 0x01u) layout->has_pointer = 1u;
                if (global.usage_page == 0x01u && usage == 0x02u) {
                    layout->has_pointer = 1u; layout->has_mouse = 1u;
                }
                local_clear(&local);
            } else if (item_tag == HID_MAIN_END_COLLECTION) {
                if (collection_depth == 0u) return CHOS_HID_ERR_GLOBAL_STACK;
                --collection_depth;
                local_clear(&local);
            } else {
                local_clear(&local);
            }
        }
    }
    if (stack_depth != 0u || collection_depth != 0u)
        return CHOS_HID_ERR_GLOBAL_STACK;
    if (!saw_input) return CHOS_HID_ERR_NO_INPUT;
    return CHOS_HID_OK;
}

const char *chos_hid_error_string(chos_hid_error_t error) {
    switch (error) {
        case CHOS_HID_OK: return "ok";
        case CHOS_HID_ERR_ARGUMENT: return "invalid descriptor or output argument";
        case CHOS_HID_ERR_TRUNCATED: return "truncated HID short item";
        case CHOS_HID_ERR_LONG_ITEM: return "truncated HID long item";
        case CHOS_HID_ERR_GLOBAL_STACK: return "invalid HID global/collection stack";
        case CHOS_HID_ERR_REPORT_ID: return "invalid HID report ID";
        case CHOS_HID_ERR_REPORT_SIZE: return "invalid HID report size/count";
        case CHOS_HID_ERR_REPORT_OVERFLOW: return "HID report bit count or field overflow";
        case CHOS_HID_ERR_TOO_MANY_FIELDS: return "HID descriptor has too many input fields";
        case CHOS_HID_ERR_LOCAL_USAGE: return "invalid or oversized HID local usages";
        case CHOS_HID_ERR_NO_INPUT: return "HID descriptor has no input reports";
        default: return "unknown HID descriptor error";
    }
}

int chos_hid_extract_bits(const uint8_t *payload, size_t payload_size,
                          uint32_t bit_offset, uint8_t bit_size,
                          uint32_t *value_out) {
    uint64_t total_bits;
    uint32_t value = 0u, i;
    if (payload == NULL || value_out == NULL || bit_size == 0u || bit_size > 32u) return 0;
    if (payload_size > UINT64_MAX / 8u) return 0;
    total_bits = (uint64_t)payload_size * 8u;
    if ((uint64_t)bit_offset > total_bits || bit_size > total_bits - bit_offset) return 0;
    for (i = 0u; i < bit_size; ++i) {
        uint64_t pos = (uint64_t)bit_offset + i;
        value |= (uint32_t)((payload[(size_t)(pos / 8u)] >> (pos % 8u)) & 1u) << i;
    }
    *value_out = value;
    return 1;
}

int32_t chos_hid_sign_extend(uint32_t value, uint8_t bit_size) {
    uint32_t sign;
    if (bit_size == 0u || bit_size > 32u) return 0;
    if (bit_size == 32u) return (int32_t)value;
    sign = UINT32_C(1) << (bit_size - 1u);
    value &= (UINT32_C(1) << bit_size) - 1u;
    return (int32_t)((value ^ sign) - sign);
}
