#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chos/gfx.h"
#include "chos/taskbar_text.h"

int main(void) {
    char output[128];
    const char *long_title = "A Really Long Application Window Title That Must Be Cut Off";
    int max_width = 74;

    assert(chos_taskbar_truncate_title("Console", output, sizeof(output), max_width));
    assert(strcmp(output, "Console") == 0);

    assert(chos_taskbar_truncate_title(long_title, output, sizeof(output), max_width));
    assert(strlen(output) >= 3u);
    assert(strcmp(output + strlen(output) - 3u, "...") == 0);
    assert(chos_gfx_ui_text_width(output, 1) <= max_width);
    assert(strlen(output) < strlen(long_title));

    /* Exact-fit labels stay intact rather than receiving an ellipsis. */
    {
        int width = chos_gfx_ui_text_width("Settings", 1);
        assert(chos_taskbar_truncate_title("Settings", output, sizeof(output), width));
        assert(strcmp(output, "Settings") == 0);
    }

    /* A four-byte destination can hold exactly the suffix plus NUL. */
    assert(chos_taskbar_truncate_title(long_title, output, 4u, 74));
    assert(strcmp(output, "...") == 0);
    assert(!chos_taskbar_truncate_title(long_title, output, sizeof(output), 2));
    assert(output[0] == '\0');
    assert(!chos_taskbar_truncate_title(NULL, output, sizeof(output), max_width));
    assert(output[0] == '\0');
    puts("taskbar text: fixed-width title fitting and ellipsis truncation passed");
    return 0;
}
