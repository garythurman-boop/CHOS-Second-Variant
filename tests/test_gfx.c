#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/gfx.h"

static uint32_t pixels[12u * 10u];
static uint32_t font_pixels[160u * 24u];

int main(void) {
    chos_surface_t s = { pixels, 10u, 10u, 12u };
    size_t i, nonzero;
    assert(chos_surface_valid(&s));
    chos_gfx_clear(&s, 0u);
    for (i = 0u; i < sizeof(pixels)/sizeof(pixels[0]); ++i) assert(pixels[i] == 0u);
    chos_gfx_fill_rect(&s, -2, -2, 5, 5, 0xff123456u);
    assert(pixels[0] == 0xff123456u && pixels[2u * 12u + 2u] == 0xff123456u);
    assert(pixels[0u * 12u + 3u] == 0u && pixels[3u * 12u] == 0u);
    chos_gfx_fill_rect(&s, 7, 7, 99, 99, 0xffaabbccu);
    assert(pixels[9u * 12u + 9u] == 0xffaabbccu);
    assert(pixels[9u * 12u + 10u] == 0u); /* stride padding remains untouched */
    chos_gfx_clear(&s, CHOS_COLOR_RGB(10, 20, 30));
    chos_gfx_outline_rect(&s, 1, 1, 8, 8, 0xffffffffu);
    assert(pixels[1u * 12u + 1u] == 0xffffffffu);
    assert(pixels[4u * 12u + 4u] == CHOS_COLOR_RGB(10,20,30));
    chos_gfx_gradient_v(&s, 0, 0, 10, 10, 0xff000000u, 0xffffffffu);
    assert(pixels[0] == 0xff000000u && pixels[9u * 12u] == 0xffffffffu);
    /* Window contents must not paint outside their clip rectangle. */
    chos_gfx_clear(&s, 0u);
    chos_gfx_set_clip(&s, 3, 2, 4, 3);
    chos_gfx_fill_rect(&s, 0, 0, 10, 10, 0xffabcdefu);
    assert(pixels[2u * 12u + 3u] == 0xffabcdefu);
    assert(pixels[4u * 12u + 6u] == 0xffabcdefu);
    assert(pixels[1u * 12u + 3u] == 0u);
    assert(pixels[2u * 12u + 2u] == 0u);
    assert(pixels[2u * 12u + 7u] == 0u);
    chos_gfx_reset_clip(&s);
    chos_gfx_fill_rect(&s, 0, 0, 1, 1, 0xff13579bu);
    assert(pixels[0] == 0xff13579bu);
    chos_gfx_clear(&s, 0u);
    chos_gfx_text(&s, 0, 0, "CHOS 09", 0xffffffffu, 0u, 0);
    nonzero = 0u;
    for (i = 0u; i < 10u * 10u; ++i) if (pixels[(i/10u)*12u + i%10u] != 0u) ++nonzero;
    assert(nonzero > 0u);
    chos_gfx_pixel(&s, -1, 0, 1u);
    chos_gfx_pixel(&s, 0, -1, 1u);
    chos_gfx_pixel(&s, 10, 0, 1u);
    chos_gfx_pixel(&s, 0, 10, 1u);
    {
        chos_surface_t font_surface = { font_pixels, 160u, 24u, 160u };
        size_t edge_pixels = 0u;
        assert(chos_gfx_ui_text_width("WWW", 0) > chos_gfx_ui_text_width("iii", 0));
        assert(chos_gfx_ui_text_width("WWW", 1) > chos_gfx_ui_text_width("iii", 1));
        chos_gfx_clear(&font_surface, 0xff000000u);
        chos_gfx_ui_text(&font_surface, 2, 2, "CHOS sans-serif", 0xffffffffu, 0);
        for (i = 0u; i < 160u * 24u; ++i) {
            if (font_pixels[i] != 0xff000000u && font_pixels[i] != 0xffffffffu) ++edge_pixels;
        }
        assert(edge_pixels > 0u); /* subpixel coverage produces blended edge pixels */
        assert(chos_gfx_ui_text_width("CHOS", 1) > chos_gfx_ui_text_width("CHOS", 0));
    }
    chos_surface_t bad = { pixels, 13u, 10u, 12u };
    uint32_t before = pixels[0];
    assert(!chos_surface_valid(&bad));
    chos_gfx_clear(&bad, 0xdeadbeefu); /* invalid surface is a safe no-op */
    assert(pixels[0] == before);
    puts("gfx: clipping, stride, primitives, antialiased proportional/bold font checks passed");
    return 0;
}
