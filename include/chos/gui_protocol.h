#ifndef CHOS_GUI_PROTOCOL_H
#define CHOS_GUI_PROTOCOL_H

/*
 * CHOS GUI application protocol: 0.10.x experimental ABI.
 *
 * This header defines the versioned fixed-width EL0 GUI service envelope used
 * by syscalls 11 and 12. The ABI is experimental: callers are isolated by
 * process ownership, exact message lengths are checked by the kernel, and each
 * process is limited to one window with bounded drawing/event queues.
 * Compatibility is not guaranteed until a future stable OS release.
 */
#include <stddef.h>
#include <stdint.h>

#define CHOS_GUI_ABI_VERSION 1u
#define CHOS_GUI_MAX_TEXT 128u
#define CHOS_GUI_MAX_MESSAGE_BYTES 560u
#define CHOS_GUI_WINDOW_ID_NONE 0u
#define CHOS_GUI_EVENT_KEY_DOWN       (1u << 0)
#define CHOS_GUI_EVENT_KEY_UP         (1u << 1)
#define CHOS_GUI_EVENT_KEY_REPEAT     (1u << 2)
#define CHOS_GUI_EVENT_POINTER_MOVE   (1u << 0)
#define CHOS_GUI_EVENT_POINTER_DOWN   (1u << 1)
#define CHOS_GUI_EVENT_POINTER_UP     (1u << 2)
#define CHOS_GUI_EVENT_POINTER_SCROLL (1u << 3)
#define CHOS_GUI_EVENT_FOCUS_GAINED   (1u << 0)
#define CHOS_GUI_EVENT_FOCUS_LOST     (1u << 1)
#define CHOS_GUI_EVENT_QUEUE_CAPACITY 16u

typedef enum {
    CHOS_GUI_MSG_CREATE_WINDOW = 1,
    CHOS_GUI_MSG_DESTROY_WINDOW = 2,
    CHOS_GUI_MSG_SHOW_WINDOW = 3,
    CHOS_GUI_MSG_SET_TITLE = 4,
    CHOS_GUI_MSG_FILL_RECT = 5,
    CHOS_GUI_MSG_DRAW_TEXT = 6,
    CHOS_GUI_MSG_REPLY = 7,
    CHOS_GUI_MSG_CLEAR = 8,
    CHOS_GUI_MSG_SET_CARET = 9,
    CHOS_GUI_MSG_KEY_EVENT = 16,
    CHOS_GUI_MSG_POINTER_EVENT = 17,
    CHOS_GUI_MSG_CLOSE_REQUEST = 18,
    CHOS_GUI_MSG_FOCUS_EVENT = 19
} chos_gui_message_type_t;

typedef struct {
    uint16_t version;
    uint16_t type;
    uint32_t size_bytes;
    uint32_t flags;
    uint32_t reserved;
    uint64_t request_id;
} chos_gui_message_header_t;

typedef struct {
    uint32_t window_id;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t background_argb;
    uint32_t options;
} chos_gui_create_window_t;

typedef struct {
    uint32_t window_id;
    uint32_t argb;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} chos_gui_fill_rect_t;

typedef struct {
    uint32_t window_id;
    int32_t x;
    int32_t y;
    uint32_t argb;
    uint16_t text_length;
    uint16_t font_id;
    char text[CHOS_GUI_MAX_TEXT];
} chos_gui_draw_text_t;

typedef struct {
    uint32_t window_id;
    uint32_t event_flags;
    uint32_t keycode;
    uint32_t modifiers;
} chos_gui_key_event_t;

typedef struct {
    uint32_t window_id;
    int32_t x;
    int32_t y;
    int32_t delta_x;
    int32_t delta_y;
    uint32_t buttons;
    uint32_t event_flags;
} chos_gui_pointer_event_t;

typedef struct {
    uint32_t window_id;
    uint32_t background_argb;
} chos_gui_clear_t;

/* Client coordinates; the server owns focus, blinking, clipping and lifetime. */
typedef struct {
    uint32_t window_id;
    int32_t x, y;
    uint32_t width, height, argb, enabled;
    /* Optional UI-font prefix: x is the text origin; server measures glyphs. */
    uint32_t text_length;
    char text[CHOS_GUI_MAX_TEXT];
} chos_gui_caret_t;

typedef struct {
    uint32_t status;
    uint32_t object_id;
    uint32_t detail;
    uint32_t reserved;
} chos_gui_reply_t;

typedef struct {
    chos_gui_message_header_t header;
    union {
        chos_gui_create_window_t create_window;
        chos_gui_fill_rect_t fill_rect;
        chos_gui_draw_text_t draw_text;
        chos_gui_key_event_t key_event;
        chos_gui_pointer_event_t pointer_event;
        chos_gui_clear_t clear;
        chos_gui_caret_t caret;
        chos_gui_reply_t reply;
        struct { uint32_t window_id; uint32_t reserved; } window;
        struct { uint32_t window_id; uint16_t title_length; uint16_t reserved; char title[CHOS_GUI_MAX_TEXT]; } set_title;
    } payload;
} chos_gui_message_t;

/* Transport implementations must reject all malformed or unsupported lengths. */
static inline int chos_gui_message_header_valid(const chos_gui_message_header_t *header,
                                                 size_t supplied_bytes) {
    if (header == NULL || supplied_bytes < sizeof(*header)) return 0;
    if (header->version != CHOS_GUI_ABI_VERSION) return 0;
    if (!((header->type >= CHOS_GUI_MSG_CREATE_WINDOW && header->type <= CHOS_GUI_MSG_SET_CARET) ||
          (header->type >= CHOS_GUI_MSG_KEY_EVENT && header->type <= CHOS_GUI_MSG_FOCUS_EVENT))) return 0;
    if (header->size_bytes < sizeof(*header) ||
        header->size_bytes > CHOS_GUI_MAX_MESSAGE_BYTES ||
        header->size_bytes > supplied_bytes) return 0;
    return header->reserved == 0u;
}

_Static_assert(sizeof(chos_gui_message_header_t) == 24u, "GUI ABI message header layout changed");
_Static_assert(sizeof(chos_gui_create_window_t) == 28u, "GUI ABI create-window layout changed");
_Static_assert(sizeof(chos_gui_fill_rect_t) == 24u, "GUI ABI fill-rect layout changed");
_Static_assert(sizeof(chos_gui_reply_t) == 16u, "GUI ABI reply layout changed");
_Static_assert(sizeof(chos_gui_message_t) <= CHOS_GUI_MAX_MESSAGE_BYTES, "GUI ABI message exceeds transport cap");

#endif /* CHOS_GUI_PROTOCOL_H */
