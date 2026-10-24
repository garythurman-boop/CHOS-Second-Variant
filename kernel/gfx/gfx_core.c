#include <stdint.h>
#include <stddef.h>
#include "chos/gfx.h"
#include "font8x12.h"
#include "font_ui_dejavu.h"

static chos_surface_t *clip_surface;
static int clip_enabled;
static int32_t clip_x0, clip_y0, clip_x1, clip_y1;

static uint8_t channel(uint32_t color, unsigned shift) {
    return (uint8_t)((color >> shift) & 0xffu);
}

int chos_surface_valid(const chos_surface_t *surface) {
    if (surface == NULL || surface->pixels == NULL || surface->width == 0u ||
        surface->height == 0u || surface->stride_pixels < surface->width) return 0;
    if ((uint64_t)surface->stride_pixels * (uint64_t)surface->height > (uint64_t)SIZE_MAX / sizeof(uint32_t)) return 0;
    return 1;
}

void chos_gfx_set_clip(chos_surface_t *surface, int32_t x, int32_t y, int32_t width, int32_t height) {
    int64_t x1, y1;
    if (!chos_surface_valid(surface) || width <= 0 || height <= 0) {
        clip_surface = surface; clip_enabled = 1;
        clip_x0 = clip_y0 = clip_x1 = clip_y1 = 0;
        return;
    }
    x1 = (int64_t)x + width; y1 = (int64_t)y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int64_t)surface->width) x1 = (int64_t)surface->width;
    if (y1 > (int64_t)surface->height) y1 = (int64_t)surface->height;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    clip_surface = surface; clip_enabled = 1;
    clip_x0 = x; clip_y0 = y; clip_x1 = (int32_t)x1; clip_y1 = (int32_t)y1;
}

void chos_gfx_reset_clip(chos_surface_t *surface) {
    if (clip_surface == surface) {
        clip_enabled = 0; clip_surface = NULL;
        clip_x0 = clip_y0 = clip_x1 = clip_y1 = 0;
    }
}

static int clip_matches(const chos_surface_t *surface) {
    return clip_enabled && clip_surface == surface;
}

void chos_gfx_clear(chos_surface_t *surface, uint32_t color) {
    size_t y, x;
    if (!chos_surface_valid(surface)) return;
    for (y = 0u; y < surface->height; ++y) {
        uint32_t *row = surface->pixels + y * surface->stride_pixels;
        for (x = 0u; x < surface->width; ++x) row[x] = color;
    }
}

void chos_gfx_pixel(chos_surface_t *surface, int32_t x, int32_t y, uint32_t color) {
    if (!chos_surface_valid(surface) || x < 0 || y < 0 ||
        (size_t)x >= surface->width || (size_t)y >= surface->height) return;
    if (clip_matches(surface) && (x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1)) return;
    surface->pixels[(size_t)y * surface->stride_pixels + (size_t)x] = color;
}

void chos_gfx_fill_rect(chos_surface_t *surface, int32_t x, int32_t y,
                        int32_t width, int32_t height, uint32_t color) {
    int64_t x0, y0, x1, y1, yy;
    if (!chos_surface_valid(surface) || width <= 0 || height <= 0) return;
    x0 = x; y0 = y; x1 = (int64_t)x + width; y1 = (int64_t)y + height;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int64_t)surface->width) x1 = (int64_t)surface->width;
    if (y1 > (int64_t)surface->height) y1 = (int64_t)surface->height;
    if (clip_matches(surface)) {
        if (x0 < clip_x0) x0 = clip_x0;
        if (y0 < clip_y0) y0 = clip_y0;
        if (x1 > clip_x1) x1 = clip_x1;
        if (y1 > clip_y1) y1 = clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) return;
    for (yy = y0; yy < y1; ++yy) {
        uint32_t *row = surface->pixels + (size_t)yy * surface->stride_pixels;
        size_t xx;
        for (xx = (size_t)x0; xx < (size_t)x1; ++xx) row[xx] = color;
    }
}

void chos_gfx_hline(chos_surface_t *surface, int32_t x, int32_t y,
                    int32_t width, uint32_t color) {
    chos_gfx_fill_rect(surface, x, y, width, 1, color);
}

void chos_gfx_vline(chos_surface_t *surface, int32_t x, int32_t y,
                    int32_t height, uint32_t color) {
    chos_gfx_fill_rect(surface, x, y, 1, height, color);
}

