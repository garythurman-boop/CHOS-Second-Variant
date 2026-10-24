#ifndef CHOS_DISPLAY_MODES_H
#define CHOS_DISPLAY_MODES_H

#include <stddef.h>
#include <stdint.h>

/* The framebuffer is statically backed so a mode change cannot fail due to an
 * allocator regression. The maximum backing store is 2560x1920x32bpp. */
#define CHOS_DISPLAY_MODE_COUNT 26u
#define CHOS_DISPLAY_MAX_WIDTH 2560u
#define CHOS_DISPLAY_MAX_HEIGHT 1920u

typedef struct {
    uint32_t width;
    uint32_t height;
} chos_display_mode_t;

size_t chos_display_mode_count(void);
int chos_display_mode_get(size_t index, chos_display_mode_t *out);
int chos_display_mode_index(uint32_t width, uint32_t height);
const char *chos_display_mode_aspect(size_t index);

#endif
