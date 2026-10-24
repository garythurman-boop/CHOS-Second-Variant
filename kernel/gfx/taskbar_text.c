#include <stddef.h>
#include <stdint.h>
#include "chos/gfx.h"
#include "chos/taskbar_text.h"

static size_t taskbar_string_length(const char *text) {
    size_t length = 0u;
    if (text == NULL) return 0u;
    while (text[length] != '\0') ++length;
    return length;
}

int chos_taskbar_truncate_title(const char *title, char *output,
                               size_t output_capacity, int max_width) {
    size_t title_length, i, best = 0u;
    int32_t title_width;
    static const char dots[] = "...";

    if (output == NULL || output_capacity == 0u) return 0;
    output[0] = '\0';
    if (title == NULL || title[0] == '\0' || max_width <= 0) return 0;

    title_length = taskbar_string_length(title);
    title_width = chos_gfx_ui_text_width(title, 1);
    if (title_width >= 0 && title_width <= max_width && title_length < output_capacity) {
        for (i = 0u; i < title_length; ++i) output[i] = title[i];
        output[title_length] = '\0';
        return 1;
    }

    /* Leave room for the three-character suffix and its terminator. */
    if (output_capacity < sizeof(dots) ||
        chos_gfx_ui_text_width(dots, 1) > max_width) return 0;

    for (i = 0u; i < title_length && i + sizeof(dots) < output_capacity; ++i) {
        output[i] = title[i];
        output[i + 1u] = '.';
        output[i + 2u] = '.';
        output[i + 3u] = '.';
        output[i + 4u] = '\0';
        if (chos_gfx_ui_text_width(output, 1) > max_width) break;
        best = i + 1u;
    }

    output[best] = '.';
    output[best + 1u] = '.';
    output[best + 2u] = '.';
    output[best + 3u] = '\0';
    return 1;
}
