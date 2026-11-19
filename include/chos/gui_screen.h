#ifndef CHOS_GUI_SCREEN_H
#define CHOS_GUI_SCREEN_H
#include <stdint.h>

#define CHOS_GUI_SCREEN_VERSION 1u
/* Numeric pixel layout 0xXXRRGGBB. The display has no user-visible alpha. */
#define CHOS_GUI_SCREEN_XRGB8888 1u

/* Read-only physical-pixel snapshot of the active desktop. Rectangles share
 * the same origin; the available rectangle excludes the bottom taskbar.
 * Desktop scale is an exact ratio, not a physical monitor DPI claim. */
typedef struct {
    uint32_t size_bytes;
    uint32_t version;
    uint32_t pixel_format;
    uint32_t reserved;
    int32_t x, y;
    uint32_t width, height;
    int32_t available_x, available_y;
    uint32_t available_width, available_height;
    uint32_t bits_per_pixel, color_bits;
    uint32_t scale_numerator, scale_denominator;
} chos_gui_screen_info_t;

#if defined(__cplusplus)
static_assert(sizeof(chos_gui_screen_info_t) == 64, "GUI screen ABI changed");
#else
_Static_assert(sizeof(chos_gui_screen_info_t) == 64, "GUI screen ABI changed");
#endif
#endif
