#ifndef CHOS_TASKBAR_TEXT_H
#define CHOS_TASKBAR_TEXT_H

#include <stddef.h>

/* Fit a GUI title in logical UI pixels using the bold proportional face.
 * Returns 1 when output contains a title, 0 for invalid/empty output. When
 * truncation is necessary, the visible suffix is exactly "...". */
int chos_taskbar_truncate_title(const char *title, char *output,
                               size_t output_capacity, int max_width);

#endif
