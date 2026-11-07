#ifndef CHOS_GUI_SURFACE_H
#define CHOS_GUI_SURFACE_H
#include "chos/gui_protocol.h"

/* Opaque XRGB8888 software frames. Tiles copy values, never user pointers.
 * BEGIN returns a positive server frame ID. TILE/PRESENT put it in request_id.
 * Every tile of a frame must be uploaded before PRESENT; duplicate tiles replace
 * their prior values. Presentation swaps complete buffers. BEGIN may abandon an
 * incomplete frame. DROP/CLEAR/close discard both visible and pending frames.
 * EAGAIN means an incomplete frame or a compositor read pin; callers must yield
 * or await their next event before retrying, with an application deadline.
 * Coordinates use the existing logical client space. The server clips to the
 * client area and uses the desktop scale. Alpha bits are ignored/forced opaque.
 */
#define CHOS_GUI_SURFACE_MAX_WIDTH 1024u
#define CHOS_GUI_SURFACE_MAX_HEIGHT 768u
#define CHOS_GUI_SURFACE_TILE_PIXELS 128u
typedef struct {
    uint32_t window_id;
    int32_t x, y;
    uint32_t width, height;
    uint32_t reserved;
} chos_gui_surface_begin_t;
typedef struct {
    uint32_t window_id;
    /* Row-major tile index: ceil(width/128) tiles per row. No row straddles. */
    uint32_t tile_index, pixel_count, reserved;
    uint32_t pixels[CHOS_GUI_SURFACE_TILE_PIXELS];
} chos_gui_surface_tile_t;
typedef struct {
    chos_gui_message_header_t header;
    union {
        chos_gui_surface_begin_t begin;
        chos_gui_surface_tile_t tile;
        struct { uint32_t window_id, reserved; } window;
    } payload;
} chos_gui_surface_message_t;
/* The event syscall keeps its original fixed envelope. Old installed clients
 * must not need a larger event buffer merely to use a newer kernel. */
_Static_assert(sizeof(chos_gui_message_t)==184u,"Legacy GUI message/event ABI changed");
_Static_assert(sizeof(chos_gui_surface_tile_t)==528u,"Surface tile ABI changed");
_Static_assert(sizeof(chos_gui_surface_message_t)==552u,"Surface transport ABI changed");
_Static_assert(sizeof(chos_gui_surface_message_t)<=CHOS_GUI_MAX_MESSAGE_BYTES,"Surface exceeds transport cap");
#endif
