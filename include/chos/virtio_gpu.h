#ifndef CHOS_VIRTIO_GPU_H
#define CHOS_VIRTIO_GPU_H

#include <stddef.h>
#include <stdint.h>
#include "chos/gfx.h"
#include "chos/display_modes.h"
#include "chos/boot_info.h"

#define CHOS_GUI_WIDTH 800u
#define CHOS_GUI_HEIGHT 600u
#define CHOS_GPU_MAX_DIRTY_RECTS 12u

typedef struct { uint32_t x, y, width, height; } chos_gpu_rect_t;

/* Coalesce dirty regions only when the host flush does not include a large
 * empty gap. Callers must validate bounds before using this geometry helper. */
static inline int chos_gpu_rect_pair_should_coalesce(chos_gpu_rect_t a, chos_gpu_rect_t b) {
    uint32_t left = a.x < b.x ? a.x : b.x;
    uint32_t top = a.y < b.y ? a.y : b.y;
    uint32_t a_right = a.x + a.width, b_right = b.x + b.width;
    uint32_t a_bottom = a.y + a.height, b_bottom = b.y + b.height;
    uint32_t right = a_right > b_right ? a_right : b_right;
    uint32_t bottom = a_bottom > b_bottom ? a_bottom : b_bottom;
    uint64_t union_area = (uint64_t)(right - left) * (uint64_t)(bottom - top);
    uint64_t copied_area = (uint64_t)a.width * a.height + (uint64_t)b.width * b.height;
    return union_area <= copied_area * 2u;
}

int virtio_gpu_init(chos_surface_t *surface);
/* Use the EFI GOP framebuffer as a blit target while the PCI display driver is unavailable. */
int virtio_gpu_init_gop(const chos_boot_info_t *boot_info, chos_surface_t *surface);
int virtio_gpu_set_mode(size_t mode_index, chos_surface_t *surface);
size_t virtio_gpu_current_mode_index(void);
int virtio_gpu_present(void);
/* Hardware cursor support uses VirtIO-GPU cursorq when available. Source pixels
 * are premultiplied-independent ARGB words in the same format as embedded PNGs. */
int virtio_gpu_cursor_init(const uint32_t *argb_pixels, uint32_t width, uint32_t height,
                           uint32_t hot_x, uint32_t hot_y);
int virtio_gpu_cursor_available(void);
int virtio_gpu_cursor_move(int32_t x, int32_t y, int visible);
/* Present a validated subrectangle of the active scanout. */
int virtio_gpu_present_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
/* Update two dirty rectangles; flush together only when their union is inexpensive. */
int virtio_gpu_present_rect_pair(uint32_t x1, uint32_t y1, uint32_t width1, uint32_t height1,
                                 uint32_t x2, uint32_t y2, uint32_t width2, uint32_t height2);
/* Transfer several small, disjoint damage rectangles and flush their union once. */
int virtio_gpu_present_rects(const chos_gpu_rect_t *rects, size_t count);
int virtio_gpu_ready(void);
const char *virtio_gpu_last_error(void);
uintptr_t virtio_gpu_detected_base(void);

#endif
