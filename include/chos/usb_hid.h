#ifndef CHOS_USB_HID_H
#define CHOS_USB_HID_H

#include <stddef.h>
#include <stdint.h>

/* Bounded, allocation-free USB HID report-descriptor parser. The layout is
 * useful to a transport driver after USB control transfers have fetched a
 * report descriptor. It intentionally does not claim to enumerate USB devices. */
#define CHOS_HID_MAX_FIELDS 96u
#define CHOS_HID_GLOBAL_STACK 8u
#define CHOS_HID_MAX_REPORT_ID 255u
#define CHOS_HID_FIELD_CONSTANT 0x01u
#define CHOS_HID_FIELD_VARIABLE 0x02u
#define CHOS_HID_FIELD_RELATIVE 0x04u
#define CHOS_HID_FIELD_SIGNED   0x08u

typedef enum {
    CHOS_HID_OK = 0,
    CHOS_HID_ERR_ARGUMENT,
    CHOS_HID_ERR_TRUNCATED,
    CHOS_HID_ERR_LONG_ITEM,
    CHOS_HID_ERR_GLOBAL_STACK,
    CHOS_HID_ERR_REPORT_ID,
    CHOS_HID_ERR_REPORT_SIZE,
    CHOS_HID_ERR_REPORT_OVERFLOW,
    CHOS_HID_ERR_TOO_MANY_FIELDS,
    CHOS_HID_ERR_LOCAL_USAGE,
    CHOS_HID_ERR_NO_INPUT
} chos_hid_error_t;

typedef struct {
    uint8_t report_id;
    uint16_t usage_page;
    uint16_t usage;
    uint16_t usage_min;
    uint16_t usage_max;
    uint16_t bit_offset; /* offset in report payload, excluding Report ID byte */
    uint8_t bit_size;
    uint8_t count;
    uint8_t flags;
} chos_hid_field_t;

typedef struct {
    chos_hid_field_t fields[CHOS_HID_MAX_FIELDS];
    size_t field_count;
    uint32_t input_bits[256];
    uint32_t output_bits[256];
    uint32_t feature_bits[256];
    uint8_t uses_report_ids;
    uint8_t has_keyboard;
    uint8_t has_pointer;
    uint8_t has_mouse;
    uint8_t has_wheel;
} chos_hid_report_layout_t;

chos_hid_error_t chos_hid_parse_report_descriptor(const uint8_t *descriptor,
                                                   size_t descriptor_size,
                                                   chos_hid_report_layout_t *layout);
const char *chos_hid_error_string(chos_hid_error_t error);
/* Extract a little-endian bitfield safely. Bits may cross byte boundaries;
 * bit_size must be 1..32 and bit_offset is relative to the payload start. */
int chos_hid_extract_bits(const uint8_t *payload, size_t payload_size,
                          uint32_t bit_offset, uint8_t bit_size,
                          uint32_t *value_out);
int32_t chos_hid_sign_extend(uint32_t value, uint8_t bit_size);

#endif
