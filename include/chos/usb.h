#ifndef CHOS_USB_H
#define CHOS_USB_H
#include "chos/usb_hid.h"
#define CHOS_USB_MAX_HID 4u
#define CHOS_USB_REPORT_BYTES 256u
typedef struct {
    uint8_t interface, endpoint, interval, boot_protocol;
    uint16_t packet_bytes, report_bytes;
} chos_usb_hid_interface_t;
/* Strictly bounded configuration parser. Hubs, alternate settings, streams and
 * oversized/high-bandwidth endpoints are rejected, never guessed at. */
int chos_usb_parse_config(const uint8_t *data, size_t size,
                         chos_usb_hid_interface_t out[CHOS_USB_MAX_HID], size_t *count);
typedef struct {
    uint8_t keys[256][32]; /* per-report-ID bitsets, so split reports cannot release each other */
    uint8_t buttons[8][32];
} chos_usb_input_state_t;
int chos_usb_decode_report(const chos_hid_report_layout_t *layout,
                          chos_usb_input_state_t *state, const uint8_t *data, size_t size);
void chos_usb_release_input(chos_usb_input_state_t *state);
#endif