void chos_gfx_outline_rect(chos_surface_t *surface, int32_t x, int32_t y,
                           int32_t width, int32_t height, uint32_t color) {
    if (width <= 0 || height <= 0) return;
    chos_gfx_hline(surface, x, y, width, color);
    if (height > 1) chos_gfx_hline(surface, x, (int32_t)((int64_t)y + height - 1), width, color);
    chos_gfx_vline(surface, x, y, height, color);
    if (width > 1) chos_gfx_vline(surface, (int32_t)((int64_t)x + width - 1), y, height, color);
}

void chos_gfx_gradient_v(chos_surface_t *surface, int32_t x, int32_t y,
                         int32_t width, int32_t height,
                         uint32_t top_color, uint32_t bottom_color) {
    int32_t row;
    if (!chos_surface_valid(surface) || width <= 0 || height <= 0) return;
    for (row = 0; row < height; ++row) {
        uint32_t r, g, b, a, color;
        uint32_t t = height <= 1 ? 0u : (uint32_t)row * 65535u / (uint32_t)(height - 1);
#define CHOS_MIX(c0, c1, sh) ((uint32_t)(((uint64_t)channel((c0), (sh)) * (65535u - t) + (uint64_t)channel((c1), (sh)) * t + 32767u) / 65535u))
        r = CHOS_MIX(top_color, bottom_color, 16u);
        g = CHOS_MIX(top_color, bottom_color, 8u);
        b = CHOS_MIX(top_color, bottom_color, 0u);
        a = CHOS_MIX(top_color, bottom_color, 24u);
#undef CHOS_MIX
        color = (a << 24) | (r << 16) | (g << 8) | b;
        chos_gfx_hline(surface, x, (int32_t)((int64_t)y + row), width, color);
    }
}

void chos_gfx_char(chos_surface_t *surface, int32_t x, int32_t y,
                   char ch, uint32_t fg, uint32_t bg, int opaque) {
    unsigned code = (unsigned char)ch;
    const uint8_t *glyph;
    unsigned row, col;
    if (!chos_surface_valid(surface)) return;
    if (code < 32u || code > 126u) code = (unsigned)'?';
    glyph = chos_font8x12[code - 32u];
    for (row = 0u; row < 12u; ++row) {
        for (col = 0u; col < 8u; ++col) {
            int set = (glyph[row] & (uint8_t)(0x80u >> col)) != 0u;
            if (set) chos_gfx_pixel(surface, (int32_t)((int64_t)x + col), (int32_t)((int64_t)y + row), fg);
            else if (opaque) chos_gfx_pixel(surface, (int32_t)((int64_t)x + col), (int32_t)((int64_t)y + row), bg);
        }
    }
}

void chos_gfx_char_scaled(chos_surface_t *surface, int32_t x, int32_t y,
                          char ch, uint32_t fg, uint32_t bg, int opaque, unsigned scale) {
    unsigned code = (unsigned char)ch;
    unsigned row, col;
    if (scale <= 1u) { chos_gfx_char(surface, x, y, ch, fg, bg, opaque); return; }
    if (!chos_surface_valid(surface)) return;
    if (code < 32u || code > 126u) code = (unsigned)'?';
    for (row = 0u; row < 12u; ++row) {
        uint8_t bits = chos_font8x12[code - 32u][row];
        for (col = 0u; col < 8u; ++col) {
            int set = (bits & (uint8_t)(0x80u >> col)) != 0u;
            if (set || opaque) {
                int64_t px = (int64_t)x + (int64_t)col * scale;
                int64_t py = (int64_t)y + (int64_t)row * scale;
                if (px < INT32_MIN || px > INT32_MAX || py < INT32_MIN || py > INT32_MAX) continue;
                chos_gfx_fill_rect(surface, (int32_t)px, (int32_t)py, (int32_t)scale, (int32_t)scale,
                                   set ? fg : bg);
            }
        }
    }
}

void chos_gfx_text(chos_surface_t *surface, int32_t x, int32_t y,
                   const char *text, uint32_t fg, uint32_t bg, int opaque) {
    int32_t origin_x = x;
    if (surface == NULL || text == NULL) return;
    while (*text != '\0') {
        if (*text == '\n') { x = origin_x; y = (int32_t)((int64_t)y + 13); ++text; continue; }
        if (*text == '\r') { ++text; continue; }
        chos_gfx_char(surface, x, y, *text++, fg, bg, opaque);
        if ((int64_t)x + 8 > INT32_MAX) break;
        x += 8;
    }
}


