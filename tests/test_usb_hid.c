#include "chos/usb_hid.h"
#include <assert.h>
#include <stdio.h>

static const uint8_t keyboard_descriptor[] = {
    0x05,0x01, 0x09,0x06, 0xa1,0x01,
    0x05,0x07, 0x19,0xe0, 0x29,0xe7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x95,0x01, 0x75,0x08, 0x81,0x01,
    0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x65,
    0x05,0x07, 0x19,0x00, 0x29,0x65, 0x81,0x00, 0xc0
};
static const uint8_t mouse_descriptor[] = {
    0x05,0x01, 0x09,0x02, 0xa1,0x01,
    0x09,0x01, 0xa1,0x00,
    0x05,0x09, 0x19,0x01, 0x29,0x03, 0x15,0x00, 0x25,0x01,
    0x95,0x03, 0x75,0x01, 0x81,0x02,
    0x95,0x01, 0x75,0x05, 0x81,0x01,
    0x05,0x01, 0x09,0x30, 0x09,0x31, 0x09,0x38,
    0x15,0x81, 0x25,0x7f, 0x75,0x08, 0x95,0x03, 0x81,0x06,
    0xc0,0xc0
};
static const uint8_t report_id_descriptor[] = {
    0x05,0x01, 0x09,0x02, 0xa1,0x01, 0x85,0x02,
    0x09,0x30, 0x15,0x00, 0x25,0xff, 0x75,0x08, 0x95,0x01, 0x81,0x02, 0xc0
};

int main(void) {
    chos_hid_report_layout_t layout;
    chos_hid_error_t result;
    uint32_t value = 0;
    const uint8_t crossing[] = { 0xb2u, 0x01u, 0x80u };
    const uint8_t truncated_short[] = { 0x75u };
    const uint8_t truncated_long[] = { 0xfeu, 0x05u, 0x01u, 0x02u };
    const uint8_t pop_underflow[] = { 0xb4u };
    const uint8_t report_id_zero[] = { 0x85u, 0x00u };
    const uint8_t no_input[] = { 0x05u, 0x01u, 0x75u, 0x01u, 0x95u, 0x01u, 0x91u, 0x02u };
    const uint8_t unclosed_collection[] = { 0xa1u, 0x01u, 0x75u, 0x01u, 0x95u, 0x01u, 0x81u, 0x02u };
    const uint8_t unclosed_global_push[] = { 0xa4u, 0x05u, 0x01u, 0x75u, 0x01u, 0x95u, 0x01u, 0x81u, 0x02u };

    result = chos_hid_parse_report_descriptor(keyboard_descriptor, sizeof(keyboard_descriptor), &layout);
    assert(result == CHOS_HID_OK);
    assert(layout.has_keyboard == 1u);
    assert(layout.input_bits[0] == 64u);
    assert(layout.field_count == 9u); /* eight modifier bits and one six-key array */
    assert(layout.fields[0].usage_page == 7u && layout.fields[0].usage == 0xe0u);
    assert((layout.fields[0].flags & CHOS_HID_FIELD_VARIABLE) != 0u);
    assert(layout.fields[8].usage_min == 0u && layout.fields[8].usage_max == 0x65u);
    assert(layout.fields[8].count == 6u && layout.fields[8].bit_offset == 16u);

    result = chos_hid_parse_report_descriptor(mouse_descriptor, sizeof(mouse_descriptor), &layout);
    assert(result == CHOS_HID_OK);
    assert(layout.has_mouse == 1u && layout.has_pointer == 1u && layout.has_wheel == 1u);
    assert(layout.input_bits[0] == 32u);
    assert(layout.field_count == 6u);
    assert(layout.fields[3].usage == 0x30u && layout.fields[3].bit_offset == 8u);
    assert((layout.fields[3].flags & CHOS_HID_FIELD_RELATIVE) != 0u);
    assert((layout.fields[3].flags & CHOS_HID_FIELD_SIGNED) != 0u);

    result = chos_hid_parse_report_descriptor(report_id_descriptor, sizeof(report_id_descriptor), &layout);
    assert(result == CHOS_HID_OK);
    assert(layout.uses_report_ids == 1u && layout.input_bits[2] == 8u);
    assert(layout.fields[0].report_id == 2u);

    assert(chos_hid_extract_bits(crossing, sizeof(crossing), 5u, 12u, &value));
    assert(value == 0x0du);
    assert(!chos_hid_extract_bits(crossing, sizeof(crossing), 18u, 8u, &value));
    assert(!chos_hid_extract_bits(crossing, sizeof(crossing), 0u, 0u, &value));
    assert(chos_hid_sign_extend(0x7fu, 8u) == 127);
    assert(chos_hid_sign_extend(0x80u, 8u) == -128);
    assert(chos_hid_sign_extend(0xfffffffdu, 32u) == -3);

    assert(chos_hid_parse_report_descriptor(truncated_short, sizeof(truncated_short), &layout) == CHOS_HID_ERR_TRUNCATED);
    assert(chos_hid_parse_report_descriptor(truncated_long, sizeof(truncated_long), &layout) == CHOS_HID_ERR_LONG_ITEM);
    assert(chos_hid_parse_report_descriptor(pop_underflow, sizeof(pop_underflow), &layout) == CHOS_HID_ERR_GLOBAL_STACK);
    assert(chos_hid_parse_report_descriptor(report_id_zero, sizeof(report_id_zero), &layout) == CHOS_HID_ERR_REPORT_ID);
    assert(chos_hid_parse_report_descriptor(no_input, sizeof(no_input), &layout) == CHOS_HID_ERR_NO_INPUT);
    assert(chos_hid_parse_report_descriptor(unclosed_collection, sizeof(unclosed_collection), &layout) == CHOS_HID_ERR_GLOBAL_STACK);
    assert(chos_hid_parse_report_descriptor(unclosed_global_push, sizeof(unclosed_global_push), &layout) == CHOS_HID_ERR_GLOBAL_STACK);
    assert(chos_hid_sign_extend(0u, 33u) == 0);
    assert(chos_hid_parse_report_descriptor(NULL, 1u, &layout) == CHOS_HID_ERR_ARGUMENT);
    assert(chos_hid_error_string(CHOS_HID_OK)[0] == 'o');
    puts("USB HID descriptor parser: keyboard, mouse, report IDs, signed fields and malformed-boundary tests passed");
    return 0;
}
