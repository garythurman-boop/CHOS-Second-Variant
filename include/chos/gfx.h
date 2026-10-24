#ifndef CHOS_GFX_H
#define CHOS_GFX_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t *pixels;
    size_t width;
    size_t height;
    size_t stride_pixels;
} chos_surface_t;

#define CHOS_COLOR_RGB(r, g, b) (0xff000000u | (((uint32_t)(r) & 0xffu) << 16) | (((uint32_t)(g) & 0xffu) << 8) | ((uint32_t)(b) & 0xffu))
#define CHOS_COLOR_RGBA(r, g, b, a) ((((uint32_t)(a) & 0xffu) << 24) | (((uint32_t)(r) & 0xffu) << 16) | (((uint32_t)(g) & 0xffu) << 8) | ((uint32_t)(b) & 0xffu))

int chos_surface_valid(const chos_surface_t *surface);
/* A single active clip rectangle for the immediate-mode renderer. Clip is reset explicitly. */
void chos_gfx_set_clip(chos_surface_t *surface, int32_t x, int32_t y, int32_t width, int32_t height);
void chos_gfx_reset_clip(chos_surface_t *surface);
void chos_gfx_clear(chos_surface_t *surface, uint32_t color);
void chos_gfx_pixel(chos_surface_t *surface, int32_t x, int32_t y, uint32_t color);
void chos_gfx_fill_rect(chos_surface_t *surface, int32_t x, int32_t y,
                        int32_t width, int32_t height, uint32_t color);
void chos_gfx_outline_rect(chos_surface_t *surface, int32_t x, int32_t y,
                           int32_t width, int32_t height, uint32_t color);
void chos_gfx_hline(chos_surface_t *surface, int32_t x, int32_t y,
                    int32_t width, uint32_t color);
void chos_gfx_vline(chos_surface_t *surface, int32_t x, int32_t y,
                    int32_t height, uint32_t color);
void chos_gfx_gradient_v(chos_surface_t *surface, int32_t x, int32_t y,
                         int32_t width, int32_t height,
                         uint32_t top_color, uint32_t bottom_color);
void chos_gfx_char(chos_surface_t *surface, int32_t x, int32_t y,
                   char ch, uint32_t fg, uint32_t bg, int opaque);
void chos_gfx_char_scaled(chos_surface_t *surface, int32_t x, int32_t y,
                          char ch, uint32_t fg, uint32_t bg, int opaque, unsigned scale);
void chos_gfx_text(chos_surface_t *surface, int32_t x, int32_t y,
                   const char *text, uint32_t fg, uint32_t bg, int opaque);
void chos_gfx_text_scaled(chos_surface_t *surface, int32_t x, int32_t y,
                          const char *text, uint32_t fg, uint32_t bg, int opaque, unsigned scale);
/* Proportional, antialiased DejaVu Sans UI font. Glyph coverage is embedded as
 * compact 4-bit bitmap data; no runtime font parsing or allocation is needed. */
#define CHOS_GFX_UI_LINE_HEIGHT 15u
#define CHOS_GFX_UI_FONT_HEIGHT 14u
unsigned chos_gfx_ui_char_width(char ch, int bold);
int32_t chos_gfx_ui_text_width(const char *text, int bold);
void chos_gfx_ui_text(chos_surface_t *surface, int32_t x, int32_t y,
                      const char *text, uint32_t fg, int bold);
void chos_gfx_ui_text_scaled(chos_surface_t *surface, int32_t x, int32_t y,
                             const char *text, uint32_t fg, int bold, unsigned scale);
void chos_gfx_bevel(chos_surface_t *surface, int32_t x, int32_t y,
                    int32_t width, int32_t height, uint32_t light,
                    uint32_t dark, int raised);

#endif
