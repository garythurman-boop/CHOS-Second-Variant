#include "chos/display_modes.h"

/* These are driver-supported framebuffer modes, not a promise that every host
 * window will fit onscreen. VirtIO-GPU set-scanout failures leave the previous
 * mode active and CHOS keeps 800x600 as the recovery/default mode. */
static const chos_display_mode_t supported_modes[CHOS_DISPLAY_MODE_COUNT] = {
    { 800u, 600u },       /* 4:3 fallback */
    { 1024u, 640u },      /* 16:10 */
    { 1024u, 768u },      /* 4:3 */
    { 1280u, 720u },      /* 16:9 */
    { 1280u, 800u },      /* 16:10 */
    { 1280u, 960u },      /* 4:3 */
    { 1366u, 768u },      /* common 16:9-class laptop mode */
    { 1440u, 900u },      /* 16:10 */
    { 1440u, 1080u },     /* 4:3 */
    { 1600u, 900u },      /* 16:9 */
    { 1600u, 1200u },     /* 4:3 */
    { 1680u, 1050u },     /* 16:10 */
    { 1920u, 1080u },     /* Full HD 16:9 */
    { 1920u, 1200u },     /* 16:10 */
    { 1920u, 1440u },     /* 4:3 */
    { 2048u, 1152u },     /* 16:9 */
    { 2048u, 1280u },     /* 16:10 */
    { 2048u, 1536u },     /* 4:3 */
    { 2304u, 1296u },     /* 16:9 */
    { 2304u, 1440u },     /* 16:10 */
    { 2560u, 1080u },     /* 21:9 ultrawide */
    { 2560u, 1440u },     /* QHD 16:9 */
    { 2560u, 1600u },     /* 16:10 equivalent */
    { 2560u, 1920u },     /* 4:3 equivalent */
    { 1152u, 864u },      /* 4:3 legacy/workstation */
    { 1200u, 900u }       /* 4:3 */
};

static const char *const mode_aspects[CHOS_DISPLAY_MODE_COUNT] = {
    "4:3", "16:10", "4:3", "16:9", "16:10", "4:3", "16:9*", "16:10",
    "4:3", "16:9", "4:3", "16:10", "16:9", "16:10", "4:3", "16:9",
    "16:10", "4:3", "16:9", "16:10", "21:9", "16:9", "16:10", "4:3",
    "4:3", "4:3"
};

size_t chos_display_mode_count(void) {
    return CHOS_DISPLAY_MODE_COUNT;
}

int chos_display_mode_get(size_t index, chos_display_mode_t *out) {
    if (out == 0 || index >= CHOS_DISPLAY_MODE_COUNT) return 0;
    *out = supported_modes[index];
    return 1;
}

int chos_display_mode_index(uint32_t width, uint32_t height) {
    size_t i;
    for (i = 0u; i < CHOS_DISPLAY_MODE_COUNT; ++i) {
        if (supported_modes[i].width == width && supported_modes[i].height == height) return (int)i;
    }
    return -1;
}

const char *chos_display_mode_aspect(size_t index) {
    return index < CHOS_DISPLAY_MODE_COUNT ? mode_aspects[index] : "?";
}
