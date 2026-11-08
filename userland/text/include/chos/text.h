#ifndef CHOS_TEXT_H
#define CHOS_TEXT_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* User-process font backend; not a syscall or a privileged font service.
 * Owns a copy of the input font. One handle is serialized internally; callers
 * must finish/join all uses before closing it. No handle survives close.
 * Limits supplement the kernel's process quota, not a production font sandbox.
 */
#define CHOS_TEXT_FONT_BYTES_MAX (4u*1024u*1024u)
#define CHOS_TEXT_FONTS_MAX 8u
#define CHOS_TEXT_UTF8_MAX 4096u
#define CHOS_TEXT_GLYPHS_MAX 4096u
typedef struct chos_text_font chos_text_font_t;
enum { CHOS_TEXT_OK=0, CHOS_TEXT_INVALID=1, CHOS_TEXT_NOMEM=2,
       CHOS_TEXT_BAD_FONT=3, CHOS_TEXT_LIMIT=4, CHOS_TEXT_SHAPE=5,
       CHOS_TEXT_RASTER=6 };
enum { CHOS_TEXT_AUTO=0,CHOS_TEXT_LTR=1,CHOS_TEXT_RTL=2 };
typedef struct {
    uint32_t *pixels;
    uint32_t width,height,stride;
    size_t capacity_pixels;
} chos_text_surface_t;
typedef struct {
    uint32_t glyphs,direction;
    int64_t advance_x_26_6,advance_y_26_6;
    uint32_t ink_pixels;
} chos_text_metrics_t;
int chos_text_font_open(const void *bytes,size_t length,unsigned face_index,chos_text_font_t **out);
void chos_text_font_close(chos_text_font_t *font);
int chos_text_font_pixels(chos_text_font_t *font,unsigned height);
/* UTF8 bytes are consumed with their explicit length; malformed sequences use
 * HarfBuzz's U+FFFD replacement policy. Language is an optional BCP47 hint<=63
 * bytes. Coordinates are physical pixels. Input alpha is ignored (opaque ink).
 * Output is clipped to the supplied surface. A failure after shaping/rasterizing
 * some glyphs may leave partial pixels; present only a successfully drawn frame.
 */
int chos_text_draw(chos_text_font_t *font,const char *utf8,size_t length,
                   const char *language,unsigned direction,
                   chos_text_surface_t *surface,int32_t x,int32_t baseline,
                   uint32_t color,chos_text_metrics_t *metrics);
#ifdef __cplusplus
}
#endif
#endif
