#include <cairo.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { width = 37, height = 23 };
struct stream {
    unsigned char bytes[8192];
    size_t used;
    size_t capacity;
};

static cairo_status_t append(void *closure, const unsigned char *bytes, unsigned count)
{
    struct stream *stream = closure;
    if (count > stream->capacity - stream->used)
        return CAIRO_STATUS_WRITE_ERROR;
    memcpy(stream->bytes + stream->used, bytes, count);
    stream->used += count;
    return CAIRO_STATUS_SUCCESS;
}

static int roundtrip(unsigned seed)
{
    uint32_t pixels[width * height];
    for (unsigned i = 0; i < width * height; ++i) {
        unsigned rgb = (i * 0x04375du + seed * 0x081921u) & 0xffffffu;
        pixels[i] = i % 7 ? (0xff000000u | rgb) : 0;
    }

    cairo_surface_t *surface = cairo_image_surface_create_for_data((unsigned char *)pixels,
        CAIRO_FORMAT_ARGB32, width, height, width * sizeof(pixels[0]));
    struct stream stream = { .capacity = 20 };
    cairo_status_t failure = cairo_surface_write_to_png_stream(surface, append, &stream);
    stream.used = 0;
    stream.capacity = sizeof(stream.bytes);
    cairo_status_t result = cairo_surface_write_to_png_stream(surface, append, &stream);
    cairo_surface_destroy(surface);
    if (failure != CAIRO_STATUS_WRITE_ERROR || result != CAIRO_STATUS_SUCCESS)
        return 1;

    unsigned char decoded[width * height * 4];
    png_image image = { .version = PNG_IMAGE_VERSION };
    int success = png_image_begin_read_from_memory(&image, stream.bytes, stream.used);
    image.format = PNG_FORMAT_RGBA;
    success = success && image.width == width && image.height == height
        && png_image_finish_read(&image, NULL, decoded, 0, NULL);
    png_image_free(&image);
    if (!success)
        return 1;
    for (unsigned i = 0; i < width * height; ++i) {
        uint32_t p = pixels[i];
        if (decoded[4*i] != (uint8_t)(p >> 16) || decoded[4*i+1] != (uint8_t)(p >> 8)
            || decoded[4*i+2] != (uint8_t)p || decoded[4*i+3] != (uint8_t)(p >> 24))
            return 1;
    }
    return 0;
}

int main(void)
{
    for (unsigned i = 0; i < 12; ++i) {
        if (roundtrip(i)) {
            printf("[FAIL] Cairo PNG stream round %u\n", i);
            return 1;
        }
    }
    puts("[ OK ] Cairo PNG stream writes, callback failures and decoded pixels passed");
    return 0;
}