void chos_gfx_text_scaled(chos_surface_t *surface, int32_t x, int32_t y,
                          const char *text, uint32_t fg, uint32_t bg, int opaque, unsigned scale) {
    int32_t origin_x = x;
    if (scale <= 1u) { chos_gfx_text(surface, x, y, text, fg, bg, opaque); return; }
    if (!chos_surface_valid(surface) || text == NULL) return;
    while (*text != '\0') {
        if (*text == '\n') {
            x = origin_x;
            if ((int64_t)y + (int64_t)13 * scale > INT32_MAX) break;
            y += (int32_t)(13u * scale);
            ++text;
            continue;
        }
        if (*text == '\r') { ++text; continue; }
        chos_gfx_char_scaled(surface, x, y, *text++, fg, bg, opaque, scale);
        if ((int64_t)x + (int64_t)8 * scale > INT32_MAX) break;
        x += (int32_t)(8u * scale);
    }
}

static uint32_t blend_text_color(uint32_t foreground, uint32_t background, unsigned coverage) {
    unsigned inverse = 15u - coverage;
    unsigned r = ((((foreground >> 16) & 0xffu) * coverage) + (((background >> 16) & 0xffu) * inverse) + 7u) / 15u;
    unsigned g = ((((foreground >> 8) & 0xffu) * coverage) + (((background >> 8) & 0xffu) * inverse) + 7u) / 15u;
    unsigned b = (((foreground & 0xffu) * coverage) + ((background & 0xffu) * inverse) + 7u) / 15u;
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static unsigned ui_glyph_index(char ch) {
    unsigned code = (unsigned char)ch;
    if (code < CHOS_UI_GLYPH_FIRST || code >= CHOS_UI_GLYPH_FIRST + CHOS_UI_GLYPH_COUNT)
        code = (unsigned)'?';
    return code - CHOS_UI_GLYPH_FIRST;
}

unsigned chos_gfx_ui_char_width(char ch, int bold) {
    unsigned index = ui_glyph_index(ch);
    return bold ? chos_ui_bold_advance[index] : chos_ui_regular_advance[index];
}

int32_t chos_gfx_ui_text_width(const char *text, int bold) {
    int64_t current = 0;
    int64_t maximum = 0;
    if (text == NULL) return 0;
    while (*text != '\0') {
        if (*text == '\n') {
            if (current > maximum) maximum = current;
            current = 0;
        } else if (*text != '\r') {
            current += (int64_t)chos_gfx_ui_char_width(*text, bold);
            if (current > INT32_MAX) return INT32_MAX;
        }
        ++text;
    }
    if (current > maximum) maximum = current;
    return maximum > INT32_MAX ? INT32_MAX : (int32_t)maximum;
}

static void ui_glyph(chos_surface_t *surface, int32_t x, int32_t y, char ch,
                     uint32_t foreground, int bold) {
    unsigned index = ui_glyph_index(ch);
    unsigned row, col, advance;
    const uint8_t (*bitmap)[CHOS_UI_FONT_ROW_BYTES] = bold
        ? chos_ui_bold_bitmap[index] : chos_ui_regular_bitmap[index];
    advance = chos_gfx_ui_char_width(ch, bold);
    for (row = 0u; row < CHOS_UI_FONT_HEIGHT; ++row) {
        for (col = 0u; col < advance && col < CHOS_UI_FONT_WIDTH; ++col) {
            uint8_t packed = bitmap[row][col / 2u];
            unsigned coverage = (col & 1u) == 0u ? (unsigned)(packed >> 4) : (unsigned)(packed & 0x0fu);
            int64_t px = (int64_t)x + col;
            int64_t py = (int64_t)y + row;
            if (coverage == 0u || px < INT32_MIN || px > INT32_MAX || py < INT32_MIN || py > INT32_MAX) continue;
            if (!chos_surface_valid(surface) || px < 0 || py < 0 || (uint64_t)px >= surface->width || (uint64_t)py >= surface->height) continue;
            if (clip_matches(surface) && (px < clip_x0 || py < clip_y0 || px >= clip_x1 || py >= clip_y1)) continue;
            {
                uint32_t *pixel = &surface->pixels[(size_t)py * surface->stride_pixels + (size_t)px];
                *pixel = coverage == 15u ? foreground : blend_text_color(foreground, *pixel, coverage);
            }
        }
    }
}

void chos_gfx_ui_text(chos_surface_t *surface, int32_t x, int32_t y,
                      const char *text, uint32_t fg, int bold) {
    int32_t origin_x = x;
    if (!chos_surface_valid(surface) || text == NULL) return;
    while (*text != '\0') {
        if (*text == '\n') { x = origin_x; y = (int32_t)((int64_t)y + CHOS_GFX_UI_LINE_HEIGHT); ++text; continue; }
        if (*text == '\r') { ++text; continue; }
        ui_glyph(surface, x, y, *text, fg, bold);
        if ((int64_t)x + CHOS_UI_FONT_WIDTH > INT32_MAX) break;
        x += (int32_t)chos_gfx_ui_char_width(*text, bold);
        ++text;
    }
}

static void ui_glyph_scaled(chos_surface_t *surface, int32_t x, int32_t y, char ch,
                            uint32_t foreground, int bold, unsigned scale) {
    unsigned index = ui_glyph_index(ch);
    unsigned row, col, advance;
    const uint8_t (*bitmap)[CHOS_UI_FONT_ROW_BYTES] = bold
        ? chos_ui_bold_bitmap[index] : chos_ui_regular_bitmap[index];
    advance = chos_gfx_ui_char_width(ch, bold);
    if (scale <= 1u) { ui_glyph(surface, x, y, ch, foreground, bold); return; }
    for (row = 0u; row < CHOS_UI_FONT_HEIGHT; ++row) {
        for (col = 0u; col < advance && col < CHOS_UI_FONT_WIDTH; ++col) {
            uint8_t packed = bitmap[row][col / 2u];
            unsigned coverage = (col & 1u) == 0u ? (unsigned)(packed >> 4) : (unsigned)(packed & 0x0fu);
            int64_t px = (int64_t)x + (int64_t)col * scale;
            int64_t py = (int64_t)y + (int64_t)row * scale;
            unsigned sy, sx;
            if (coverage == 0u || px < INT32_MIN || px > INT32_MAX || py < INT32_MIN || py > INT32_MAX) continue;
            if (!chos_surface_valid(surface)) return;
            if (coverage == 15u) {
                chos_gfx_fill_rect(surface, (int32_t)px, (int32_t)py,
                                   (int32_t)scale, (int32_t)scale, foreground);
                continue;
            }
            for (sy = 0u; sy < scale; ++sy) {
                for (sx = 0u; sx < scale; ++sx) {
                    int64_t dx = px + sx, dy = py + sy;
                    uint32_t *pixel;
                    if (dx < 0 || dy < 0 || (uint64_t)dx >= surface->width || (uint64_t)dy >= surface->height) continue;
                    if (clip_matches(surface) && (dx < clip_x0 || dy < clip_y0 || dx >= clip_x1 || dy >= clip_y1)) continue;
                    pixel = &surface->pixels[(size_t)dy * surface->stride_pixels + (size_t)dx];
                    *pixel = blend_text_color(foreground, *pixel, coverage);
                }
            }
        }
    }
}

void chos_gfx_ui_text_scaled(chos_surface_t *surface, int32_t x, int32_t y,
                             const char *text, uint32_t fg, int bold, unsigned scale) {
    int32_t origin_x = x;
    if (scale <= 1u) { chos_gfx_ui_text(surface, x, y, text, fg, bold); return; }
    if (!chos_surface_valid(surface) || text == NULL) return;
    while (*text != '\0') {
        if (*text == '\n') {
            x = origin_x;
            if ((int64_t)y + (int64_t)CHOS_GFX_UI_LINE_HEIGHT * scale > INT32_MAX) break;
            y += (int32_t)(CHOS_GFX_UI_LINE_HEIGHT * scale);
            ++text;
            continue;
        }
        if (*text == '\r') { ++text; continue; }
        ui_glyph_scaled(surface, x, y, *text, fg, bold, scale);
        if ((int64_t)x + (int64_t)CHOS_UI_FONT_WIDTH * scale > INT32_MAX) break;
        x += (int32_t)(chos_gfx_ui_char_width(*text, bold) * scale);
        ++text;
    }
}

void chos_gfx_bevel(chos_surface_t *surface, int32_t x, int32_t y,
                    int32_t width, int32_t height, uint32_t light,
                    uint32_t dark, int raised) {
    uint32_t top_left = raised ? light : dark;
    uint32_t bottom_right = raised ? dark : light;
    if (width <= 0 || height <= 0) return;
    chos_gfx_hline(surface, x, y, width, top_left);
    chos_gfx_vline(surface, x, y, height, top_left);
    chos_gfx_hline(surface, x, (int32_t)((int64_t)y + height - 1), width, bottom_right);
    chos_gfx_vline(surface, (int32_t)((int64_t)x + width - 1), y, height, bottom_right);
}
